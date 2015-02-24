#include "Local.h"
#include "Daemon.h"
#include "CompilerArgs.h"
#include <Plast.h>
#include <rct/Process.h>
#include <rct/ThreadPool.h>
#include <rct/Log.h>
#include <unistd.h>
#include <stdio.h>

Local::Local()
{
}

Local::~Local()
{
}

void Local::init()
{
    mPool.setCount(Daemon::instance()->options().jobCount);
    mPool.readyReadStdOut().connect([this](ProcessPool::Id id, Process* proc) {
            mJobs.remove(id);
            const Data& data = mJobs[id];
            Job::SharedPtr job = data.job.lock();
            if (!job)
                return;
            job->mStdOut += proc->readAllStdOut();
            job->mReadyReadStdOut(job.get());
        });
    mPool.readyReadStdErr().connect([this](ProcessPool::Id id, Process* proc) {
            assert(mJobs.contains(id));
            const Data& data = mJobs[id];
            Job::SharedPtr job = data.job.lock();
            if (!job)
                return;
            job->mStdErr += proc->readAllStdErr();
            job->mReadyReadStdErr(job.get());
        });
    mPool.started().connect([this](ProcessPool::Id id, Process*) {
            assert(mJobs.contains(id));
            const Data& data = mJobs[id];
            Job::SharedPtr job = data.job.lock();
            if (!job)
                return;
            job->updateStatus(Job::Compiling);
            Connection& scheduler = Daemon::instance()->remote().scheduler();
            scheduler.send(BuildingMessage(job->remoteName(), job->compilerArgs()->sourceFile(),
                                           BuildingMessage::Start, job->id()));
        });
    mPool.finished().connect([this](ProcessPool::Id id, Process* proc) {
            error() << "pool finished for" << id;
            assert(mJobs.contains(id));
            const Data data = mJobs[id];
            const int fd = data.fd;
            const String fn = data.filename;
            Job::SharedPtr job = data.job.lock();
            const bool localForRemote = !fn.isEmpty();
            FILE* f = 0;

            mJobs.erase(id);
            if (localForRemote) {
                assert(fd != -1);
                f = fdopen(fd, "r");
                assert(f);
            }

            if (!job) {
                if (localForRemote) {
                    fclose(f);
                    unlink(fn.constData());
                }
                return;
            }

            Connection& scheduler = Daemon::instance()->remote().scheduler();
            scheduler.send(BuildingMessage(BuildingMessage::Stop, job->id()));

            const int retcode = proc->returnCode();
            if (retcode != 0) {
                if (retcode < 0) {
                    // this is bad
                    job->mError = "Invalid return code for local compile";
                }
                job->updateStatus(Job::Error);
            } else {
                if (localForRemote) {
                    // read all the compiled data
                    f = freopen(fn.constData(), "r", f);
                    assert(f);

                    job->mObjectCode = Rct::readAll(f);
                    if (job->mObjectCode.isEmpty()) {
                        job->mError = "Got no object code for compile";
                        job->updateStatus(Job::Error);
                    } else {
                        job->updateStatus(Job::Compiled);
                    }
                } else {
                    job->updateStatus(Job::Compiled);
                }
            }
            if (localForRemote) {
                fclose(f);
                unlink(fn.constData());
            }
            Job::finish(job.get());

            takeRemoteJobs();
        });
    mPool.error().connect([this](ProcessPool::Id id) {
            error() << "pool error for" << id;
            const Data& data = mJobs[id];
            const bool localForRemote = !data.filename.isEmpty();
            if (localForRemote) {
                assert(data.fd != -1);
                close(data.fd);
                unlink(data.filename.constData());
            }

            mJobs.erase(id);
            Job::SharedPtr job = data.job.lock();
            if (!job)
                return;
            job->mError = "Local compile pool returned error";
            job->updateStatus(Job::Error);
            Job::finish(job.get());

            takeRemoteJobs();
        });
}

void Local::post(const Job::SharedPtr& job)
{
    error() << "local post";
    std::shared_ptr<CompilerArgs> args = job->compilerArgs();
    List<String> cmdline = args->commandLine;
    const Path cmd = plast::resolveCompiler(cmdline.front());
    if (cmd.isEmpty()) {
        error() << "Unable to resolve compiler" << cmdline.front();
        job->mError = "Unable to resolve compiler for Local post";
        job->updateStatus(Job::Error);
        return;
    }

    Data data(job);

    if (job->type() == Job::RemoteJob) {
        assert(job->isPreprocessed());
        assert(args->sourceFileIndexes.size() == 1);

        data.filename = "/tmp/plastXXXXXXcmp";
        data.fd = mkstemps(data.filename.data(), 3);
        if (data.fd == -1) {
            // badness happened
            job->mError = "Unable to mkstemps preprocess file";
            job->updateStatus(Job::Error);
            return;
        }

        String lang;
        if (!(args->flags & CompilerArgs::HasDashX)) {
            // need to add language, see if CompilerArgs already knows
            if (args->flags & CompilerArgs::C) {
                lang = "c";
            } else if (args->flags & CompilerArgs::CPlusPlus) {
                lang = "c++";
            } else if (args->flags & CompilerArgs::ObjectiveC) {
                lang = "objective-c";
            } else if (args->flags & CompilerArgs::ObjectiveCPlusPlus) {
                lang = "objective-c++";
            } else {
                error() << "Unknown language";
                job->mError = "Unknown language for remote job";
                job->updateStatus(Job::Error);
                return;
            }
        }

        // hack the command line input argument to - and send stuff to stdin
        cmdline[args->sourceFileIndexes[0]] = "-";

        if (args->flags & CompilerArgs::HasDashO) {
            cmdline[args->objectFileIndex] = data.filename;
        } else {
            cmdline.push_back("-o");
            cmdline.push_back(data.filename);
        }

        int i = 0;
        while (i < cmdline.size()) {
            const String &arg = cmdline.at(i);
            // error() << "considering" << i << arg;
            if (arg == "-MF") {
                cmdline.remove(i, 2);
            } else if (arg == "-MT") {
                cmdline.remove(i, 2);
            } else if (arg == "-MMD") {
                cmdline.removeAt(i);
            } else if (arg.startsWith("-I")) {
                if (arg.size() == 2) {
                    cmdline.remove(i, 2);
                } else {
                    cmdline.removeAt(i);
                }
            } else {
                ++i;
            }
        }

        cmdline.removeFirst();
        cmdline.prepend(lang);
        cmdline.prepend("-x");
        error() << "Compiler resolved to" << cmd << job->path() << cmdline << data.filename;
        const ProcessPool::Id id = mPool.prepare(Path(), cmd, cmdline, List<String>(), job->preprocessed());
        mJobs[id] = data;
        mPool.post(id);
    } else {
        error() << "Compiler resolved to" << cmd << job->path() << cmdline << data.filename;
        cmdline.removeFirst();
        const ProcessPool::Id id = mPool.prepare(job->path(), cmd, cmdline);
        mJobs[id] = data;
        mPool.post(id);
    }
}

void Local::run(const Job::SharedPtr& job)
{
    error() << "local run";
    List<String> args = job->args();
    const Path cmd = plast::resolveCompiler(args.front());
    if (cmd.isEmpty()) {
        error() << "Unable to resolve compiler" << args.front();
        job->mError = "Unable to resolve compiler for Local post";
        job->updateStatus(Job::Error);
        return;
    }
    args.removeFirst();
    error() << "Compiler resolved to" << cmd << job->path() << args;
    const ProcessPool::Id id = mPool.prepare(job->path(), cmd, args);
    mJobs[id] = job;
    mPool.run(id);
}

void Local::takeRemoteJobs()
{
    for (;;) {
        if (!mPool.isIdle())
            break;
        const Job::SharedPtr job = Daemon::instance()->remote().take();
        if (!job)
            break;
        post(job);
    }
    if (mPool.isIdle())
        Daemon::instance()->remote().requestMore();
}

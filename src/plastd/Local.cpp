#include "Local.h"
#include "Daemon.h"
#include "CompilerArgs.h"
#include <Plast.h>
#include <rct/Process.h>
#include <rct/ThreadPool.h>
#include <rct/Log.h>
#include <unistd.h>
#include <stdio.h>

Local::Local(int overcommit)
    : mOvercommit(overcommit)
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
            static uint32_t count = 0;
            error() << "started" << ++count << "jobs";
            assert(mJobs.contains(id));
            const Data& data = mJobs[id];
            Job::SharedPtr job = data.job.lock();
            if (!job)
                return;
            job->updateStatus(Job::Compiling);
            if (data.posted) {
                std::shared_ptr<Connection> scheduler = Daemon::instance()->remote().scheduler();
                scheduler->send(BuildingMessage(job->remoteName(), job->compilerArgs()->sourceFile(),
                                                BuildingMessage::Start, job->id()));
            }
        });
    mPool.finished().connect([this](ProcessPool::Id id, Process* proc) {
            error() << "pool finished for" << id;
            assert(mJobs.contains(id));
            const Data data = mJobs[id];
            const String fn = data.filename;
            Job::SharedPtr job = data.job.lock();
            const bool localForRemote = !fn.isEmpty();

            mJobs.erase(id);

            if (data.posted) {
                std::shared_ptr<Connection> scheduler = Daemon::instance()->remote().scheduler();
                scheduler->send(BuildingMessage(data.remoteName, BuildingMessage::Stop, data.jobid));
            }

            if (!job) {
                error() << "job not found in finish";
                if (localForRemote) {
                    unlink(fn.constData());
                }
                return;
            }
            assert(job->id() == data.jobid);

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
                    FILE* f = fopen(fn.constData(), "r");
                    assert(f);
                    job->mObjectCode = Rct::readAll(f);
                    fclose(f);
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
                unlink(fn.constData());
            }
            Job::finish(job.get());
        });
    mPool.error().connect([this](ProcessPool::Id id) {
            error() << "pool error for" << id;
            const Data data = mJobs[id];
            const bool localForRemote = !data.filename.isEmpty();
            if (localForRemote) {
                unlink(data.filename.constData());
            }

            mJobs.erase(id);

            if (data.posted) {
                std::shared_ptr<Connection> scheduler = Daemon::instance()->remote().scheduler();
                scheduler->send(BuildingMessage(data.remoteName, BuildingMessage::Stop, data.jobid));
            }

            Job::SharedPtr job = data.job.lock();
            if (!job) {
                error() << "job not found in error";
                return;
            }

            assert(job->id() == data.jobid);

            job->mError = "Local compile pool returned error";
            job->updateStatus(Job::Error);
            Job::finish(job.get());

            takeRemoteJobs();
        });
    mPool.idle().connect([this](ProcessPool*) {
            takeRemoteJobs();
        });
}

void Local::post(const Job::SharedPtr& job)
{
    job->aborted().connect(std::bind(&Local::handleJobAborted, this, std::placeholders::_1));

    error() << "local post";
    std::shared_ptr<CompilerArgs> args = job->compilerArgs();
    List<String> cmdline = args->commandLine;
    const Path cmd = job->resolvedCompiler();
    if (cmd.isEmpty()) {
        error() << "Unable to resolve compiler" << cmdline.front();
        job->mError = "Unable to resolve compiler for Local post";
        job->updateStatus(Job::Error);
        return;
    }
    assert(cmd.isAbsolute());

    Data data(job, true);

    if (job->type() == Job::RemoteJob) {
        assert(job->isPreprocessed());
        assert(args->sourceFileIndexes.size() == 1);

        data.filename = "/tmp/plastXXXXXXcmp";
        const int fd = mkstemps(data.filename.data(), 3);
        if (fd == -1) {
            // badness happened
            job->mError = "Unable to mkstemps preprocess file";
            job->updateStatus(Job::Error);
            return;
        }
        close(fd);

        String lang;
        if (!(args->flags & CompilerArgs::HasDashX)) {
            CompilerArgs::Flag f = static_cast<CompilerArgs::Flag>(args->flags & CompilerArgs::LanguageMask);
            lang = CompilerArgs::languageName(f, true);
            if (lang.isEmpty()) {
                error() << "Unknown language" << args->sourceFile();
                job->mError = "Unknown language for remote job " + args->sourceFile();
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
        warning() << "Compiler resolved to" << cmd << job->path() << cmdline << data.filename;
        const ProcessPool::Id id = mPool.prepare(Path(), cmd, cmdline, List<String>(), job->preprocessed());
        mJobs[id] = data;
        mPool.post(id);
    } else {
        if (job->isPreprocessed()) {
            warning() << "preprocessed remote job became local" << job->id();
            Daemon::instance()->remote().compilingLocally(job);
        }
        warning() << "Compiler resolved to" << cmd << job->path() << cmdline << data.filename;
        cmdline.removeFirst();
        const ProcessPool::Id id = mPool.prepare(job->path(), cmd, cmdline);
        mJobs[id] = data;
        mPool.post(id);
    }
}

void Local::run(const Job::SharedPtr& job)
{
    job->aborted().connect(std::bind(&Local::handleJobAborted, this, std::placeholders::_1));

    assert(!job->isPreprocessed());
    warning() << "local run";
    List<String> args = job->args();
    const Path cmd = job->resolvedCompiler();
    if (cmd.isEmpty()) {
        error() << "Unable to resolve compiler" << args.front();
        job->mError = "Unable to resolve compiler for Local post";
        job->updateStatus(Job::Error);
        return;
    }
    args.removeFirst();
    warning() << "Compiler resolved to" << cmd << job->path() << args;
    const ProcessPool::Id id = mPool.prepare(job->path(), cmd, args);
    mJobs[id] = Data(job, false);
    mPool.run(id);
}

void Local::handleJobAborted(Job* job)
{
    // not very efficient
    auto it = mJobs.begin();
    const auto end = mJobs.cend();
    while (it != end) {
        if (it->second.jobid == job->id()) {
            mPool.kill(it->first);
            return;
        }
        ++it;
    }
}

void Local::takeRemoteJobs()
{
    warning() << "takeRemoteJobs?";
    for (;;) {
        if (!mPool.isIdle()) {
            warning() << "pool is not idle";
            break;
        }
        warning() << "pool is idle, taking remote?";
        const Job::SharedPtr job = Daemon::instance()->remote().take();
        if (!job) {
            warning() << "no remote jobs";
            break;
        }
        warning() << "took remote job";
        job->clearPreprocessed();
        post(job);
    }
    if (mPool.isIdle())
        Daemon::instance()->remote().requestMore();
}

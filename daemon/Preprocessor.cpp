#include "Preprocessor.h"
#include "CompilerArgs.h"
#include <Plast.h>
#include <rct/Process.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>

Preprocessor::Preprocessor()
{
    mPool.readyReadStdOut().connect([this](ProcessPool::Id id, Process* proc) {
            Job::SharedPtr job = mJobs[id].job.lock();
            if (!job)
                return;
            job->mStdOut += proc->readAllStdOut();
            job->mReadyReadStdOut(job.get());
        });
    mPool.readyReadStdErr().connect([this](ProcessPool::Id id, Process* proc) {
            // throw stderr data away, mark job as having errors
            Job::SharedPtr job = mJobs[id].job.lock();
            if (!job)
                return;
            job->mStdErr += proc->readAllStdErr();
            job->mReadyReadStdErr(job.get());
        });
    mPool.started().connect([this](ProcessPool::Id id, Process*) {
            Job::SharedPtr job = mJobs[id].job.lock();
            if (!job)
                return;
            job->updateStatus(Job::Preprocessing);
        });
    mPool.finished().connect([this](ProcessPool::Id id, Process* proc) {
            Hash<ProcessPool::Id, Data>::iterator data = mJobs.find(id);
            assert(data != mJobs.end());
            Job::SharedPtr job = data->second.job.lock();
            const int fd = data->second.fd;
            assert(fd != -1);
            FILE* f = fdopen(fd, "r");
            assert(f);
            if (job) {
                if (proc->returnCode() != 0) {
                    job->mError = "Preprocess failed";
                    job->updateStatus(Job::Error);
                } else {
                    // read all the preprocessed data
                    f = freopen(data->second.filename.constData(), "r", f);
                    assert(f);

                    char buf[65536];
                    size_t r;
                    while (!feof(f) && !ferror(f)) {
                        r = fread(buf, 1, sizeof(buf), f);
                        if (r) {
                            job->mPreprocessed.append(buf, r);
                        }
                    }
                    if (job->mPreprocessed.isEmpty()) {
                        job->mError = "Got no data from stdout for preprocess";
                        job->updateStatus(Job::Error);
                    } else {
                        job->updateStatus(Job::Preprocessed);
                    }
                }
            }
            fclose(f);
            unlink(data->second.filename.constData());
            data->second.fd = -1;
            mJobs.erase(data);
        });
    mPool.error().connect([this](ProcessPool::Id id) {
            Hash<ProcessPool::Id, Data>::iterator data = mJobs.find(id);
            assert(data != mJobs.end());
            assert(data->second.fd != -1);
            close(data->second.fd);
            unlink(data->second.filename.constData());
            data->second.fd = -1;
            Job::SharedPtr job = data->second.job.lock();
            if (job) {
                job->mError = "Unable to start job for preprocess";
                job->updateStatus(Job::Error);
            }
            mJobs.erase(data);
        });
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::preprocess(const Job::SharedPtr& job)
{
    Data data(job);
    data.filename = "/tmp/plastXXXXXXpre";
    data.fd = mkstemps(data.filename.data(), 3);
    if (data.fd == -1) {
        // badness happened
        job->mError = "Unable to mkstemps preprocess file";
        job->updateStatus(Job::Error);
        return;
    }

    std::shared_ptr<CompilerArgs> args = job->compilerArgs();
    List<String> cmdline = args->commandLine;
    if (args->flags & CompilerArgs::HasDashO) {
        cmdline[args->objectFileIndex] = data.filename;
    } else {
        cmdline.push_back("-o");
        cmdline.push_back(data.filename);
    }
    cmdline.push_back("-E");
    const Path compiler = plast::resolveCompiler(cmdline.front());
    cmdline.removeFirst();

    const ProcessPool::Id id = mPool.prepare(job->path(), compiler, cmdline);
    mJobs[id] = data;
    mPool.post(id);
}

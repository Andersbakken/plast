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
            if (job) {
                if (proc->returnCode() != 0) {
                    job->mError = "Preprocess failed";
                    job->updateStatus(Job::Error);
                } else {
                    // read all the preprocessed data
                    char buf[65536];
                    size_t r;
                    FILE* f = fopen(data->second.filename.constData(), "r");
                    assert(f);
                    job->mPreprocessed.clear();
                    while (!feof(f) && !ferror(f)) {
                        r = fread(buf, 1, sizeof(buf), f);
                        if (r) {
                            job->mPreprocessed.append(buf, r);
                        }
                    }
                    fclose(f);
                    if (job->mPreprocessed.isEmpty()) {
                        job->mError = "Got no data from stdout for preprocess";
                        job->updateStatus(Job::Error);
                    } else {
                        job->updateStatus(Job::Preprocessed);
                    }
                }
            }
            unlink(data->second.filename.constData());
            mJobs.erase(data);
        });
    mPool.error().connect([this](ProcessPool::Id id) {
            Hash<ProcessPool::Id, Data>::iterator data = mJobs.find(id);
            assert(data != mJobs.end());
            unlink(data->second.filename.constData());
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

bool Preprocessor::preprocess(const Job::SharedPtr& job)
{
    Data data(job);
    data.filename = "/tmp/plastXXXXXXpre";
    const int fd = mkstemps(data.filename.data(), 3);
    if (fd == -1) {
        // badness happened
        job->mError = "Unable to mkstemps preprocess file";
        job->updateStatus(Job::Error);
        return false;
    }
    close(fd);

    std::shared_ptr<CompilerArgs> args = job->compilerArgs();
    List<String> cmdline = args->commandLine;
    if (args->flags & CompilerArgs::HasDashO) {
        cmdline[args->objectFileIndex] = data.filename;
    } else {
        cmdline.push_back("-o");
        cmdline.push_back(data.filename);
    }
    cmdline.push_back("-E");
    const Path compiler = job->resolvedCompiler();
    cmdline.removeFirst();

    job->mPreprocessed.resize(1);
    const ProcessPool::Id id = mPool.prepare(job->path(), compiler, cmdline);
    mJobs[id] = data;
    mPool.post(id);
    return true;
}

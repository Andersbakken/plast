#include "ProcessPool.h"
#include <rct/Process.h>
#include <rct/Log.h>

ProcessPool::ProcessPool(int count)
    : mCount(count), mRunning(0), mNextId(0)
{
}

ProcessPool::~ProcessPool()
{
}

void ProcessPool::setCount(int count)
{
    mCount = count;
}

bool ProcessPool::runProcess(Process*& proc, const Job& job, bool except)
{
    static Hash<Process*, Id> ids;
    if (!proc) {
        proc = new Process;
        proc->readyReadStdOut().connect([this](Process* proc) {
                const Id id = ids[proc];
                mReadyReadStdOut(id, proc);
            });
        proc->readyReadStdErr().connect([this](Process* proc) {
                const Id id = ids[proc];
                mReadyReadStdErr(id, proc);
            });
        proc->finished().connect([this, except](Process* proc) {
                --mRunning;
                Hash<Process*, Id>::iterator idit = ids.find(proc);
                assert(idit != ids.end());
                const Id id = idit->second;
                ids.erase(idit);
                mFinished(id, proc);
                if (except) {
                    EventLoop::eventLoop()->deleteLater(proc);
                } else {
                    while (!mPending.isEmpty()) {
                        // take one from the back of mPending if possible
                        if (!runProcess(proc, mPending.front(), false)) {
                            mError(mPending.front().id);
                            mPending.pop_front();
                        } else {
                            mPending.pop_front();
                            return;
                        }
                    }
                    // make this process available for new jobs
                    mAvail.push_back(proc);
                }
            });
    }
    proc->clear();
    ids[proc] = job.id;
    if (!job.path.isEmpty()) {
        proc->setCwd(job.path);
    }
    const bool ok = proc->start(job.command, job.arguments, job.environ);
    if (ok) {
        ++mRunning;
        if (!job.stdin.isEmpty()) {
            proc->write(job.stdin);
            proc->closeStdIn();
        }
        mStarted(job.id, proc);
    }
    return ok;
}

ProcessPool::Id ProcessPool::prepare(const Path& path, const Path &command, const List<String> &arguments,
                                     const List<String> &environ, const String& stdin)
{
    const Id id = ++mNextId;
    Job job = { id, path, command, arguments, environ, stdin };
    mPrepared[id] = job;
    return id;
}

void ProcessPool::post(Id id)
{
    Hash<Id, Job>::iterator it = mPrepared.find(id);
    assert(it != mPrepared.end());
    const Job& job = it->second;

    if (!mAvail.isEmpty()) {
        Process* proc = mAvail.back();
        mAvail.pop_back();
        if (!runProcess(proc, job, false)) {
            mError(id);
            mPrepared.erase(it);
            return;
        }
    } else if (mProcs.size() < mCount) {
        mProcs.push_back(0);
        if (!runProcess(mProcs.back(), job, false)) {
            mError(id);
            mPrepared.erase(it);
            return;
        }
    } else {
        mPending.push_back(job);
    }
    mPrepared.erase(it);
}

void ProcessPool::run(Id id)
{
    Hash<Id, Job>::iterator it = mPrepared.find(id);
    assert(it != mPrepared.end());
    const Job& job = it->second;
    if (!mAvail.isEmpty()) {
        Process* proc = mAvail.back();
        mAvail.pop_back();
        if (!runProcess(proc, job, false)) {
            mError(id);
            mPrepared.erase(it);
            return;
        }
    } else if (mProcs.size() < mCount) {
        mProcs.push_back(0);
        if (!runProcess(mProcs.back(), job, false)) {
            mError(id);
            mPrepared.erase(it);
            return;
        }
    } else {
        // create a process right here
        Process* proc = 0;
        if (!runProcess(proc, job, true)) {
            mError(id);
            mPrepared.erase(it);
            delete proc;
            return;
        }
    }
    mPrepared.erase(it);
}

#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <rct/List.h>
#include <rct/Hash.h>
#include <rct/LinkedList.h>
#include <rct/String.h>
#include <rct/Path.h>
#include <rct/SignalSlot.h>
#include <cstdint>
#include <signal.h>

class Process;

class ProcessPool
{
public:
    typedef uint32_t Id;

    ProcessPool(int count = 0);
    ~ProcessPool();

    void setCount(int count);

    Id prepare(const Path& path,
               const Path& command,
               const List<String>& arguments = List<String>(),
               const List<String>& environ = List<String>(),
               const String& stdin = String());
    void post(Id id);
    void run(Id id);
    bool kill(Id id, int sig = SIGTERM);

    Signal<std::function<void(Id, Process*)> >& started() { return mStarted; }
    Signal<std::function<void(Id, Process*)> >& readyReadStdOut() { return mReadyReadStdOut; }
    Signal<std::function<void(Id, Process*)> >& readyReadStdErr() { return mReadyReadStdErr; }
    Signal<std::function<void(Id, Process*)> >& finished() { return mFinished; }
    Signal<std::function<void(ProcessPool*)> >& idle() { return mIdle; }
    Signal<std::function<void(Id)> >& error() { return mError; }

    bool isIdle() const { return !mAvail.isEmpty() || mProcs.size() < mCount; }
    int running() const { return mRunning; }
    int pending() const { return mPending.size(); }
    int max() const { return mCount; }

private:
    struct Job
    {
        Id id;
        Path path, command;
        List<String> arguments, environ;
        String stdin;
        Process* process;
    };

    bool runProcess(Process*& proc, Job& job, bool except);

private:
    int mCount;
    int mRunning;
    Id mNextId;
    List<Process*> mProcs, mAvail;
    Signal<std::function<void(Id, Process*)> > mStarted, mReadyReadStdOut, mReadyReadStdErr, mFinished;
    Signal<std::function<void(Id)> > mError;
    Signal<std::function<void(ProcessPool*)> > mIdle;

    LinkedList<Job> mPending;
    Hash<Id, Job> mPrepared, mRunningJobs;
};

#endif

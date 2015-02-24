#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <rct/List.h>
#include <rct/Hash.h>
#include <rct/LinkedList.h>
#include <rct/String.h>
#include <rct/Path.h>
#include <rct/SignalSlot.h>

class Process;

class ProcessPool
{
public:
    typedef unsigned int Id;

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

    Signal<std::function<void(Id, Process*)> >& started() { return mStarted; }
    Signal<std::function<void(Id, Process*)> >& readyReadStdOut() { return mReadyReadStdOut; }
    Signal<std::function<void(Id, Process*)> >& readyReadStdErr() { return mReadyReadStdErr; }
    Signal<std::function<void(Id, Process*)> >& finished() { return mFinished; }
    Signal<std::function<void(Id)> >& error() { return mError; }

    bool isIdle() const { return !mAvail.isEmpty() || mProcs.size() < mCount; }
    int pending() const { return mRunning + mPending.size(); }
    int max() const { return mCount; }

private:
    struct Job
    {
        Id id;
        Path path, command;
        List<String> arguments, environ;
        String stdin;
    };

    bool runProcess(Process*& proc, const Job& job, bool except);

private:
    int mCount;
    int mRunning;
    Id mNextId;
    List<Process*> mProcs, mAvail;
    Signal<std::function<void(Id, Process*)> > mStarted, mReadyReadStdOut, mReadyReadStdErr, mFinished;
    Signal<std::function<void(Id)> > mError;

    LinkedList<Job> mPending;
    Hash<Id, Job> mPrepared;
};

#endif

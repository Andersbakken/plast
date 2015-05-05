#ifndef JOB_H
#define JOB_H

#include "CompilerVersion.h"
#include <rct/Hash.h>
#include <rct/List.h>
#include <rct/Path.h>
#include <rct/String.h>
#include <rct/SignalSlot.h>
#include <rct/Log.h>
#include <memory>
#include <cstdint>
#include <stdio.h>

struct CompilerArgs;

class Job : public std::enable_shared_from_this<Job>
{
public:
    typedef std::shared_ptr<Job> SharedPtr;
    typedef std::weak_ptr<Job> WeakPtr;

    ~Job();

    enum Type { LocalJob, RemoteJob };

    static SharedPtr create(const Path& path, const List<String>& args, Type type,
                            const String& remoteName, uint64_t remoteId = 0,
                            const String& preprocessed = String(),
                            uint32_t serial = 0,
                            plast::CompilerType ctype = plast::Unknown,
                            int32_t cmajor = 0, const String& ctarget = String());
    static SharedPtr job(uint64_t j);

    void start();
    void abort();

    enum Status { Idle, Preprocessing, Preprocessed, RemotePending, RemoteReceiving, Compiling, Compiled, Error, Aborted };
    static const char *statusName(Status status);
    Signal<std::function<void(Job*, Status, Status)> >& statusChanged() { return mStatusChanged; }
    Signal<std::function<void(Job*)> >& readyReadStdOut() { return mReadyReadStdOut; }
    Signal<std::function<void(Job*)> >& readyReadStdErr() { return mReadyReadStdErr; }
    Signal<std::function<void(Job*)> >& destroyed() { return mDestroyed; }

    plast::CompilerType compilerType() const { return mCompilerType; }
    int32_t compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }

    Status status() const { return mStatus; }
    bool isPreprocessed() const { return !mPreprocessed.isEmpty(); }
    Path path() const { return mPath; }
    Path resolvedCompiler() const { return mResolvedCompiler; }
    String preprocessed() const { return mPreprocessed; }
    void clearPreprocessed() { mPreprocessed.clear(); }
    String objectCode() const { return mObjectCode; }
    List<String> args() const { return mArgs; }
    std::shared_ptr<CompilerArgs> compilerArgs() const { return mCompilerArgs; }
    Type type() const { return mType; }

    String readAllStdOut();
    String readAllStdErr();

    String error() const { return mError; }

    uint64_t id() const { return mId; }
    uint64_t remoteId() const { return mRemoteId; }

    String remoteName() const { return mRemoteName; }

    uint32_t serial() const { return mSerial; }
    void increaseSerial() { mSerial += 1; }

private:
    Job(const Path& path, const List<String>& args, Type type, uint64_t remoteId,
        const String& preprocessed, uint32_t serial, const String& remoteName,
        plast::CompilerType ctype, int32_t cmajor, const String& ctarget);

    void writeFile(const String& data);
    void updateStatus(Status status);

    static void finish(Job* job);

private:
    Signal<std::function<void(Job*, Status, Status)> > mStatusChanged;
    Signal<std::function<void(Job*)> > mReadyReadStdOut, mReadyReadStdErr, mDestroyed;
    String mError;
    List<String> mArgs;
    std::shared_ptr<CompilerArgs> mCompilerArgs;
    Path mPath, mResolvedCompiler;
    uint64_t mRemoteId;
    String mPreprocessed, mObjectCode;
    String mStdOut, mStdErr;
    Status mStatus;
    Type mType;
    uint32_t mSerial;
    uint64_t mId;
    String mRemoteName;
    plast::CompilerType mCompilerType;
    int32_t mCompilerMajor;
    String mCompilerTarget;

    static Hash<uint64_t, SharedPtr> sJobs;
    static uint64_t sNextId;

    friend class Local;
    friend class Preprocessor;
    friend class Remote;
};

inline void Job::updateStatus(Status status)
{
    assert(status != mStatus);
    if (mStatus != Aborted) {
        const Status old = mStatus;
        mStatus = status;
        mStatusChanged(this, status, old);
    }
}

inline Log operator<<(Log log, Job::Status status)
{
    log << Job::statusName(status);
    return log;
}

#endif

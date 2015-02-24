#include "Job.h"
#include "CompilerArgs.h"
#include "Local.h"
#include "Daemon.h"

Hash<uint64_t, Job::SharedPtr> Job::sJobs;
uint64_t Job::sNextId = 0;

Job::Job(const Path& path, const List<String>& args, Type type,
         uint64_t remoteId, const String& preprocessed, int serial, const String& remoteName)
    : mArgs(args), mPath(path), mRemoteId(remoteId), mPreprocessed(preprocessed),
      mStatus(Idle), mType(type), mSerial(serial), mId(++sNextId), mRemoteName(remoteName)
{
#warning this breaks rtags
    mCompilerArgs = CompilerArgs::create(mArgs);
}

Job::~Job()
{
}

Job::SharedPtr Job::create(const Path& path, const List<String>& args, Type type,
                           const String& remoteName, uint64_t remoteId,
                           const String& preprocessed, int serial)
{
    Job::SharedPtr job(new Job(path, args, type, remoteId, preprocessed, serial, remoteName));
    sJobs[job->id()] = job;
    return job;
}

Job::SharedPtr Job::job(uint64_t id)
{
    Hash<uint64_t, Job::SharedPtr>::const_iterator it = sJobs.find(id);
    if (it == sJobs.end())
        return SharedPtr();
    return it->second;
}

void Job::start()
{
    Local& local = Daemon::instance()->local();
    if (mCompilerArgs->mode != CompilerArgs::Compile) {
        assert(mType == LocalJob);
        local.run(shared_from_this());
    } else if (local.isAvailable() || mType == RemoteJob || mCompilerArgs->sourceFileIndexes.size() != 1) {
        local.post(shared_from_this());
    } else {
        assert(mType == LocalJob);
        Daemon::instance()->remote().post(shared_from_this());
    }
}

void Job::finish(Job* job)
{
    sJobs.erase(reinterpret_cast<uint64_t>(job));
}

String Job::readAllStdOut()
{
    String ret;
    std::swap(ret, mStdOut);
    return ret;
}

String Job::readAllStdErr()
{
    String ret;
    std::swap(ret, mStdErr);
    return ret;
}

void Job::writeFile(const String& data)
{
    // see if we can open
    Path out = mCompilerArgs->output();
    if (out.isEmpty()) {
        mError = "Compiler output empty";
        updateStatus(Error);
        return;
    }
    out = mPath.ensureTrailingSlash() + out;
    FILE* file = fopen(out.constData(), "w");
    if (!file) {
        mError = "fopen failed";
        updateStatus(Error);
        return;
    }

    const size_t w = fwrite(data.constData(), data.size(), 1, file);
    if (w != 1) {
        // bad
        mError = "fwrite failed";
        updateStatus(Error);
    }
    fclose(file);
}
const char* Job::statusName(Status status)
{
    switch (status) {
    case Idle: return "idle";
    case Preprocessing: return "preprocessing";
    case Preprocessed: return "preprocessed";
    case RemotePending: return "remotepending";
    case RemoteReceiving: return "remotereceiving";
    case Compiling: return "compiling";
    case Compiled: return "compiled";
    case Error: return "error";
    }
    return "";
}
void Job::abort()
{
#warning implement me
}

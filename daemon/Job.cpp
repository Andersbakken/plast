#include "Job.h"
#include "CompilerArgs.h"
#include "Local.h"
#include "Daemon.h"

Hash<uintptr_t, Job::SharedPtr> Job::sJobs;

Job::Job(const Path& path, const List<String>& args, Type type,
         uintptr_t remoteId, const String& preprocessed)
    : mArgs(args), mPath(path), mRemoteId(remoteId), mPreprocessed(preprocessed),
      mType(type), mFile(0), mFileError(false)
{
    mCompilerArgs = CompilerArgs::create(mArgs);
}

Job::~Job()
{
    if (mFile) {
        fclose(mFile);
    }
}

Job::SharedPtr Job::create(const Path& path, const List<String>& args, Type type,
                           uintptr_t remoteId, const String& preprocessed)
{
    Job::SharedPtr job(new Job(path, args, type, remoteId, preprocessed));
    sJobs[reinterpret_cast<uintptr_t>(job.get())] = job;
    return job;
}

Job::SharedPtr Job::job(uintptr_t id)
{
    Hash<uintptr_t, Job::SharedPtr>::const_iterator it = sJobs.find(id);
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
    sJobs.erase(reinterpret_cast<uintptr_t>(job));
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

void Job::closeFile()
{
    if (mFileError) {
        assert(!mFile);
        return;
    }
    if (mFile) {
        fclose(mFile);
        mFile = 0;
    }
}

void Job::appendFile(const String& data)
{
    if (mFileError)
        return;
    if (!mFile) {
        // see if we can open
        Path out = mCompilerArgs->output();
        if (out.isEmpty()) {
            mFileError = true;
            mError = "Compiler output empty";
            mStatusChanged(this, Error);
            return;
        }
        out = mPath.ensureTrailingSlash() + out;
        mFile = fopen(out.constData(), "w");
        if (!mFile) {
            mFileError = true;
            mError = "fopen failed";
            mStatusChanged(this, Error);
            return;
        }
    }
    const size_t w = fwrite(data.constData(), data.size(), 1, mFile);
    if (w != 1) {
        // bad
        fclose(mFile);
        mFile = 0;
        mFileError = true;
        mError = "fwrite failed";
        mStatusChanged(this, Error);
    }
}

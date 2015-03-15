#ifndef LOCAL_H
#define LOCAL_H

#include <rct/Hash.h>
#include "ProcessPool.h"
#include "Job.h"
#include <cstdint>

class Local
{
public:
    Local(int overcommit = 0);
    ~Local();

    void init();

    void post(const Job::SharedPtr& job);
    void run(const Job::SharedPtr& job);

    bool isAvailable() const { return mPool.isIdle() || mPool.pending() < mOvercommit; }
    uint32_t availableCount() const { return std::max<int>(mPool.max() - mPool.running() + mOvercommit, 0); }

private:
    void takeRemoteJobs();

private:
    ProcessPool mPool;
    struct Data
    {
        Data() {}
        Data(const Job::SharedPtr& j, uint64_t id, bool p) : job(j), jobid(id), posted(p) {}

        String filename;
        Job::WeakPtr job;
        uint64_t jobid;
        bool posted;
    };
    Hash<ProcessPool::Id, Data> mJobs;
    int mOvercommit;
};

#endif

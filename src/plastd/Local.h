#ifndef LOCAL_H
#define LOCAL_H

#include <rct/Hash.h>
#include "ProcessPool.h"
#include "Job.h"

class Local
{
public:
    Local(int overcommit = 0);
    ~Local();

    void init();

    void post(const Job::SharedPtr& job);
    void run(const Job::SharedPtr& job);

    bool isAvailable() const { return mPool.isIdle() || mPool.pending() < mOvercommit; }
    unsigned int availableCount() const { return std::max<int>(mPool.max() - mPool.running() + mOvercommit, 0); }

private:
    void takeRemoteJobs();

private:
    ProcessPool mPool;
    struct Data
    {
        Data() : fd(-1) {}
        Data(const Job::SharedPtr& j) : fd(-1), job(j) {}

        int fd;
        String filename;
        Job::WeakPtr job;
    };
    Hash<ProcessPool::Id, Data> mJobs;
    int mOvercommit;
};

#endif

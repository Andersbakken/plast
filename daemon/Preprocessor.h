#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <rct/SignalSlot.h>
#include <rct/List.h>
#include <rct/Hash.h>
#include <rct/Path.h>
#include <rct/String.h>
#include "ProcessPool.h"
#include "Job.h"

class Preprocessor
{
public:
    Preprocessor();
    ~Preprocessor();

    void setCount(int count) { mPool.setCount(count); }
    void preprocess(const Job::SharedPtr& job);

private:
    ProcessPool mPool;
    struct Data
    {
        Data() : fd(-1) {}
        Data(const Job::SharedPtr& j) : job(j), fd(-1) {}

        Job::WeakPtr job;
        String filename;
        int fd;
    };
    Hash<ProcessPool::Id, Data> mJobs;
};

#endif

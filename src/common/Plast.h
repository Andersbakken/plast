#ifndef PLAST_H
#define PLAST_H

#include <rct/Path.h>

namespace plast {

enum CompilerType {
    Unknown,
    ClangApple,
    Clang,
    GCC
};

struct CompilerKey
{
    CompilerType type;
    int major;
    String target;

    bool operator<(const CompilerKey& other) const
    {
        if (type < other.type)
            return true;
        if (type > other.type)
            return false;
        if (major < other.major)
            return true;
        if (major > other.major)
            return false;
        return target < other.target;
    }
};

Path resolveCompiler(const Path &path);
Path defaultSocketFile();
enum {
    DefaultServerPort = 5166,
    DefaultDaemonPort = 5167,
    DefaultDiscoveryPort = 5168,
    DefaultHttpPort = 5169,
    DefaultRescheduleTimeout = 15000,
    DefaultRescheduleCheck = 2500
};
const String DefaultServerHost = "127.0.0.1";

enum {
    HasJobsMessageId = 32,
    JobMessageId,
    LastJobMessageId,
    RequestJobsMessageId,
    HandshakeMessageId,
    JobResponseMessageId,
    PeerMessageId,
    BuildingMessageId,
};

} // namespace plast

#endif

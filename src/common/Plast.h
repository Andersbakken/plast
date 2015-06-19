#ifndef PLAST_H
#define PLAST_H

#include <rct/Path.h>

namespace plast {

Path resolveCompiler(const Path &path);
Path defaultSocketFile();
enum {
    DefaultServerPort = 5166,
    DefaultDaemonPort = 5167,
    DefaultDiscoveryPort = 5168,
    DefaultHttpPort = 5169,
    DefaultRescheduleTimeout = 15000,
    DefaultRescheduleCheck = 2500,
    DefaultOvercommit = 4,
    DefaultMaxPreprocessPending = 100,

    ConnectionVersion = 1
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
    CompilerMessageId
};

} // namespace plast

#endif

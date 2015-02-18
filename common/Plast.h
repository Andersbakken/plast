#ifndef PLAST_H
#define PLAST_H

#include <rct/Path.h>

namespace plast {

Path defaultSocketFile();
enum {
    DefaultServerPort = 5166,
    DefaultDaemonPort = 5167,
    DefaultDiscoveryPort = 5168,
    DefaultHttpPort = 5169
};
const String DefaultServerHost = "127.0.0.1";

enum {
    HasJobsMessageId = 32,
    JobMessageId = 33
};

} // namespace plast

#endif

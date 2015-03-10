#include "Client.h"
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    const char *logFile = 0;
    int logLevel = 0;
    unsigned int logFlags = 0;
    Path logPath;
    if (!initLogging(argv[0], LogStderr, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s 0x%0x\n",
                logLevel, logFile ? logFile : "", logFlags);
        return 1;
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Client client;
    client.run(argc, argv);

    loop->exec();

    return client.exitCode();
}

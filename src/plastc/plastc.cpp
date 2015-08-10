#include "Client.h"
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    const char *logFile = 0;
    Flags<LogFileFlag> logFlags;
    Path logPath;
    const LogLevel logLevel = LogLevel::Error;
    if (!initLogging(argv[0], LogStderr, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s %s\n",
                logLevel.toInt(), logFile ? logFile : "", logFlags.toString().constData());
        return 1;
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Client client;
    client.run(argc, argv);

    loop->exec();

#if 0
    FILE *f = fopen("/tmp/out", "a");
    for (int i=0; i<argc; ++i) {
        if (i)
            fwrite(" ", 1, 1, f);
        fwrite(argv[i], strlen(argv[i]), 1, f);
    }
    fprintf(f, " => %d\n", client.exitCode());
    fclose(f);
#endif

    return client.exitCode();
}

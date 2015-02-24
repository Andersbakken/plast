#include "Daemon.h"
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Config.h>
#include <rct/ThreadPool.h>
#include <stdio.h>

template<typename T>
inline bool validate(int64_t c, const char* name, String& err)
{
    if (c < 0) {
        err = String::format<128>("Invalid %s. Must be >= 0", name);
        return false;
    } else if (c > std::numeric_limits<T>::max()) {
        err = String::format<128>("Invalid %s. Must be <= %d", name, std::numeric_limits<T>::max());
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);

    const int idealThreadCount = ThreadPool::idealThreadCount();
    const String socketPath = plast::defaultSocketFile();

    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<int>("job-count", String::format<128>("Job count (defaults to %d)", idealThreadCount), 'j', idealThreadCount,
                                [](const int &count, String &err) { return validate<int>(count, "job-count", err); });
    Config::registerOption<int>("preprocess-count", String::format<128>("Preprocess count (defaults to %d)", idealThreadCount * 5), 'E', idealThreadCount * 5,
                                [](const int &count, String &err) { return validate<int>(count, "preprocess-count", err); });
    Config::registerOption<String>("server",
                                   String::format<128>("Server to connect to. (defaults to port %d if hostname doesn't contain a port)", plast::DefaultServerPort), 's');
    Config::registerOption<int>("port", String::format<129>("Use this port, (default %d)", plast::DefaultDaemonPort),'p', plast::DefaultDaemonPort,
                                [](const int &count, String &err) { return validate<uint16_t>(count, "port", err); });
    Config::registerOption<String>("socket",
                                   String::format<128>("Run daemon with this domain socket. (default %s)", socketPath.constData()),
                                   'n', socketPath);

    Config::parse(argc, argv, List<Path>() << (Path::home() + ".config/plastd.rc") << "/etc/plastd.rc");
    if (Config::isEnabled("help")) {
        Config::showHelp(stdout);
        return 1;
    }

    const char *logFile = 0;
    int logLevel = 0;
    unsigned int logFlags = 0;
    Path logPath;
    if (!initLogging(argv[0], LogStderr, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s 0x%0x\n",
                logLevel, logFile ? logFile : "", logFlags);
        return 1;
    }

    Daemon::Options options = {
        Config::value<int>("job-count"),
        Config::value<int>("preprocess-count"),
        plast::DefaultServerHost, plast::DefaultServerPort,
        static_cast<uint16_t>(Config::value<int>("port")),
        Config::value<String>("socket")
    };
    const String serverValue = Config::value<String>("server");
    if (!serverValue.isEmpty()) {
        const int colon = serverValue.indexOf(':');

        if (colon != -1) {
            options.serverHost = serverValue.left(colon);
            const int64_t port = serverValue.mid(colon + 1).toLongLong();
            if (port <= 0 || port > std::numeric_limits<uint16_t>::max()) {
                fprintf(stderr, "Invalid argument to -s %s\n", optarg);
                return 1;
            }
            options.serverPort = static_cast<uint16_t>(port);
        } else {
            options.serverHost = serverValue;
        }
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Daemon::SharedPtr daemon = std::make_shared<Daemon>(options);
    daemon->init();

    loop->exec();

    return 0;
}

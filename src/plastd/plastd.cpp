#include "Daemon.h"
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Config.h>
#include <rct/ThreadPool.h>
#include <stdio.h>
#include <signal.h>

static void sigSegvHandler(int signal)
{
    unlink(Config::value<String>("socket").constData());
    fprintf(stderr, "Caught signal %d\n", signal);
    const String trace = Rct::backtrace();
    fprintf(stderr, "%s\n", trace.constData());
    fflush(stderr);
    _exit(1);
}

template<typename T, int min = 0>
inline bool validate(int64_t c, const char* name, String& err)
{
    if (c < min) {
        err = String::format<128>("Invalid %s. Must be >= %d", name, min);
        return false;
    } else if (c > std::numeric_limits<T>::max()) {
        err = String::format<128>("Invalid %s. Must be <= %d", name, std::numeric_limits<T>::max());
        return false;
    }
    return true;
}

static inline void ensurePath(const Path& p)
{
    p.mkdir();
}

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);

    ensurePath(Path::home() + ".config");

    const int idealThreadCount = ThreadPool::idealThreadCount();
    const String socketPath = plast::defaultSocketFile();

    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<bool>("no-sighandler", "Don't use a signal handler", 'S');
    Config::registerOption<int>("job-count", String::format<128>("Job count (defaults to %d)", idealThreadCount), 'j', idealThreadCount,
                                [](const int &count, String &err) { return validate<int>(count, "job-count", err); });
    Config::registerOption<int>("preprocess-count", String::format<128>("Preprocess count (defaults to %d)", idealThreadCount * 5), 'E', idealThreadCount * 5,
                                [](const int &count, String &err) { return validate<int>(count, "preprocess-count", err); });
    Config::registerOption<String>("server",
                                   String::format<128>("Server to connect to. (defaults to port %d if hostname doesn't contain a port)", plast::DefaultServerPort), 's');
    Config::registerOption<int>("port", String::format<128>("Use this port, (default %d)", plast::DefaultDaemonPort), 'p', plast::DefaultDaemonPort,
                                [](const int &count, String &err) { return validate<uint16_t>(count, "port", err); });
    Config::registerOption<String>("socket",
                                   String::format<128>("Run daemon with this domain socket. (default %s)", socketPath.constData()),
                                   'n', socketPath);
    Config::registerOption<int>("reschedule-timeout", String::format<128>("Reschedule job timeout (defaults to %d)", plast::DefaultRescheduleTimeout),
                                't', plast::DefaultRescheduleTimeout,
                                [](const int &count, String &err) { return validate<int>(count, "reschedule-timeout", err); });
    Config::registerOption<int>("reschedule-check", String::format<128>("How often to check for reschedule (defaults to %d)", plast::DefaultRescheduleCheck),
                                'c', plast::DefaultRescheduleCheck,
                                [](const int &count, String &err) { return validate<int, 500>(count, "reschedule-check", err); });
    Config::registerOption<int>("overcommit", String::format<128>("How many local jobs to overcommit (defaults to %d)", plast::DefaultOvercommit),
                                'o', plast::DefaultOvercommit,
                                [](const int &count, String &err) { return validate<int>(count, "overcommit", err); });

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

    if (!Config::isEnabled("no-sighandler")) {
        signal(SIGSEGV, sigSegvHandler);
        signal(SIGILL, sigSegvHandler);
        signal(SIGABRT, sigSegvHandler);
    }

    Daemon::Options options = {
        Config::value<int>("job-count"),
        Config::value<int>("preprocess-count"),
        plast::DefaultServerHost, plast::DefaultServerPort,
        static_cast<uint16_t>(Config::value<int>("port")),
        Config::value<String>("socket"),
        Config::value<int>("reschedule-timeout"),
        Config::value<int>("reschedule-check"),
        Config::value<int>("overcommit")
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
    unsigned int flags = EventLoop::MainEventLoop;
    if (!Config::isEnabled("no-sighandler"))
        flags |= EventLoop::EnableSigIntHandler;
    loop->init(flags);

    Daemon::SharedPtr daemon = std::make_shared<Daemon>(options);
    if (!daemon->init())
        return 1;

    loop->exec();

    return daemon->exitCode();
}

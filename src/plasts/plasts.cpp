#include "Scheduler.h"
#include <Plast.h>
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Config.h>
#include <rct/Rct.h>
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

static inline void ensurePath(const Path& p)
{
    p.mkdir();
}

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);

    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<bool>("syslog", "Log to syslog", 'y');
    Config::registerOption<String>("compilers", "Load compilers from this file "
                                   "(default " PLAST_DATA_PREFIX "/var/compilers.json)", 'c',
                                   PLAST_DATA_PREFIX "/var/compilers.json");
    Config::registerOption<int>("port", String::format<129>("Use this port, (default %d)", plast::DefaultServerPort),'p', plast::DefaultServerPort,
                                [](const int &count, String &err) { return validate<uint16_t>(count, "port", err); });

    if (!Config::parse(argc, argv, List<Path>() << (Path::home() + ".config/plast/plasts.conf") << (PLAST_DATA_PREFIX "/etc/plast/plasts.conf"))) {
        return 1;
    }

    const Flags<LogMode> logMode = Config::isEnabled("syslog") ? LogSyslog : LogStderr;
    const char *logFile = 0;
    Flags<LogFileFlag> logFlags;
    Path logPath;
    LogLevel logLevel = LogLevel::Error;
    if (!initLogging(argv[0], logMode, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s %s\n",
                logLevel.toInt(), logFile ? logFile : "", logFlags.toString().constData());
        return 1;
    }

    ensurePath(Path::home() + ".config/plast/");

    if (Config::isEnabled("help")) {
        Config::showHelp(stdout);
        return 2;
    }
    Scheduler::Options opts = {
        static_cast<uint16_t>(Config::value<int>("port")),
        Config::value<String>("compilers")
    };

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Scheduler::SharedPtr scheduler = std::make_shared<Scheduler>(opts);
    scheduler->init();

    loop->exec();

    return 0;
}

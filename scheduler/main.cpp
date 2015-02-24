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

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);

    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<int>("port", String::format<129>("Use this port, (default %d)", plast::DefaultServerPort),'p', plast::DefaultServerPort,
                                [](const int &count, String &err) { return validate<uint16_t>(count, "port", err); });

    const char *logFile = 0;
    int logLevel = 0;
    unsigned int logFlags = 0;
    Path logPath;
    if (!initLogging(argv[0], LogStderr, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s 0x%0x\n",
                logLevel, logFile ? logFile : "", logFlags);
        return 1;
    }

    Config::parse(argc, argv, List<Path>() << (Path::home() + ".config/plasts.rc") << "/etc/plasts.rc");
    if (Config::isEnabled("help")) {
        Config::showHelp(stdout);
        return 1;
    }
    Scheduler::Options opts = {
        static_cast<uint16_t>(Config::value<int>("port"))
    };

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Scheduler::SharedPtr scheduler = std::make_shared<Scheduler>(opts);
    scheduler->init();

    loop->exec();

    return 0;
}

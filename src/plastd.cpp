/* This file is part of Plast.

   Plast is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Plast is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Plast.  If not, see <http://www.gnu.org/licenses/>. */

#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/ThreadPool.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <Server.h>
#include <rct/Rct.h>
#include <rct/Config.h>
#include "Daemon.h"
#include "Plast.h"

Path socketFile;
static void sigSegvHandler(int signal)
{
    Console::cleanup();
    Path::rm(socketFile);
    fprintf(stderr, "Caught signal %d\n", signal);
    // this is not really allowed in signal handlers but will mostly work
    const String trace = Rct::backtrace();
    fprintf(stderr, "%s\n", trace.constData());
    fflush(stderr);
    _exit(1);
}

static inline bool validate(int c, const char *name, String &err)
{
    if (c < 0) {
        err = String::format<128>("Invalid %s. Must be >= 0", name);
        return false;
    }
    return true;
}
int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);
    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<String>("log-file", "Log to this file", 'l');
    Config::registerOption<bool>("verbose", "Be more verbose", 'v');
    Config::registerOption<bool>("silent", "Be silent", 'S');
    Config::registerOption<bool>("no-local-jobs", "Don't run any local jobs. Only useful for debugging", 'L');
    Config::registerOption<String>("cache-dir", "Where to put compiler cache. Default ~/.plastd/cache", 'c', Path::home() + ".plastd/cache",
                                   [](const String &dir, String &err) {
                                       if (dir.isEmpty()) {
                                           err = "cache-dir can't be empty";
                                           return false;
                                       }
                                       if (!Path::mkdir(dir, Path::Recursive)) {
                                           err = "Can't create directory: " + dir;
                                           return false;
                                       }
                                       return true;
                                   });
    const int idealThreadCount = ThreadPool::idealThreadCount();
    Config::registerOption<int>("job-count", String::format<128>("Job count (defaults to %d", idealThreadCount), 'j', idealThreadCount,
                                [](const int &count, String &err) { return validate(count, "job-count", err); });
    Config::registerOption<int>("preprocess-count", String::format<128>("Preprocess count (defaults to %d", idealThreadCount * 5), 'E', idealThreadCount * 5,
                                [](const int &count, String &err) { return validate(count, "preprocess-count", err); });
    Config::registerOption<String>("server",
                                   String::format<128>("Server to connect to. (defaults to port %d if hostname doesn't contain a port)", Plast::DefaultServerPort), 's');
    Config::registerOption<int>("port", String::format<129>("Use this port, (default %d)", Plast::DefaultDaemonPort),'p', Plast::DefaultDaemonPort,
                                [](const int &count, String &err) { return validate(count, "port", err); });
    Config::registerOption<int>("discovery-port", String::format<128>("Use this port for server discovery (default %d)", Plast::DefaultDiscoveryPort),
                                'P', Plast::DefaultDiscoveryPort,
                                [](const int &count, String &err) { return validate(count, "discovery-port", err); });
    const String socketPath = Plast::defaultSocketFile();
    Config::registerOption<String>("socket",
                                   String::format<128>("Run daemon with this domain socket. (default %s)", socketPath.constData()),
                                   'n', socketPath);

    Config::parse(argc, argv, List<Path>() << (Path::home() + ".config/plastd.rc") << "/etc/plastd.rc");
    if (Config::isEnabled("help")) {
        Config::showHelp(stdout);
        return 1;
    }

    Plast::init();

    Daemon::Options options = {
        Config::value<String>("socket"),
        Plast::DefaultServerPort,
        static_cast<uint16_t>(Config::value<int>("port")),
        static_cast<uint16_t>(Config::value<int>("discovery-port")),
        String(),
        Config::value<String>("cache-dir"),
        Config::value<int>("job-count"),
        Config::value<int>("preprocess-count"),
        Config::isEnabled("no-local-jobs") ? Daemon::Options::NoLocalJobs : Daemon::Options::None
    };

    const String serverValue = Config::value<String>("server");
    if (!serverValue.isEmpty()) {
        const int colon = serverValue.indexOf(':');

        if (colon != -1) {
            options.serverHost = serverValue.left(colon);
            options.serverPort = serverValue.mid(colon + 1).toLongLong();
            if (options.serverPort <= 0) {
                fprintf(stderr, "Invalid argument to -s %s\n", optarg);
                return 1;
            }
        } else {
            options.serverHost = serverValue;
        }
    }

    signal(SIGSEGV, sigSegvHandler);

    const int logLevel = Config::isEnabled("silent") ? -1 : Config::isEnabled("verbose");
    if (!initLogging(argv[0], LogStderr, logLevel, Config::value<String>("log-file"), 0)) {
        fprintf(stderr, "Can't initialize logging with %d %s\n",
                logLevel, Config::value<String>("log-file").constData());
        return 1;
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Daemon daemon;
    if (!daemon.init(options))
        return 1;

    loop->exec();
    Console::cleanup();
    cleanupLogging();
    return 0;
}

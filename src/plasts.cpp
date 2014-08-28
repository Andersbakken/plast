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

#include "Server.h"
#include "Plast.h"
#include <getopt.h>
#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Config.h>
#include <rct/Rct.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void sigSegvHandler(int signal)
{
    Console::cleanup();
    fprintf(stderr, "Caught signal %d\n", signal);
    // this is not really allowed in signal handlers but will mostly work
    const String trace = Rct::backtrace();
    fprintf(stderr, "%s\n", trace.constData());
    fflush(stderr);
    _exit(1);
}

int main(int argc, char** argv)
{
    Plast::init();
    Rct::findExecutablePath(*argv);

    Config::registerOption<bool>("help", "Display this page", 'h');
    Config::registerOption<String>("log-file", "Log to this file", 'l');
    Config::registerOption<bool>("verbose", "Be more verbose", 'v');
    Config::registerOption<bool>("silent", "Be silent", 'S');
    Config::registerOption<int>("port", String::format<129>("Use this port, (default %d)", Plast::DefaultServerPort),'p', Plast::DefaultDaemonPort,
                                [](const int &count, String &err) {
                                    if (count <= 0) {
                                        err = "Invalid port. Must be > 0";
                                        return false;
                                    }
                                    return true;
                                });
    Config::parse(argc, argv, List<Path>() << (Path::home() + ".config/plasts.rc") << "/etc/plasts.rc");
    if (Config::isEnabled("help")) {
        Config::showHelp(stdout);
        return 1;
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

    Server server;
    if (!server.init(Config::value<int>("port")))
        return 1;

    loop->exec();
    Console::cleanup();
    cleanupLogging();
    return 0;
}

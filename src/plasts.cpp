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
#include <rct/Rct.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void sigSegvHandler(int signal)
{
    fprintf(stderr, "Caught signal %d\n", signal);
    // this is not really allowed in signal handlers but will mostly work
    const String trace = Rct::backtrace();
    fprintf(stderr, "%s\n", trace.constData());
    fflush(stderr);
    _exit(1);
}

static void usage(FILE *f)
{
    fprintf(f,
            "plasts [...options...]\n"
            "  --help|-h                                  Display this page.\n"
            "  --port|-p [port]                           Run server on this port. (default 5160)\n");
}

int main(int argc, char** argv)
{
    Plast::init();
    Rct::findExecutablePath(*argv);

    struct option opts[] = {
        { "help", no_argument, 0, 'h' },
        { "log-file", required_argument, 0, 'l' },
        { "verbose", no_argument, 0, 'v' },
        { "port", required_argument, 0, 'p' },
        { 0, 0, 0, 0 }
    };
    const String shortOptions = Rct::shortOptions(opts);

    int port = 5160;
    const char *logFile = 0;
    int logLevel = 0;

    while (true) {
        const int c = getopt_long(argc, argv, shortOptions.constData(), opts, 0);
        if (c == -1)
            break;
        switch (c) {
        case 'p':
            port = atoi(optarg);
            if (port <= 0) {
                fprintf(stderr, "Invalid argument to -p %s\n", optarg);
                return 1;
            }
            break;
        case 'h':
            usage(stdout);
            return 0;
        case 'L':
            logFile = optarg;
            break;
        case 'v':
            if (logLevel >= 0)
                ++logLevel;
            break;
        case '?': {
            usage(stderr);
            return 1; }
        }
    }
    if (optind < argc) {
        fprintf(stderr, "plasts: unexpected option -- '%s'\n", argv[optind]);
        return 1;
    }

    signal(SIGSEGV, sigSegvHandler);

    if (!initLogging(argv[0], LogStderr, logLevel, logFile, 0)) {
        fprintf(stderr, "Can't initialize logging with %d %s 0x%0x\n",
                logLevel, logFile ? logFile : "", 0);
        return 1;
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Server server;
    if (!server.init(port))
        return 1;

    loop->exec();
    cleanupLogging();
    return 0;
}

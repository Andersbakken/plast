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
#include "Daemon.h"
#include "Plast.h"

Path socketFile;
static void sigSegvHandler(int signal)
{
    Path::rm(socketFile);
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
            "plastd [...options...]\n"
            "  --help|-h                                  Display this page.\n"
            "  --log-file|-l [file]                       Log to this file.\n"
            "  --verbose|-v                               Be more verbose.\n"
            "  --job-count|-j [count]                     Job count (defaults to number of cores)\n"
            "  --server|-s [hostname:(port)]              Server to connect to. (defaults to port 5160 if hostname doesn't contain a port)\n"
            "  --port|-p [port]                           Use this port (default 5161)\n"
            "  --discovery-port|-P [port]                 Use this port for server discovery (default 5163)\n"
            "  --socket|-n [file]                         Run daemon with this domain socket. (default ~/.plastd.sock)\n");
}

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);

    Plast::init();

    struct option opts[] = {
        { "help", no_argument, 0, 'h' },
        { "log-file", required_argument, 0, 'l' },
        { "verbose", no_argument, 0, 'v' },
        { "port", required_argument, 0, 'p' },
        { "discovery-port", required_argument, 0, 'P' },
        { "socket", required_argument, 0, 'n' },
        { "server", required_argument, 0, 's' },
        { "job-count", required_argument, 0, 'j' },
        { 0, 0, 0, 0 }
    };
    const String shortOptions = Rct::shortOptions(opts);

    Daemon::Options options = {
        Path::home() + "/.plastd.sock",
        5160,
        5161,
        5162,
        String(),
        ThreadPool::idealThreadCount()
    };
    const char *logFile = 0;
    int logLevel = 0;

    while (true) {
        const int c = getopt_long(argc, argv, shortOptions.constData(), opts, 0);
        if (c == -1)
            break;
        switch (c) {
        case 'n':
            socketFile = optarg;
            break;
        case 's': {
            const char *colon = strchr(optarg, ':');
            if (colon) {
                options.serverHost.assign(optarg, colon - optarg);
                options.serverPort = atoi(colon + 1);
                if (options.serverPort <= 0) {
                    fprintf(stderr, "Invalid argument to -s %s\n", optarg);
                    return 1;
                }
            } else {
                options.serverHost = optarg;
            }
            break; }
        case 'p':
            options.port = atoi(optarg);
            if (options.port <= 0) {
                fprintf(stderr, "Invalid argument to -p %s\n", optarg);
                return 1;
            }
            break;
        case 'j':
            options.jobCount = atoi(optarg);
            if (options.jobCount < 0) {
                fprintf(stderr, "Invalid argument to -j %s\n", optarg);
                return 1;
            }
            break;
        case 'P':
            options.discoveryPort = atoi(optarg);
            if (options.discoveryPort <= 0) {
                fprintf(stderr, "Invalid argument to -P %s\n", optarg);
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
        fprintf(stderr, "plastd: unexpected option -- '%s'\n", argv[optind]);
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

    Daemon daemon;
    if (!daemon.init(options))
        return 1;

    loop->exec();
    cleanupLogging();
    return 0;
}

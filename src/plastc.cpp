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

#include "Plast.h"
#include <rct/EventLoop.h>
#include <rct/Connection.h>

int buildLocal(int argc, char **argv)
{
    const String fileName = Path(argv[0]).fileName();
    const List<String> paths = String(getenv("PATH")).split(':');
    error() << fileName;
    for (const auto &p : paths) {
        Path exec = p + "/" + fileName;
        error() << "Trying" << exec;
        if (exec.resolve() && exec != Rct::executablePath()) {
            printf("[%s:%d]: if (exec.resolve() && exec != Rct::executablePath()) {\n", __FILE__, __LINE__); fflush(stdout);
            execv(exec.constData(), &argv[1]);
            fprintf(stderr, "execv error %d/%s\n", errno, strerror(errno));
            return 1;
        }
    }
    fprintf(stderr, "Failed to find compiler for %s/%s\n", fileName.constData(), argv[0]);
    return 2;
}

static inline unsigned long conf(const char *env, unsigned long defaultValue)
{
    if (const char *val = getenv(env)) {
        char *end;
        const unsigned int ret = strtoul(val, &end, 10);
        if (*end) {
            fprintf(stderr, "Invalid value \"%s\"=\"%s\"\n", env, val);
        } else {
            return ret;
        }
    }
    return defaultValue;
}

int main(int argc, char** argv)
{
    Rct::findExecutablePath(*argv);
    printf("[%s]\n", argv[0]);
    return 0;
    initLogging(argv[0], LogStderr, Error, 0, 0);

    Plast::init();
    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Path socket = getenv("PLAST_SOCKET_FILE");
    if (socket.isEmpty())
        socket = Path::home() + "/.plastd.sock";

    const unsigned long connectTimeout = conf("PLAST_DAEMON_CONNECT_TIMEOUT", 2000);
    const unsigned long jobTimeout = conf("PLAST_JOB_TIMEOUT", 10000);

    int returnValue = -1;
    Connection connection;
    connection.newMessage().connect([returnValue](Message *message, Connection *conn) {


        });

    connection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    connection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    if (!connection.connectUnix(socket, connectTimeout)) {
        return buildLocal(argc, argv);
    }
    if (loop->exec(jobTimeout) == EventLoop::Success) {
        return returnValue;
    } else {
        return buildLocal(argc, argv);
    }
}

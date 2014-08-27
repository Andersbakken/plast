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
    const Path path = Plast::resolveCompiler(argv[0]);
    if (!path.isEmpty()) {
        argv[0] = strdup(path.constData());
        execv(path.constData(), argv); // execute without resolving symlink
        fprintf(stderr, "execv error for %s %d/%s\n", path.constData(), errno, strerror(errno));
        return 1;
    }
    fprintf(stderr, "Failed to find compiler for '%s'\n", argv[0]);
    return 2;
}

static inline int conf(const char *env, int defaultValue)
{
    if (const char *val = getenv(env)) {
        char *end;
        const int ret = strtoul(val, &end, 10);
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

    // for (int i=0; i<argc + 1; ++i) {
    //     printf("%d/%d %s\n", i, argc, argv[i]);
    // }
    // return 0;

    // printf("[%s]\n", argv[0]);
    // return 0;
    initLogging(argv[0], LogStderr, Error, 0, 0);

    Plast::init();
    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    Path socket = getenv("PLAST_SOCKET_FILE");
    if (socket.isEmpty())
        socket = Path::home() + "/.plastd.sock";

    const unsigned long connectTimeout = conf("PLAST_DAEMON_CONNECT_TIMEOUT", 2000);
    const unsigned long jobTimeout = conf("PLAST_JOB_TIMEOUT", -1);

    int returnValue = -1;
    Connection connection;
    connection.newMessage().connect([&returnValue, loop](Message *message, Connection *conn) {
            switch (message->messageId()) {
            case LocalJobResponseMessage::MessageId: {
                LocalJobResponseMessage *msg = static_cast<LocalJobResponseMessage*>(message);
                for (const auto &output : msg->output()) {
                    switch (output.type) {
                    case Output::StdOut:
                        fwrite(output.text.constData(), 1, output.text.size(), stdout);
                        break;
                    case Output::StdErr:
                        fwrite(output.text.constData(), 1, output.text.size(), stderr);
                        break;
                    }
                }
                returnValue = msg->status();
                loop->quit();
                break; }
            default:
                error() << "Unexpected message" << message->messageId();
                break;
            }

        });

    connection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    connection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    if (!connection.connectUnix(socket, connectTimeout)) {
        return buildLocal(argc, argv);
    }

    List<String> env;
    for (char **e = environ; *e; ++e)
        env.append(*e);
    connection.send(LocalJobMessage(argc, argv, env, Path::pwd()));
    loop->exec(jobTimeout);
    if (returnValue == -1)
        return buildLocal(argc, argv);
    return returnValue;
}

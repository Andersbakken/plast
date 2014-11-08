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
#include <rct/Connection.h>
#include "ClientJobResponseMessage.h"
#include "ClientJobMessage.h"
#include "Plast.h"
#include "CompilerArgs.h"
#include "Compiler.h"

static inline int buildLocal(const Path &path, int argc, char **argv, const char *reason)
{
    List<String> foo;
    for (int i=0; i<argc; ++i) {
        foo << argv[i];
    }

    error() << "Building local" << reason << String::join(foo, ' ');
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

static inline bool checkFlags(unsigned int flags)
{
    if (flags & (CompilerArgs::StdinInput|CompilerArgs::MultiSource)) {
        return false;
    }
    switch (flags & CompilerArgs::LanguageMask) {
    case CompilerArgs::C:
    case CompilerArgs::CPlusPlus:
    case CompilerArgs::CPreprocessed:
    case CompilerArgs::CPlusPlusPreprocessed:
        return true;
    default:
        break;
    }
    return false;
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
    initLogging(argv[0], LogStderr, getenv("PLASTC_VERBOSE") ? Debug : Error, 0, 0);

    List<String> commandLine(argc);
    for (int i=0; i<argc; ++i) {
        commandLine[i].assign(argv[i]);
    }

    const Path resolvedCompiler = Compiler::resolve(argv[0]);

    std::shared_ptr<CompilerArgs> args = CompilerArgs::create(commandLine);
    if (!args || args->mode != CompilerArgs::Compile || !checkFlags(args->flags)) {
        return buildLocal(resolvedCompiler, argc, argv, "flags or args or something");
    }

    Plast::init();
    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop);

    Path socket = getenv("PLAST_SOCKET_FILE");
    if (socket.isEmpty())
        socket = Plast::defaultSocketFile();
    debug() << "Using socketfile" << socket;

    const int connectTimeout = conf("PLAST_DAEMON_CONNECT_TIMEOUT", 2000);
    const int jobTimeout = conf("PLAST_JOB_TIMEOUT", -1);

    int returnValue = -1;
    Connection connection;
    connection.newMessage().connect([&returnValue, loop](const std::shared_ptr<Message> &message, Connection *conn) {
            switch (message->messageId()) {
            case ClientJobResponseMessage::MessageId: {
                std::shared_ptr<ClientJobResponseMessage> msg = std::static_pointer_cast<ClientJobResponseMessage>(message);
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
                debug() << "Got ClientJobResponseMessage" << msg->status();
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
        debug() << "Couldn't connect to daemon";
        return buildLocal(resolvedCompiler, argc, argv, "connection failure");
    }

    List<String> env;
    extern char **environ;
    for (char **e = environ; *e; ++e)
        env.append(*e);
    connection.send(ClientJobMessage(args, resolvedCompiler, env, Path::pwd()));
    loop->exec(jobTimeout);
    if (returnValue == -1) {
        return buildLocal(resolvedCompiler, argc, argv, "Compile failure");
    }
    return returnValue;
}

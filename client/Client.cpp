#include "Client.h"
#include <Plast.h>
#include <Messages.h>
#include <rct/Log.h>
#include <stdio.h>

Client::Client()
{
    messages::init();
}

Client::~Client()
{
}

bool Client::run(int argc, char** argv)
{
    mConnection.newMessage().connect([](std::shared_ptr<Message> message, Connection*) {
            if (message->messageId() == ResponseMessage::MessageId) {
                std::shared_ptr<ResponseMessage> resp = std::static_pointer_cast<ResponseMessage>(message);
                const String response = resp->data();
                if (!response.isEmpty()) {
                    FILE* f = (resp->type() == ResponseMessage::Stdout ? stdout : stderr);
                    fprintf(f, "%s\n", response.constData());
                    fflush(f);
                }
            } else {
                error("Unexpected message Client: %d", message->messageId());
            }
        });
    mConnection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    mConnection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
    if (!mConnection.connectUnix(plast::defaultSocketFile())) {
        Path path = plast::resolveCompiler(argv[0]);
        // error() << "Building local" << reason << String::join(args, ' ');
        if (!path.isEmpty()) {
            argv[0] = path.data();
            execv(path.constData(), argv); // execute without resolving symlink
            fprintf(stderr, "execv error for %s %d/%s\n", path.constData(), errno, strerror(errno));
            return false;
        }
        fprintf(stderr, "Failed to find compiler for '%s'\n", argv[0]);
        return false;
    }

    List<String> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    mConnection.send(JobMessage(Path::pwd(), args));
    return true;
}

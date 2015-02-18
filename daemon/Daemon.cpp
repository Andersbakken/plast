#include "Daemon.h"
#include "Job.h"
#include <rct/Log.h>

Daemon::WeakPtr Daemon::sInstance;

Daemon::Daemon()
{
    auto conn = [this](SocketServer* server) {
        SocketClient::SharedPtr client;
        for (;;) {
            client = server->nextConnection();
            if (!client)
                return;
            addClient(client);
        }
    };
    mTcpServer.newConnection().connect(conn);
    if (!mTcpServer.listen(13119)) {
        error() << "Unable to tcp listen";
        abort();
    }
    mUnixServer.newConnection().connect(conn);
    if (!mUnixServer.listen(Path::home().ensureTrailingSlash() + ".plast.sock")) {
        error() << "Unable to unix listen";
        abort();
    }
}

Daemon::~Daemon()
{
}

void Daemon::handleJobMessage(const JobMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handling job message";
    Job::SharedPtr job = Job::create(msg->path(), msg->args());
    job->statusChanged().connect([conn](Job* job, Job::Status status) {
            error() << "job status changed" << job << status;
            switch (status) {
            case Job::Complete:
                conn->finish();
                break;
            case Job::Error:
                conn->finish(-1);
                break;
            default:
                break;
            }
        });
    job->readyReadStdOut().connect([conn](Job* job) {
            error() << "job ready stdout";
            conn->write(job->readAllStdOut());
        });
    job->readyReadStdErr().connect([conn](Job* job) {
            const String err = job->readAllStdErr();
            error() << "job ready stderr" << err;
            conn->write(err, ResponseMessage::Stderr);
        });
    job->start();
}

void Daemon::handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle job message!";
}

void Daemon::addClient(const SocketClient::SharedPtr& client)
{
    error() << "client added";
    Connection* conn = new Connection(client);
    conn->newMessage().connect([this](const std::shared_ptr<Message>& msg, Connection* conn) {
            switch (msg->messageId()) {
            case JobMessage::MessageId:
                handleJobMessage(std::static_pointer_cast<JobMessage>(msg), conn);
                break;
            case HasJobsMessage::MessageId:
                handleHasJobsMessage(std::static_pointer_cast<HasJobsMessage>(msg), conn);
                break;
            default:
                error() << "Unexpected message" << msg->messageId();
                conn->finish(1);
                break;
            }
        });
    conn->disconnected().connect([](Connection* conn) {
            conn->disconnected().disconnect();
            EventLoop::deleteLater(conn);
        });
}

void Daemon::init()
{
    sInstance = shared_from_this();
    messages::init();
}

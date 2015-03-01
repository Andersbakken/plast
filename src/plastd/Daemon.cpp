#include "Daemon.h"
#include "Job.h"
#include "CompilerVersion.h"
#include <rct/Log.h>
#include <rct/QuitMessage.h>

Daemon::WeakPtr Daemon::sInstance;

Daemon::Daemon(const Options& opts)
    : mOptions(opts), mExitCode(0)
{
    const Path cmp = Path::home() + ".config/plasts.compilers";
    mCompilers << cmp.readAll().split('\n', String::SkipEmpty);
    if (mCompilers.isEmpty()) {
        mCompilers << "/usr/bin/cc";
        mCompilers << "/usr/bin/c++";
    }
    for (const Path& c : mCompilers) {
        CompilerVersion::init(c.resolved());
    }
}

bool Daemon::init()
{
    mServer.newConnection().connect([this](SocketServer* server) {
            SocketClient::SharedPtr client;
            for (;;) {
                client = server->nextConnection();
                if (!client)
                    return;
                addClient(client);
            }
        });

    bool ok = false;
    for (int i=0; i<10; ++i) {
        warning() << "listening" << mOptions.localUnixPath;
        if (mServer.listen(mOptions.localUnixPath)) {
            ok = true;
            break;
        }
        if (!i) {
            enum { Timeout = 1000 };
            Connection connection;
            if (connection.connectUnix(mOptions.localUnixPath, Timeout)) {
                connection.send(QuitMessage());
                connection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
                connection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
                EventLoop::eventLoop()->exec(Timeout);
            }
            Path::rm(mOptions.localUnixPath);
        } else {
            sleep(1);
        }
    }

    if (!ok) {
        error() << "Unable to unix listen" << mOptions.localUnixPath;
        return false;
    }

    sInstance = shared_from_this();
    messages::init();
    mLocal.init();
    mRemote.init();

    mHostName.resize(sysconf(_SC_HOST_NAME_MAX));
    if (gethostname(mHostName.data(), mHostName.size()) == 0) {
        mHostName.resize(strlen(mHostName.constData()));
    } else {
        mHostName.clear();
    }

    return true;
}

Daemon::~Daemon()
{
}

void Daemon::handleJobMessage(const JobMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handling job message";

    Job::SharedPtr job = Job::create(msg->path(), msg->args(), Job::LocalJob, mHostName);
    Job::WeakPtr weak = job;
    conn->disconnected().connect([weak](Connection *) {
            if (Job::SharedPtr job = weak.lock())
                job->abort();
        });
    job->statusChanged().connect([conn](Job* job, Job::Status status) {
            error() << "job status changed" << job << status;
            switch (status) {
            case Job::Compiled:
                conn->finish();
                break;
            case Job::Error:
                conn->write(job->error(), ResponseMessage::Stderr);
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

void Daemon::addClient(const SocketClient::SharedPtr& client)
{
    error() << "local client added";
    Connection* conn = new Connection(client);
    conn->newMessage().connect([this](const std::shared_ptr<Message>& msg, Connection* conn) {
            switch (msg->messageId()) {
            case QuitMessage::MessageId:
                mExitCode = std::static_pointer_cast<QuitMessage>(msg)->exitCode();
                EventLoop::eventLoop()->quit();
                break;
            case JobMessage::MessageId:
                handleJobMessage(std::static_pointer_cast<JobMessage>(msg), conn);
                break;
            default:
                error() << "Unexpected message Daemon" << msg->messageId();
                conn->finish(1);
                break;
            }
        });
    conn->disconnected().connect([](Connection* conn) {
            conn->disconnected().disconnect();
            EventLoop::deleteLater(conn);
        });
}

#include "Daemon.h"
#include "Job.h"
#include "CompilerVersion.h"
#include "CompilerArgs.h"
#include <rct/Log.h>
#include <rct/QuitMessage.h>
#include <rct/Rct.h>

Daemon::WeakPtr Daemon::sInstance;

Daemon::Daemon(const Options& opts)
    : mLocal(opts.overcommit), mOptions(opts), mExitCode(0)
{
    const Path cmp = Path::home() + ".config/plastd.compilers";
    auto cmplist = cmp.readAll().split('\n', String::SkipEmpty);
    if (cmplist.isEmpty()) {
        cmplist << "/usr/bin/cc";
        cmplist << "/usr/bin/c++";
    }
    for (const String& c : cmplist) {
        auto entry = c.split(' '); // assume compilers don't have spaces in the file names
        const Path r = static_cast<Path>(entry[0]).resolved();
        const String target = (entry.size() > 1) ? entry[1] : String();
        CompilerVersion::init(r, 0, target);
        if (Rct::is64Bit)
            CompilerVersion::init(r, CompilerArgs::HasDashM32, target);
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

#warning should this really be a weak_ptr?
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

void Daemon::handleJobMessage(const JobMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    error() << "handling job message";

    Job::SharedPtr job = Job::create(msg->path(), msg->args(), Job::LocalJob, mHostName);
    Job::WeakPtr weak = job;
    conn->disconnected().connect([weak](Connection *) {
            if (Job::SharedPtr job = weak.lock())
                job->abort();
        });
    job->statusChanged().connect([conn](Job* job, Job::Status status) {
            assert(job->type() == Job::LocalJob);
            error() << "job status changed" << job << "local" << job->id() << status;
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
    static Hash<Connection*, std::shared_ptr<Connection> > conns;
    std::shared_ptr<Connection> conn = std::make_shared<Connection>(client);
    std::weak_ptr<Connection> weak = conn;
    conns[conn.get()] = conn;
    conn->newMessage().connect([this, weak](const std::shared_ptr<Message>& msg, Connection*) {
            std::shared_ptr<Connection> conn = weak.lock();
            if (!conn)
                return;
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
    conn->disconnected().connect([](Connection* ptr) {
            error() << "conn dis 1";
            auto found = conns.find(ptr);
            assert(found != conns.end());
            std::shared_ptr<Connection> conn = found->second;
            conn->disconnected().disconnect();
            conns.erase(found);
        });
}

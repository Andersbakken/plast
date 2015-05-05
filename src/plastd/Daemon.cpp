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
        CompilerVersion::SharedPtr ver = CompilerVersion::version(r, 0, target);
        if (ver) {
            if (Rct::is64Bit) {
                switch (ver->compiler()) {
                case plast::Clang:
                    CompilerVersion::version(r, CompilerArgs::HasDashM32, target);
                    break;
                case plast::GCC: {
                    // does this gcc support both 64 and 32?
                    const Set<String>& multis = ver->multiLibs();
                    error() << "Multis" << multis;
                    if (multis.contains("m32") && multis.contains("m64")) {
                        // assume we support i686-linux-gnu
                        ver = CompilerVersion::version(r, CompilerArgs::HasDashM32, "i686-linux-gnu");
                        if (ver)
                            ver->setExtraArgs(List<String>() << "-m32");
                    }
                    break; }
                case plast::Unknown:
                    break;
                }
            } else {
                // assume that the only alternative to 64bit is 32bit
                if (ver->compiler() == plast::GCC) {
                    // does this gcc support both 64 and 32?
                    const Set<String>& multis = ver->multiLibs();
                    error() << "Multis 2" << multis;
                    if (multis.contains("m32") && multis.contains("m64")) {
                        // assume we support x86_64-linux-gnu
                        ver = CompilerVersion::version(r, CompilerArgs::HasDashM64, "x86_64-linux-gnu");
                        if (ver)
                            ver->setExtraArgs(List<String>() << "-m64");
                    }
                }
            }
        }
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
            std::shared_ptr<Connection> connection = Connection::create(plast::ConnectionVersion);
            if (connection->connectUnix(mOptions.localUnixPath, Timeout)) {
                connection->send(QuitMessage());
                connection->disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
                connection->finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
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
    std::weak_ptr<Connection> weakConn = conn;
    conn->disconnected().connect([weak](const std::shared_ptr<Connection> &) {
            if (Job::SharedPtr job = weak.lock())
                job->abort();
        });
    job->statusChanged().connect([weakConn](Job* job, Job::Status status, Job::Status) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
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
    job->readyReadStdOut().connect([weakConn](Job* job) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
            error() << "job ready stdout";
            conn->write(job->readAllStdOut());
        });
    job->readyReadStdErr().connect([weakConn](Job* job) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
            const String err = job->readAllStdErr();
            error() << "job ready stderr" << err;
            conn->write(err, ResponseMessage::Stderr);
        });
    job->start();
}

void Daemon::addClient(const SocketClient::SharedPtr& client)
{
    error() << "local client added";
    static Set<std::shared_ptr<Connection> > conns;
    std::shared_ptr<Connection> conn = Connection::create(client, plast::ConnectionVersion);
    conns.insert(conn);
    conn->newMessage().connect([this](const std::shared_ptr<Message>& msg, const std::shared_ptr<Connection> &conn) {
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
    conn->disconnected().connect([](const std::shared_ptr<Connection> &conn) {
            conn->disconnected().disconnect();
            conns.remove(conn);
        });
}

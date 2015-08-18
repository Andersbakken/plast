#include "Daemon.h"
#include "Job.h"
#include "CompilerVersion.h"
#include "CompilerArgs.h"
#include <rct/Log.h>
#include <rct/QuitMessage.h>
#include <rct/Rct.h>
#include "Http.h"
#include <stdlib.h>
#include <rct/Config.h>

Daemon::WeakPtr Daemon::sInstance;

Daemon::Daemon(const Options& opts)
    : mLocal(opts.overcommit), mOptions(opts), mExitCode(0)
{
    updateCompilers();
}

void Daemon::updateCompilers()
{
    if (Config::isEnabled("download-compilers")) {
        Http::Request req;
        req.url = "http://lgux-pnavarro3.corp.netflix.com/toolchains/";
        Http::Response response = Http::get(req);
        size_t offset = 0;
        std::cmatch match;
        std::regex rx("<a href=\"([^\"]+LATEST[^\"]+)\"");
        const Path compilerDir = mOptions.cacheDirectory + "compilers/";
        while (std::regex_search(response.contents.constData() + offset, match, rx)) {
            const String m(match[1].str());
            int dot = m.lastIndexOf(".tar", -1, String::CaseInsensitive);
            if (dot == -1)
                dot = m.lastIndexOf(".tgz", -1, String::CaseInsensitive);
            if (dot == -1)
                dot = m.lastIndexOf(".tbz2", -1, String::CaseInsensitive);

            if (dot != -1) {
                Http::Request request;
                request.url = req.url + m;
                char tempBuf[PATH_MAX];
                Path fileOutput = compilerDir + m;
                // request.fileOutput = mOptions.cacheDirectory + "compilers/" + m;
                const Path manifest = fileOutput.left(dot + (fileOutput.size() - m.size()) + 1) + "manifest";
                // error() << url << fileName << manifest;
                bool hadEtag = false;
                for (const auto &line : manifest.readAll().split("\n")) {
                    if (line.startsWith("ETag: ")) {
                        request.headers["If-None-Match"] = line.mid(6);
                        snprintf(tempBuf, sizeof(tempBuf), "%s_plast_compiler_download_XXXXXX", compilerDir.constData());
                        const int fd = mkstemp(tempBuf);
                        if (fd != -1) {
                            request.output = fdopen(fd, "w");
                            assert(request.output);
                            hadEtag = true;
                        }
                        break;
                    }
                }
                if (!request.output) {
                    request.output = fopen(fileOutput.constData(), "w");
                    assert(request.output);
                    if (!request.output)
                        continue;
                }

                const Http::Response response = Http::get(request);

                fclose(request.output);

                if (response.statusCode == 304) {
                    error() << "File was golden" << fileOutput;
                    unlink(tempBuf);
                } else if (response.statusCode >= 200 && response.statusCode < 300) {
                    if (hadEtag) {
                        int r = rename(tempBuf, fileOutput.constData());
                        (void)r;
                        assert(!r);
                    }
                    error() << "Got new file" << request.url << fileOutput << response.responseHeaders.value("ETag");
                    FILE *f = fopen(manifest.constData(), "w");
                    assert(f);
                    fprintf(f, "ETag: %s", response.responseHeaders.value("ETag").constData());
                    fclose(f);
                } else {
                    error() << "Something went wrong for" << request.url << response.error;
                }
            }
            offset += match.position() + match.length();
        }
    }

    auto processCompilerFile = [](const Path &cmp) {
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
    };

    for (const Path &path : (List<Path>() << "/etc/plast/compilers"
                             << Path("/etc/plast/compilers.d/").files(Path::File)
                             << Path::home() + ".config/plastd.compilers")) {
        processCompilerFile(path);
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
                conn->finish(job->exitCode());
                break;
            case Job::Error:
                conn->write(job->error(), ResponseMessage::Stderr);
                conn->finish(job->exitCode());
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

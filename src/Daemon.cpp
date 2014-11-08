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

#include "Daemon.h"
#include <rct/SHA256.h>

Daemon::Daemon()
    : mNextJobId(1), mExplicitServer(false), mServerConnection(std::make_shared<Connection>()),
      mHostName(Rct::hostName())
{
    Console::init("plastd> ",
                  std::bind(&Daemon::handleConsoleCommand, this, std::placeholders::_1),
                  std::bind(&Daemon::handleConsoleCompletion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    mLocalServer.newConnection().connect([this](SocketServer *server) {
            while (true) {
                auto socket = server->nextConnection();
                if (!socket)
                    break;
                std::shared_ptr<Connection> conn = std::make_shared<Connection>(socket);
                mConnections.insert(conn);
                conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
            }
        });
    mRemoteServer.newConnection().connect([this](SocketServer *server) {
            while (true) {
                auto socket = server->nextConnection();
                if (!socket)
                    break;
                std::shared_ptr<Connection> conn = std::make_shared<Connection>(socket);
                mConnections.insert(conn);
                conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
            }
        });
    mServerConnection->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    mServerConnection->disconnected().connect(std::bind(&Daemon::restartServerTimer, this));
    mServerConnection->connected().connect([this](Connection *conn) {
            warning() << "Connected to server:" << conn->client()->peerString() << "Sending handshake";
            sendHandshake(mServerConnection);
        });

    mServerConnection->error().connect(std::bind(&Daemon::restartServerTimer, this));

    mServerTimer.timeout().connect(std::bind(&Daemon::reconnectToServer, this));
    mOutstandingJobRequestsTimer.timeout().connect(std::bind(&Daemon::checkJobRequestTimeout, this));
}

Daemon::~Daemon()
{
    Console::cleanup();
    mPeersByHost.deleteAll();
    mPeersByConnection.clear();
}

bool Daemon::init(const Options &options)
{
    if (!Path::mkdir(options.cacheDir, Path::Recursive)) {
        error() << "Couldn't create directory" << options.cacheDir;
        return false;
    }
    bool success = false;
    for (int i=0; i<10; ++i) {
        if (mLocalServer.listen(options.socketFile)) {
            success = true;
            break;
        }
        if (!i) {
            enum { Timeout = 1000 };
            Connection connection;
            if (connection.connectUnix(options.socketFile, Timeout)) {
                connection.send(QuitMessage());
                connection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
                connection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
                EventLoop::eventLoop()->exec(Timeout);
            }
        } else {
            sleep(1);
        }
        Path::rm(options.socketFile);
    }
    if (!success) {
        error() << "Can't seem to listen on" << options.socketFile;
        return false;
    }

    if (!mRemoteServer.listen(options.port)) {
        error() << "Can't seem to listen on" << options.port;
        return false;
    }
    warning() << "Listening" << options.port;

    if (options.discoveryPort) {
        mDiscoverySocket.reset(new SocketClient);
        mDiscoverySocket->bind(options.discoveryPort);
        mDiscoverySocket->readyReadFrom().connect([this](const SocketClient::SharedPtr &, const String &ip, uint16_t port, Buffer &&data) {
                if (port == mOptions.discoveryPort)
                    onDiscoverySocketReadyRead(std::forward<Buffer>(data), ip);
            });
    }

    mOptions = options;
    mCompilerCache.reset(new CompilerCache(mOptions.cacheDir + "compilers/"));

    const Path dir = mOptions.cacheDir + "output/";
    for (const Path &file : dir.files(Path::Directory)) {
        file.rm();
    }

    mExplicitServer = !mOptions.serverHost.isEmpty();
    reconnectToServer();

    return true;
}

void Daemon::onNewMessage(const std::shared_ptr<Message> &message, Connection *conn)
{
    auto connection = conn->shared_from_this();
    switch (message->messageId()) {
    case Plast::ClientJobMessageId:
        handleClientJobMessage(std::static_pointer_cast<ClientJobMessage>(message), connection);
        break;
    case Plast::QuitMessageId:
        warning() << "Quitting by request";
        EventLoop::eventLoop()->quit();
        break;
    case Plast::JobAnnouncementMessageId:
        handleJobAnnouncementMessage(std::static_pointer_cast<JobAnnouncementMessage>(message), connection);
        break;
    case Plast::CompilerMessageId:
        handleCompilerMessage(std::static_pointer_cast<CompilerMessage>(message), connection);
        break;
    case Plast::CompilerRequestMessageId:
        handleCompilerRequestMessage(std::static_pointer_cast<CompilerRequestMessage>(message), connection);
        break;
    case Plast::JobRequestMessageId:
        handleJobRequestMessage(std::static_pointer_cast<JobRequestMessage>(message), connection);
        break;
    case Plast::JobMessageId:
        handleJobMessage(std::static_pointer_cast<JobMessage>(message), connection);
        break;
    case Plast::JobResponseMessageId:
        handleJobResponseMessage(std::static_pointer_cast<JobResponseMessage>(message), connection);
        break;
    case Plast::JobDiscardedMessageId:
        handleJobDiscardedMessage(std::static_pointer_cast<JobDiscardedMessage>(message), connection);
        break;
    case Plast::DaemonListMessageId:
        handleDaemonListMessage(std::static_pointer_cast<DaemonListMessage>(message), connection);
        break;
    case Plast::HandshakeMessageId:
        handleHandshakeMessage(std::static_pointer_cast<HandshakeMessage>(message), connection);
        break;
    default:
        error() << "Unexpected message" << message->messageId();
        break;
    }
}

void Daemon::handleClientJobMessage(const std::shared_ptr<ClientJobMessage> &msg,
                                    const std::shared_ptr<Connection> &conn)
{
    debug() << "Got local job" << msg->arguments() << msg->cwd();

    List<String> env = msg->environ();
    assert(!env.contains("PLAST=1"));
    env.append("PLAST=1");

    const Path resolvedCompiler = msg->resolvedCompiler();
    std::shared_ptr<Compiler> compiler = mCompilerCache->findByPath(resolvedCompiler);
    if (!compiler) {
        warning() << "I haven't seen this compiler before. Lets see if we can load it" << resolvedCompiler;
        compiler = mCompilerCache->create(resolvedCompiler);
        if (!compiler) {
            warning() << "Can't create compiler for" << resolvedCompiler;
            conn->send(ClientJobResponseMessage());
            conn->close();
            return;
        }
    }
    std::shared_ptr<Job> job = std::make_shared<Job>(msg->arguments(), resolvedCompiler, env, msg->cwd(), compiler, conn);
    error() << "Got local job" << job->arguments->sourceFile();
    mJobsByLocalConnection[conn] = job;
    addJob(Job::PendingPreprocessing, job);
    startJobs();
}

void Daemon::handleCompilerMessage(const std::shared_ptr<CompilerMessage> &message,
                                   const std::shared_ptr<Connection> &connection)
{
    assert(message->isValid());
    if (mCompilerCache->create(message->compiler().fileName(), message->sha256(), message->contents())) {
        fetchJobs(mPeersByConnection.value(connection));
    }
}

void Daemon::handleCompilerRequestMessage(const std::shared_ptr<CompilerRequestMessage> &message,
                                          const std::shared_ptr<Connection> &connection)
{
    std::shared_ptr<Compiler> compiler = mCompilerCache->findBySha256(message->sha256());
    if (compiler) {
        error() << "Sending compiler" << message->sha256();
        const auto contents = mCompilerCache->contentsForSha256(message->sha256());
        if (contents.isEmpty())
            return;
        connection->send(CompilerMessage(compiler->path().fileName(), compiler->sha256(), contents));
    } else {
        error() << "I don't know nothing about no" << message->sha256() << "Fisk" << mCompilerCache->dump();
    }
}

void Daemon::handleJobRequestMessage(const std::shared_ptr<JobRequestMessage> &message,
                                     const std::shared_ptr<Connection> &connection)
{
    for (auto job : mPendingCompileJobs) {
        if (!(job->flags & Job::FromRemote) && job->compiler->sha256() == message->sha256()) {
            debug() << "Sending job to" << connection->client()->peerString() << job->arguments->commandLine << job->arguments->sourceFiles();
            if (connection->send(JobMessage(message->id(), message->sha256(), job->preprocessed, job->arguments))) {
                error() << "Sent job" << job->arguments->sourceFile() << "to" << connection->client()->peerString();
                job->remoteConnections.insert(connection);
                std::shared_ptr<RemoteData> &data = mJobsByRemoteConnection[connection];
                if (!data)
                    data.reset(new RemoteData);
                data->byJob[job] = message->id();
                RemoteJob &remoteJob = data->byId[message->id()];
                remoteJob.job = job;
                remoteJob.startTime = Rct::monoMs();
                // data->byId[message->id()] = { job, Rct::monoMs() };
                job->flags |= Job::Remote;
                removeJob(job);
                addJob(Job::Compiling, job);
                sendMonitorMessage(String::format<128>("{\"type\":\"start\","
                                                       "\"host\":\"%s:%d\","
                                                       "\"peer\":\"%s\","
                                                       "\"path\":\"%s\","
                                                       "\"arguments\":\"%s\","
                                                       "\"id\":\"%p\"}",
                                                       mHostName.constData(), mOptions.port,
                                                       connection->client()->peerString().constData(),
                                                       job->arguments->sourceFile().constData(),
                                                       String::join(job->arguments->commandLine, ' ').constData(),
                                                       job.get()));
            } else {
#warning what to do here?
            }
            return;
        }
    }
    assert(mPeersByConnection.contains(connection));
    connection->send(JobMessage(message->id(), message->sha256()));
}

void Daemon::handleJobMessage(const std::shared_ptr<JobMessage> &message,
                              const std::shared_ptr<Connection> &connection)
{
    warning() << "Got job message" << message->sha256()
              << (message->args() ? message->args()->sourceFile() : Path());
    if (message->preprocessed().isEmpty()) {
        Peer *peer = mPeersByConnection.value(connection);
        assert(peer);
        peer->jobsAvailable.remove(message->sha256());
    } else {
        auto compiler = mCompilerCache->findBySha256(message->sha256());
        assert(compiler);
        List<String> env;
        env << ("PATH=" + compiler->path().parentDir());
        std::shared_ptr<Job> job(new Job(message->args(), compiler->path(), env, Path(), compiler));
        job->preprocessed = message->preprocessed();
        job->flags |= Job::FromRemote;
        job->source = connection;
        job->id = message->id();
        std::shared_ptr<RemoteData> &data = mJobsByRemoteConnection[connection];
        if (!data)
            data.reset(new RemoteData);
        data->byJob[job] = message->id();
        data->byId[message->id()] = { job, Rct::monoMs() };
        addJob(Job::PendingCompiling, job);
        error() << "Got job message" << message->id() << message->args()->sourceFile();
    }
    mOutstandingJobRequests.remove(message->id());
    checkJobRequestTimeout();
    startJobs();
}

void Daemon::handleJobResponseMessage(const std::shared_ptr<JobResponseMessage> &message,
                                      const std::shared_ptr<Connection> &connection)
{
    std::shared_ptr<Job> job = removeRemoteJob(connection, message->id());
    if (!job) {
        error() << "Couldn't find job with this id" << message->id();
        return;
    }
    error() << "Got job response" << message->id() << job->arguments->sourceFile()
            << (Rct::monoMs() - job->received);

    Path output = job->arguments->output();
    if (!output.startsWith('/'))
        output.prepend(job->cwd);
    Rct::writeFile(output, message->objectFileContents());
    debug() << "Writing object file contents to" << job->arguments->output() << message->status()
            << message->output();
#warning do we need to fflush this before notifying plastc?
    removeJob(job);
    sendMonitorMessage(String::format<128>("{\"type\":\"end\",\"id\":\"%p\"}", job.get()));
    job->localConnection->send(ClientJobResponseMessage(message->status(), message->output()));
    mJobsByLocalConnection.remove(job->localConnection);

    job->remoteConnections.remove(connection);
    if (!job->remoteConnections.isEmpty())
        sendJobDiscardedMessage(job);
}

void Daemon::handleJobDiscardedMessage(const std::shared_ptr<JobDiscardedMessage> &message,
                                       const std::shared_ptr<Connection> &connection)
{
    auto job = removeRemoteJob(connection, message->id());
    warning() << "Received job discarded message" << job.get() << message->id();
    if (job) {
        if (job->process) {
            mCompileJobsByProcess.remove(job->process);
            debug() << "Killing process";
            job->process->kill();
        }
        removeJob(job);
    }
}

void Daemon::handleDaemonListMessage(const std::shared_ptr<DaemonListMessage> &message,
                                     const std::shared_ptr<Connection> &connection)
{
    warning() << "Got daemon-list" << message->hosts().size();
    for (const auto &host : message->hosts()) {
        warning() << "Handling daemon list" << host.toString();
        Peer *&peer = mPeersByHost[host];
        if (!peer) {
            auto conn = std::make_shared<Connection>();
            warning() << "Trying to connect to peer:" << host.address << host.port;
            if (conn->connectTcp(host.address, host.port)) {
                peer = new Peer({ conn, host });
                mPeersByConnection[conn] = peer;
                mConnections.insert(conn);
                conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
                warning() << "Sending handshake to peer:" << host.toString();
                sendHandshake(conn);
            } else {
                error() << "Failed to connect to pper" << host.toString();
                mPeersByHost.remove(host);
            }
        }
    }
}

void Daemon::handleHandshakeMessage(const std::shared_ptr<HandshakeMessage> &message,
                                    const std::shared_ptr<Connection> &connection)
{
    Peer *&peer = mPeersByConnection[connection];
    Host host { connection->client()->peerName(), message->port(), message->friendlyName() };
    if (!peer) {
        peer = new Peer({ connection, host });
        mPeersByHost[host] = peer;
    } else {
        peer->host = host;
    }
    announceJobs(peer);
    warning() << "Got handshake from" << peer->host.friendlyName;
}

void Daemon::handleJobAnnouncementMessage(const std::shared_ptr<JobAnnouncementMessage> &message,
                                          const std::shared_ptr<Connection> &connection)
{
    warning() << "Got announcement" << message->announcement() << "from" << connection->client()->peerString();
    Peer *peer = mPeersByConnection.value(connection);
    assert(peer);
    peer->jobsAvailable = message->announcement();
    fetchJobs(peer);
}

void Daemon::reconnectToServer()
{
    if (mServerConnection->client()) {
        switch (mServerConnection->client()->state()) {
        case SocketClient::Connected:
            sendHandshake(mServerConnection);
            return;
        case SocketClient::Connecting:
            restartServerTimer();
            return;
        case SocketClient::Disconnected:
            break;
        }
    }

    warning() << "Trying to connect to server:" << mOptions.serverHost << mOptions.serverPort;
    if (mOptions.serverHost.isEmpty()) {
        mDiscoverySocket->writeTo("255.255.255.255", mOptions.discoveryPort, "?");
    } else if (!mServerConnection->connectTcp(mOptions.serverHost, mOptions.serverPort)) {
        restartServerTimer();
    } else if (mServerConnection->client()->state() == SocketClient::Connected) {
        sendHandshake(mServerConnection);
    }
}

void Daemon::sendHandshake(const std::shared_ptr<Connection> &conn)
{
    conn->send(HandshakeMessage(mHostName, mOptions.port, mOptions.jobCount));
}

void Daemon::onDiscoverySocketReadyRead(Buffer &&data, const String &ip)
{
    Buffer buf = std::forward<Buffer>(data);
    Deserializer deserializer(reinterpret_cast<const char*>(buf.data()), buf.size());
    char command;
    deserializer >> command;
    warning() << "Got discovery packet" << command;
    switch (command) {
    case 's':
        if (!mExplicitServer && !mServerConnection->isConnected()) {
            deserializer >> mOptions.serverHost >> mOptions.serverPort;
            warning() << "found server" << mOptions.serverHost << mOptions.serverPort;
            reconnectToServer();
        }
        break;
    case 'S':
        if (!mExplicitServer && !mServerConnection->isConnected()) {
            deserializer >> mOptions.serverPort;
            mOptions.serverHost = ip;
            warning() << "found server" << mOptions.serverHost << mOptions.serverPort;
            reconnectToServer();
        }
        break;
    case '?':
        if (mServerConnection->isConnected()) {
            String packet;
            {
                Serializer serializer(packet);
                serializer << 's' << mServerConnection->client()->peerName() << mOptions.serverPort;
            }
            warning() << "telling" << ip << "about server" << mServerConnection->client()->peerName() << mOptions.serverPort;
            mDiscoverySocket->writeTo(ip, mOptions.discoveryPort, packet);
        }
        break;
    }
}

void Daemon::restartServerTimer()
{
    mServerTimer.restart(1000, Timer::SingleShot);
}

void Daemon::onCompileProcessFinished(Process *process)
{
    std::shared_ptr<Job> job = mCompileJobsByProcess.take(process);
    warning() << "process finished" << process << job;
    if (job) {
        assert(job->process == process);
        job->process = 0;
        assert(job->flags & Job::Compiling);
        removeJob(job);
        if (job->flags & Job::FromRemote) {
            assert(job->source);
            removeRemoteJob(job->source, job->id);
            assert(!job->tempObjectFile.isEmpty());
            String objectFile;
#warning what to do if file fails to load? Try again locally? Also, what if the job produced other output (separate debug info)
            Rct::readFile(job->tempObjectFile, objectFile);
            Path::rm(job->tempObjectFile);
            error() << "Sending job response" << process->returnCode() << job->arguments->sourceFile();
            job->source->send(JobResponseMessage(job->id, process->returnCode(), objectFile, job->output));
        } else {
            sendMonitorMessage(String::format<128>("{\"type\":\"end\",\"id\":\"%p\"}", job.get()));

            job->localConnection->send(ClientJobResponseMessage(process->returnCode(), job->output));
            mJobsByLocalConnection.remove(job->localConnection);
            if (job->flags & Job::Remote) {
                error() << "Remote job finished" << job->arguments->sourceFiles().first() << Rct::monoMs() - job->received;
                assert(!job->remoteConnections.isEmpty());
                sendJobDiscardedMessage(job);
            } else {
                error() << "Local job finished" << job->arguments->sourceFiles().first() << Rct::monoMs() - job->received;
            }
        }

        assert(job.use_count() == 1);
    }
    EventLoop::deleteLater(process);
    startJobs();
}

int Daemon::startPreprocessingJobs()
{
    int ret = 0;
    while (mPreprocessJobsByProcess.size() < mOptions.preprocessCount && !mPendingPreprocessJobs.isEmpty()) {
        auto job = mPendingPreprocessJobs.first();
        assert(job->flags & Job::PendingPreprocessing);
        removeJob(job);
        List<String> args = job->arguments->commandLine.mid(1);
        if (job->arguments->flags & CompilerArgs::HasDashO) {
            for (int i=0; i<args.size(); ++i) {
                if (args.at(i) == "-o") {
                    if (++i == args.size()) {
                        job->localConnection->send(ClientJobResponseMessage());
                        mJobsByLocalConnection.remove(job->localConnection);
                        job.reset();
                    } else {
                        args[i] = "-";
                    }
                    break;
                }
            }
        } else {
            args << "-o" << "-";
        }
        if (!job)
            continue;
        args.append("-E");
        // assert(!arguments.isEmpty());
        const Path compiler = Compiler::resolve(job->arguments->commandLine.first());
        if (compiler.isEmpty()) {
            job->localConnection->send(ClientJobResponseMessage());
            mJobsByLocalConnection.remove(job->localConnection);
            error() << "Can't resolve compiler for" << args.first();
            continue;
        }
        assert(!job->process);
        job->process = new Process;
        mPreprocessJobsByProcess[job->process] = job;

        job->process->setCwd(job->cwd);
        addJob(Job::Preprocessing, job);
        job->process->finished().connect([this, args, compiler](Process *proc) {
                auto job = mPreprocessJobsByProcess.take(proc);
                debug() << "Preprocessjob finished" << job << proc->returnCode();
                if (job) {
                    removeJob(job);
                    const String err = proc->readAllStdErr();
                    if (!err.isEmpty()) {
                        error() << err;
                        job->output.append(Output({ Output::StdErr, err }));
                    }
                    if (proc->returnCode() != 0) {
                        job->localConnection->send(ClientJobResponseMessage());
                        mJobsByLocalConnection.remove(job->localConnection);
                    } else {
                        job->preprocessed = proc->readAllStdOut();
                        addJob(Job::PendingCompiling, job);
                        error() << "Preprocessing finished" << job->arguments->sourceFiles().first() << mPendingCompileJobs.size();
                        startJobs();
                    }
                }
            });

        if (!job->process->start(compiler, args, job->environ)) {
            job->localConnection->send(ClientJobResponseMessage());
            mJobsByLocalConnection.remove(job->localConnection);
            error() << "Can't start compiler for preprocessing" << String::join(args, ' ');
            continue;
        }
        debug() << "Starting preprocessing in" << job->cwd << String::format<256>("%s %s", compiler.constData(), String::join(args.mid(1), ' ').constData());
        ++ret;
    }
    return ret;
}

int Daemon::startCompileJobs()
{
    if (!mPreprocessJobsByProcess.isEmpty())
        return 0;
    auto startJob = [this](const std::shared_ptr<Job> &job) {
        List<String> args = job->arguments->commandLine.mid(1);
        assert(job->arguments->sourceFileIndexes.size() == 1);
        args.removeAt(job->arguments->sourceFileIndexes.first() - 1);
        CompilerArgs::Flag lang = static_cast<CompilerArgs::Flag>(job->arguments->flags & CompilerArgs::LanguageMask);
        switch (lang) {
        case CompilerArgs::CPlusPlus: lang = CompilerArgs::CPlusPlusPreprocessed; break;
        case CompilerArgs::C: lang = CompilerArgs::CPreprocessed; break;
        default: break;
        }
        int i=0;
        while (i<args.size()) {
            const String &arg = args.at(i);
            // error() << "considering" << i << arg;
            if (arg == "-MF") {
                args.remove(i, 2);
            } else if (arg == "-MT") {
                args.remove(i, 2);
            } else if (arg == "-MMD") {
                args.removeAt(i);
            } else if (arg.startsWith("-I")) {
                if (arg.size() == 2) {
                    args.remove(i, 2);
                } else {
                    args.removeAt(i);
                }
            } else {
                ++i;
            }
        }

        if (!(job->arguments->flags & CompilerArgs::HasDashX)) {
            args << "-x";
            args << CompilerArgs::languageName(lang);
        } else {
            const int idx = args.indexOf("-x");
            assert(idx != -1);
            assert(idx + 1 < args.size());
            args[idx + 1] = CompilerArgs::languageName(lang);
        }
        args << "-";
        if (job->flags & Job::FromRemote) {
            assert(job->tempObjectFile.isEmpty());
            const Path dir = mOptions.cacheDir + "output/";
            Path::mkdir(dir, Path::Recursive);
            job->tempObjectFile = dir + job->arguments->sourceFile().fileName() + ".XXXXXX";
            const int fd = mkstemp(&job->tempObjectFile[0]);
            // error() << errno << strerror(errno) << job->tempOutput;
            assert(fd != -1);
            close(fd);
            // error() << "Args are now" << args;
        } else {
            sendMonitorMessage(String::format<128>("{\"type\":\"start\","
                                                   "\"host\":\"%s:%d\","
                                                   "\"path\":\"%s\","
                                                   "\"arguments\":\"%s\","
                                                   "\"id\":\"%p\"}",
                                                   mHostName.constData(), mOptions.port,
                                                   job->arguments->sourceFile().constData(),
                                                   String::join(job->arguments->commandLine, ' ').constData(),
                                                   job.get()));
        }
        if (!(job->arguments->flags & CompilerArgs::HasDashO)) {
            args << "-o";
            if (!(job->flags & Job::FromRemote)) {
                args << job->arguments->output();
            } else {
                args << job->tempObjectFile;
            }
        } else if (job->flags & Job::FromRemote) {
            const int idx = args.indexOf("-o");
            assert(idx != -1);
            args[idx + 1] = job->tempObjectFile;
        }
        debug() << "Starting process" << args;
        assert(!args.isEmpty());
        job->process = new Process;
        job->process->setCwd(job->cwd);
        job->process->finished().connect(std::bind(&Daemon::onCompileProcessFinished, this, std::placeholders::_1));
#warning I hope this wont keep the shared_ptr alive for ever more?
        job->process->readyReadStdOut().connect([this](Process *process) {
                mCompileJobsByProcess[process]->output.append(Output({Output::StdOut, process->readAllStdOut()}));
            });
        job->process->readyReadStdErr().connect([this](Process *process) {
                mCompileJobsByProcess[process]->output.append(Output({Output::StdErr, process->readAllStdErr()}));
            });

        if (!job->process->start(String("/usr/bin/") + job->resolvedCompiler.fileName(), args, job->environ)) {
            delete job->process;
            removeJob(job);
            job->localConnection->send(ClientJobResponseMessage());
            mJobsByLocalConnection.remove(job->localConnection);
            assert(job.use_count() == 1);
            error() << "Failed to start compiler" << job->resolvedCompiler;
            return false;
        } else {
            debug() << "writing" << job->preprocessed.size() << "bytes to stdin of" << job->resolvedCompiler;
            job->process->write(job->preprocessed);
            job->process->closeStdIn();
            mCompileJobsByProcess[job->process] = job;
            return true;
        }
    };

    int ret = 0;
    while (mCompileJobsByProcess.size() < mOptions.jobCount && !mPendingCompileJobs.isEmpty()) {
        std::shared_ptr<Job> job;
        if (mOptions.flags & Options::NoLocalJobs) {
            for (auto j : mPendingCompileJobs) {
                if (j->flags & Job::FromRemote) {
                    job = j;
                    break;
                }
            }
            if (!job)
                break;
        } else {
            job = mPendingCompileJobs.first();
        }

        removeJob(job);
        addJob(Job::Compiling, job);
        if (startJob(job))
            ++ret;
    }

    if (mPreprocessingJobs.isEmpty() && mCompileJobsByProcess.size() < mOptions.jobCount) {
        // start local version of remote job, oldest first
        for (const auto &job : mCompilingJobs) {
            if (!job->process) {
                assert(!job->remoteConnections.isEmpty());
                assert(job->flags & Job::Remote);
                if (startJob(job)) {
                    ++ret;
                    if (mCompileJobsByProcess.size() == mOptions.jobCount) {
                        break;
                    }
                }
            }
        }
    }
    return ret;
}

void Daemon::startJobs()
{
    warning("startJobs jobCount: %d/%d preprocessCount: %d/%d pending compile: %d pending preprocess: %d",
            mCompileJobsByProcess.size(), mOptions.jobCount,
            mPreprocessJobsByProcess.size(), mOptions.preprocessCount,
            mPendingCompileJobs.size(), mPendingPreprocessJobs.size());
    const int startedPreprocessing = startPreprocessingJobs();
    const int startedCompileJobs = startCompileJobs();
    debug() << "Started preprocessing" << startedPreprocessing << "started compile jobs" << startedCompileJobs;
    announceJobs();
    fetchJobs();
}

void Daemon::announceJobs(Peer *peer)
{
    // debug() << "Announcing" << mPeersByConnection.size();
    assert(!peer || mPeersByConnection.contains(peer->connection));
    if (mPeersByConnection.isEmpty()) {
        return;
    }

    const int shaCount = mCompilerCache->count();
    Set<String> jobs;
    for (const auto &it : mPendingCompileJobs) {
        assert(it->compiler);
        if (!(it->flags & Job::FromRemote) && jobs.insert(it->compiler->sha256()) && jobs.size() == shaCount) {
            break;
        }
    }

    if (jobs.isEmpty())
        return;

    warning("Announcing jobs to %d peers. %s. Pending compile: %d Pending preprocess: %d",
            peer ? 1 : mPeersByConnection.size(), Log::toString(jobs).constData(),
            mPendingCompileJobs.size(), mPendingPreprocessJobs.size());

    const JobAnnouncementMessage msg(jobs);
    if (peer) {
        peer->connection->send(msg);
        warning() << "Announcing" << jobs.size() << "to" << peer->host.toString();
#warning handle send error?
    } else {
        for (auto peer : mPeersByConnection) {
            peer.first->send(msg);
            warning() << "Announcing" << jobs.size() << "to" << peer.second->host.toString();
        }
    }
}

void Daemon::fetchJobs(Peer *peer)
{
    int available = mOptions.jobCount - mOutstandingJobRequests.size() + mCompileJobsByProcess.size();
    if (available <= 0)
        return;

    List<std::pair<Peer *, String> > candidates;
    Set<String> compilerRequests;
    auto process = [&candidates, &compilerRequests, this, available](Peer *p) {
        for (const String &sha : p->jobsAvailable) {
            warning() << p->host.toString() << "has jobs for" << sha;
            if (auto compiler = mCompilerCache->findBySha256(sha)) {
                if (compiler->isValid()) {
                    debug() << "Adding a candidate" << sha;
                    candidates.append(std::make_pair(p, sha));
                    if (candidates.size() == available)
                        return false;
                    assert(candidates.size() < available);
                } else {
                    debug() << "We want no part of that compiler. It's BAD for us";
                }
            } else {
                if (compilerRequests.insert(sha)) {
                    p->connection->send(CompilerRequestMessage(sha));
                }
            }
        }
        return true;
    };
    if (peer) {
        process(peer);
    } else {
        for (auto p : mPeersByConnection) {
            if (!process(p.second))
                break;
        }
    }
    if (!candidates.isEmpty()) {
        int idx = 0;
        assert(candidates.size() <= available);
        while (available-- > 0) {
            auto cand = candidates.at(idx++ % candidates.size());
            warning() << "Requesting a job" << mNextJobId << "from" << cand.first->host.toString();
            if (cand.first->connection->send(JobRequestMessage(mNextJobId++, cand.second)))
                mOutstandingJobRequests[mNextJobId] = Rct::monoMs();
        }
        checkJobRequestTimeout();
    }
}

void Daemon::checkJobRequestTimeout()
{
    const uint64_t now = Rct::monoMs();
    auto it = mOutstandingJobRequests.begin();
    uint64_t timer = 0;
    while (it != mOutstandingJobRequests.end()) {
        if (now - it->second > 10000) { // people need to get back to us in 10 seconds
            it = mOutstandingJobRequests.erase(it);
        } else {
            const uint64_t elapseTime = it->second + 10000 - now;
            if (!timer || elapseTime < timer)
                timer = elapseTime;
            ++it;
        }
    }
    if (timer) {
        mOutstandingJobRequestsTimer.restart(timer, Timer::SingleShot);
    }
}


void Daemon::sendMonitorMessage(const String &message)
{
    if (mServerConnection)
        mServerConnection->send(MonitorMessage(message));
}

void Daemon::onConnectionDisconnected(Connection *conn)
{
    // warning() << "Lost connection" << conn->client()->port();
    std::shared_ptr<Connection> c = conn->shared_from_this();
    mConnections.remove(c);
    if (std::shared_ptr<Job> job = mJobsByLocalConnection.take(c)) {
        if (job->flags & Job::Preprocessing) {
            assert(job->process);
            mPreprocessJobsByProcess.remove(job->process);
            job->process->kill();
        } else if (job->flags & Job::Remote) {
            assert(!job->remoteConnections.isEmpty());
            sendJobDiscardedMessage(job);
        } else if (job->flags & Job::Compiling) {
            assert(job->process);
            mCompileJobsByProcess.remove(job->process);
            job->process->kill();
        }
        removeJob(job);
        assert(job.use_count() == 1);
        return;
    }

    if (Peer *peer = mPeersByConnection.take(c)) {
        error() << "Lost peer" << peer->host.friendlyName;
        mPeersByHost.remove(peer->host);
        auto remoteData = mJobsByRemoteConnection.take(c);
        if (remoteData) {
            for (const auto &remoteJob : remoteData->byJob) {
                remoteJob.first->remoteConnections.remove(c);
                if (!remoteJob.first->process && remoteJob.first->remoteConnections.isEmpty()) {
                    // move back to pending
                    removeJob(remoteJob.first);
                    addJob(Job::PendingCompiling, remoteJob.first);
                }
            }
        }
        // ### is this right?
        delete peer;
    }
}

void Daemon::handleConsoleCommand(const String &string)
{
    String str = string;
    while (str.endsWith(' '))
        str.chop(1);
    if (str == "jobs") {
        struct {
            LinkedList<std::shared_ptr<Job> > *list;
            const char *name;
        } const lists[] = {
            { &mPendingPreprocessJobs, "Pending preprocessing" },
            { &mPreprocessingJobs, "Preprocessing" },
            { &mPendingCompileJobs, "Pending compile" },
            { &mPreprocessingJobs, "Compiling" },
            { 0, 0 }
        };
        printf("Job capacity: %d Preprocessing capacity: %d\n", mOptions.jobCount, mOptions.preprocessCount);
        for (int i=0; lists[i].list; ++i) {
            if (!lists[i].list->isEmpty()) {
                printf("%s: %d\n", lists[i].name, lists[i].list->size());
                for (const auto &job : *lists[i].list) {
                    printf("Job: %s Received: %llu\n",
                           String::join(job->arguments->sourceFiles(), ", ").constData(), job->received);
                }
            }
        }
    } else if (str == "quit") {
        EventLoop::eventLoop()->quit();
    } else if (str == "peers") {
        for (const auto &peer : mPeersByHost) {
            printf("Peer: %s\n", peer.first.toString().constData());
            auto jobs = [](const char *name, const Set<String> &j) {
                if (j.isEmpty()) {
                    printf("%s: None\n", name);
                } else {
                    for (const String &sha : j)
                        printf("%s\n", sha.constData());
                }
            };
            jobs("Announced", peer.second->announced);
            jobs("Available", peer.second->jobsAvailable);
        }
    } else if (str.startsWith("monitor ")) {
        sendMonitorMessage(str.mid(8));
    } else if (str == "compilers") {
        printf("%s\n", mCompilerCache->dump().constData());
    }
}

void Daemon::handleConsoleCompletion(const String &string, int, int,
                                     String &common, List<String> &candidates)
{
    static const List<String> cands = List<String>() << "jobs" << "quit" << "peers" << "compilers" << "monitor";
    auto res = Console::tryComplete(string, cands);
    // error() << res.text << res.candidates;
    common = res.text;
    candidates = res.candidates;
}

void Daemon::addJob(Job::Flag flag, const std::shared_ptr<Job> &job)
{
    assert(job);
    assert(!(job->flags & Job::StateMask));
    assert(flag & Job::StateMask);
    job->flags |= flag;
    switch (flag) {
    case Job::PendingPreprocessing:
        job->position = mPendingPreprocessJobs.insert(mPendingPreprocessJobs.end(), job);
        break;
    case Job::Preprocessing:
        job->position = mPreprocessingJobs.insert(mPreprocessingJobs.end(), job);
        break;
    case Job::PendingCompiling:
        job->position = mPendingCompileJobs.insert(mPendingCompileJobs.end(), job);
        break;
    case Job::Compiling:
        job->position = mCompilingJobs.insert(mCompilingJobs.end(), job);
        break;
    default:
        assert(0);
    }
}

void Daemon::removeJob(const std::shared_ptr<Job> &job)
{
    assert(job);
    const Job::Flag flag = static_cast<Job::Flag>(job->flags & Job::StateMask);
    assert(flag);
    job->flags &= ~flag;
    switch (flag) {
    case Job::PendingPreprocessing:
        mPendingPreprocessJobs.erase(job->position);
        break;
    case Job::Preprocessing:
        mPreprocessingJobs.erase(job->position);
        break;
    case Job::PendingCompiling:
        mPendingCompileJobs.erase(job->position);
        break;
    case Job::Compiling:
        mCompilingJobs.erase(job->position);
        if (!mAnnouncementDirty) {
            for (const auto &otherJob : mPendingCompileJobs) {
                if (!(otherJob->flags & Job::FromRemote) && otherJob->compiler != job->compiler) {
                    mAnnouncementDirty = true;
                    break;
                }
            }
        }
        break;
    default:
        assert(0);
    }
}

void Daemon::sendJobDiscardedMessage(const std::shared_ptr<Job> &job)
{
    assert(!job->remoteConnections.isEmpty());
    assert(job->flags & Job::Remote);
    for (const auto &remoteConnection : job->remoteConnections) {
        if (uint64_t id = removeRemoteJob(remoteConnection, job)) {
            error() << "Sent job discarded message to" << remoteConnection->client()->peerString() << id;
            remoteConnection->send(JobDiscardedMessage(id));
        }
    }
}

std::shared_ptr<Daemon::Job> Daemon::removeRemoteJob(const std::shared_ptr<Connection> &conn, uint64_t id)
{
    std::shared_ptr<RemoteData> &data = mJobsByRemoteConnection[conn];
    if (!data)
        return std::shared_ptr<Job>();
    std::shared_ptr<Job> job = data->byId.take(id).job;
    if (job) {
        data->byJob.remove(job);
        if (data->byJob.isEmpty() && data->byId.isEmpty()) {
            data.reset();
        }
    }
    return job;
}

uint64_t Daemon::removeRemoteJob(const std::shared_ptr<Connection> &conn, const std::shared_ptr<Job> &job)
{
    std::shared_ptr<RemoteData> &data = mJobsByRemoteConnection[conn];
    if (!data)
        return 0;
    const uint64_t id = data->byJob.take(job);
    if (id) {
        data->byId.remove(id);
    }
    return id;
}

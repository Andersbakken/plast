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
    : mNextJobId(1), mExplicitServer(false), mServerConnection(std::make_shared<Connection>())
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
            warning() << "Connected to" << conn->client()->peerString() << "Sending handshake";
            sendHandshake(mServerConnection);
        });

    mServerConnection->error().connect(std::bind(&Daemon::restartServerTimer, this));

    mServerTimer.timeout().connect(std::bind(&Daemon::reconnectToServer, this));
    mOutstandingJobRequestsTimer.timeout().connect(std::bind(&Daemon::checkJobRequstTimeout, this));
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

    for (const Path &path : mOptions.cacheDir.files(Path::Directory)) {
        // error() << path;
        if (Path(path + "BAD").isFile()) {
            Compiler::insert(Path(), path.name(), Set<Path>());
            continue;
        }
        List<String> shaList;
        Set<Path> files;
        for (const Path &file : path.files(Path::File)) {
            if (!file.isSymLink()) {
                shaList.append(file.fileName());
                files.insert(file);
                // error() << file.fileName();
            }
        }
        SHA256 sha;
        shaList.sort();
        for (const String &fn : shaList) {
            sha.update(fn);
        }
        const String sha256 = sha.hash(SHA256::Hex);
        if (sha256 != path.name()) {
            error() << "Invalid compiler" << path << sha256;
            Path::rmdir(path);
        } else {
            const Path exec = Path::resolved(path + "/COMPILER");
            if (!exec.isFile()) {
                error() << "Can't find COMPILER symlink";
            } else {
                warning() << "Got compiler" << sha256 << path.fileName();
                Compiler::insert(exec, sha256, files);
            }
        }
    }

    mExplicitServer = !mOptions.serverHost.isEmpty();
    reconnectToServer();

    return true;
}

void Daemon::onNewMessage(Message *message, Connection *conn)
{
    auto connection = conn->shared_from_this();
    switch (message->messageId()) {
    case ClientJobMessageId:
        handleClientJobMessage(static_cast<ClientJobMessage*>(message), connection);
        break;
    case QuitMessageId:
        warning() << "Quitting by request";
        EventLoop::eventLoop()->quit();
        break;
    case DaemonJobAnnouncementMessageId:
        handleJobAnnouncementMessage(static_cast<JobAnnouncementMessage*>(message), connection);
        break;
    case CompilerMessageId:
        handleCompilerMessage(static_cast<CompilerMessage*>(message), connection);
        break;
    case CompilerRequestMessageId:
        handleCompilerRequestMessage(static_cast<CompilerRequestMessage*>(message), connection);
        break;
    case JobRequestMessageId:
        handleJobRequestMessage(static_cast<JobRequestMessage*>(message), connection);
        break;
    case JobMessageId:
        handleJobMessage(static_cast<JobMessage*>(message), connection);
        break;
    case JobResponseMessageId:
        handleJobResponseMessage(static_cast<JobResponseMessage*>(message), connection);
        break;
    case DaemonListMessageId:
        handleDaemonListMessage(static_cast<DaemonListMessage*>(message), connection);
        break;
    case HandshakeMessageId:
        handleHandshakeMessage(static_cast<HandshakeMessage*>(message), connection);
        break;
    default:
        error() << "Unexpected message" << message->messageId();
        break;
    }
}

void Daemon::handleClientJobMessage(const ClientJobMessage *msg, const std::shared_ptr<Connection> &conn)
{
    debug() << "Got local job" << msg->arguments() << msg->cwd();

    List<String> env = msg->environ();
    assert(!env.contains("PLAST=1"));
    env.append("PLAST=1");

    const Path resolvedCompiler = msg->resolvedCompiler();
    std::shared_ptr<Compiler> compiler = Compiler::compiler(resolvedCompiler);
    if (!compiler) {
        warning() << "Can't find compiler for" << resolvedCompiler;
        conn->send(ClientJobResponseMessage());
        conn->close();
        return;
    }
    std::shared_ptr<Job> job = std::make_shared<Job>(msg->arguments(), resolvedCompiler, env, msg->cwd(), compiler, conn);
    mJobsByLocalConnection[conn] = job;
    addJob(Job::PendingPreprocessing, job);
    startJobs();
}

void Daemon::handleCompilerMessage(const CompilerMessage *message, const std::shared_ptr<Connection> &connection)
{
    assert(message->isValid());
    if (!message->writeFiles(mOptions.cacheDir + message->sha256() + '/')) {
        error() << "Couldn't write files to" << mOptions.cacheDir + message->sha256() + '/';
    } else {
        warning() << "Wrote compiler" << message->sha256() << "to" << mOptions.cacheDir;
    }
}

void Daemon::handleCompilerRequestMessage(const CompilerRequestMessage *message, const std::shared_ptr<Connection> &connection)
{
    std::shared_ptr<Compiler> compiler = Compiler::compilerBySha256(message->sha256());
    if (compiler) {
        error() << "Sending compiler" << message->sha256();
        connection->send(CompilerMessage(compiler));
    } else {
        error() << "I don't know nothing about no" << message->sha256() << "Fisk" << Compiler::dump();
    }
}

void Daemon::handleJobRequestMessage(const JobRequestMessage *message, const std::shared_ptr<Connection> &connection)
{
    error() << "Got job request" << message->id() << message->sha256();
    auto it = mPendingCompileJobs.begin();
    while (it != mPendingCompileJobs.end()) {
        if ((*it)->compiler->sha256() == message->sha256()) {
            List<String> args = job->args;
            if (connection->send(JobMessage(message->id(), message->sha256(), args))) {
                auto job = *it;
                removeJob(job);
                job->remoteConnection = connection;
                mJobsByRemoteConnection[connection].insert(job);
                job->flags |= Job::Remote;
                addJob(Job::Compiling, job);
                job->remoteId = message->id();
            }
            return;
        }
        ++it;
    }

    assert(mPeersByConnection.contains(connection));
    mPeersByConnection[connection]->announced.remove(message->sha256());
}

void Daemon::handleJobMessage(const JobMessage *message, const std::shared_ptr<Connection> &connection)
{
    error() << "Got job message" << message->id() << message->preprocessed().size() << message->args();
}

void Daemon::handleJobResponseMessage(const JobResponseMessage *message, const std::shared_ptr<Connection> &connection)
{
    error() << "Got job response" << message->id() << message->files().keys();
}

void Daemon::handleDaemonListMessage(const DaemonListMessage *message, const std::shared_ptr<Connection> &connection)
{
    warning() << "Got daemon-list" << message->hosts().size();
    for (const auto &host : message->hosts()) {
        warning() << "Handling daemon list" << host.toString();
        Peer *&peer = mPeersByHost[host];
        if (!peer) {
            auto conn = std::make_shared<Connection>();
            warning() << "Trying to connect to" << host.address << host.port << host.friendlyName;
            if (conn->connectTcp(host.address, host.port) || conn->connectTcp(host.friendlyName, host.port)) {
                peer = new Peer({ conn, host, Set<String>() });
                mPeersByConnection[conn] = peer;
                mConnections.insert(conn);
                conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
                warning() << "Sending handshake to" << host.toString();
                sendHandshake(conn);
            } else {
                error() << "Failed to connect to host" << host.toString();
                mPeersByHost.remove(host);
            }
        }
    }
}

void Daemon::handleHandshakeMessage(const HandshakeMessage *message, const std::shared_ptr<Connection> &connection)
{
    Peer *&peer = mPeersByConnection[connection];
    Host host { connection->client()->peerName(), message->port(), message->friendlyName() };
    if (!peer) {
        peer = new Peer({ connection, host });
        mPeersByHost[host] = peer;
    } else {
        peer->host = host;
    }
    warning() << "Got handshake from" << peer->host.friendlyName;
}

void Daemon::handleJobAnnouncementMessage(const JobAnnouncementMessage *message, const std::shared_ptr<Connection> &connection)
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

    warning() << "Trying to connect" << mOptions.serverHost << mOptions.serverPort;
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
    conn->send(HandshakeMessage(Rct::hostName(), mOptions.port, mOptions.jobCount));
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

template <typename T>
static inline bool addOutput(Process *process, Hash<Process*, T> &hash,
                             Output::Type type, const String &text)
{
    if (T t = hash.value(process)) {
        t->output.append(Output({type, text}));
        return true;
    }
    return false;
}

void Daemon::onCompileProcessReadyReadStdOut(Process *process)
{
    const String out = process->readAllStdOut();
    debug() << "ready read stdout" << process << out;
    addOutput(process, mCompileJobsByProcess, Output::StdOut, out);
}

void Daemon::onCompileProcessReadyReadStdErr(Process *process)
{
    const String out = process->readAllStdErr();
    debug() << "ready read stderr" << process << out;
    addOutput(process, mCompileJobsByProcess, Output::StdErr, out);
}

void Daemon::onCompileProcessFinished(Process *process)
{
    std::shared_ptr<Job> job = mCompileJobsByProcess.take(process);
    warning() << "process finished" << process << job;
    if (job) {
        assert(job->process == process);
        assert(job->flags & Job::Compiling);
        mCompilingJobs.erase(job->position);
        job->localConnection->send(ClientJobResponseMessage(process->returnCode(), job->output));
        mJobsByLocalConnection.remove(job->localConnection);
        job->process = 0;
        assert(job.use_count() == 1);
    }
    EventLoop::deleteLater(process);
    startJobs();
}

void Daemon::startJobs()
{
    debug() << "startJobs" << mOptions.jobCount << mOptions.preprocessCount
            << mPreprocessJobsByProcess.size() << mCompileJobsByProcess.size();
    while (mPreprocessJobsByProcess.size() < mOptions.preprocessCount && !mPendingPreprocessJobs.isEmpty()) {
        auto job = mPendingPreprocessJobs.first();
        assert(job->flags & Job::PendingPreprocessing);
        removeJob(job);
        List<String> args = job->arguments->commandLine.mid(1);
        if (job->arguments->flags & CompilerArgs::HasOutput) {
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
                        warning() << "Preprocessing finished" << compiler << args << job->preprocessed.size();
                        addJob(Job::PendingCompiling, job);
                        startJobs();
                    }
                }
            });

        job->process->start(compiler, args, job->environ);
        debug() << "Starting preprocessing in" << job->cwd << String::format<256>("%s %s", compiler.constData(), String::join(args.mid(1), ' ').constData());
    }

    if (!mOptions.flags & Options::NoLocalJobs) {
        while (mCompileJobsByProcess.size() < mOptions.jobCount && !mPendingCompileJobs.isEmpty()) {
            auto job = mPendingCompileJobs.first();
            removeJob(job);
            addJob(Job::Compiling, job);

            List<String> args = job->arguments->commandLine.mid(1);
            assert(job->arguments->sourceFileIndexes.size() == 1);
            args.removeAt(job->arguments->sourceFileIndexes.first() - 1);
            CompilerArgs::Flag lang = static_cast<CompilerArgs::Flag>(job->arguments->flags & CompilerArgs::LanguageMask);
            switch (lang) {
            case CompilerArgs::CPlusPlus: lang = CompilerArgs::CPlusPlusPreprocessed; break;
            case CompilerArgs::C: lang = CompilerArgs::CPreprocessed; break;
            default: break; // ### what about the other languages?
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
                assert(job->tempOutput.isEmpty());
                const Path dir = mOptions.cacheDir + "output/";
                Path::mkdir(dir, Path::Recursive);
                job->tempOutput = dir + job->arguments->sourceFile().fileName() + ".XXXXXX";
                const int fd = mkstemp(&job->tempOutput[0]);
                // error() << errno << strerror(errno) << job->tempOutput;
                assert(fd != -1);
                close(fd);
            }
            if (!(job->arguments->flags & CompilerArgs::HasOutput)) {
                args << "-o";
                if (!(job->flags & Job::FromRemote)) {
                    Path sourceFile = job->arguments->sourceFile();
                    const int lastDot = sourceFile.lastIndexOf('.');
                    if (lastDot != -1 && lastDot > sourceFile.lastIndexOf('/')) {
                        sourceFile.chop(sourceFile.size() - lastDot - 1);
                        sourceFile.append('o');
                    }
                    args << sourceFile;
                } else {
                    args << job->tempOutput;
                }
            } else if (job->flags & Job::FromRemote) {
                const int idx = args.indexOf("-o");
                assert(idx != -1);
                args[idx + 1] = job->tempOutput;
            }
            debug() << "Starting process" << args;
            assert(!args.isEmpty());
            job->process = new Process;
            job->process->setCwd(job->cwd);
            job->process->finished().connect(std::bind(&Daemon::onCompileProcessFinished, this, std::placeholders::_1));
            job->process->readyReadStdOut().connect(std::bind(&Daemon::onCompileProcessReadyReadStdOut, this, std::placeholders::_1));
            job->process->readyReadStdErr().connect(std::bind(&Daemon::onCompileProcessReadyReadStdErr, this, std::placeholders::_1));
            // if (job->flags & Job::FromRemote) {
            // ### need to make up a temp file
            // if (job->arguments.flags & CompilerArgs::HasOutput) {
            //     for (int i=0; i<args.size(); ++i) {
            //         if (args.at(i) == "-o") {
            //             if (++i == args.size()) {
            //                 job->localConnection->send(ClientJobResponseMessage());
            //                 mJobsByLocalConnection.remove(job->localConnection);
            //                 job.reset();
            //             } else {
            //                 args[i] = "-";
            //             }
            //             break;
            //         }
            //     }
            // }

            if (!job->process->start(job->resolvedCompiler, args, job->environ)) {
                delete job->process;
                removeJob(job);
                job->localConnection->send(ClientJobResponseMessage());
                mJobsByLocalConnection.remove(job->localConnection);
                assert(job.use_count() == 1);
                error() << "Failed to start compiler" << job->resolvedCompiler;
            } else {
                job->process->write(job->preprocessed);
                job->process->closeStdIn();
                mCompileJobsByProcess[job->process] = job;
            }
        }
        // ### start compiling our jobs that are being handled by other daemons
    }
    announceJobs();
}

void Daemon::announceJobs(Peer *peer)
{
    if (mPeersByConnection.isEmpty())
        return;

    const int shaCount = Compiler::count();
    Set<String> jobs;
    for (const auto &it : mPendingCompileJobs) {
        assert(it->compiler);
        if (jobs.insert(it->compiler->sha256()) && jobs.size() == shaCount) {
            break;
        }
    }

    if (peer) {
        if (peer->announced != jobs) {
            const JobAnnouncementMessage msg(jobs);
            peer->connection->send(msg);
        }
    } else {
        std::shared_ptr<JobAnnouncementMessage> msg;
        for (auto peer : mPeersByConnection) {
            if (peer.second->announced != jobs) {
                if (!msg)
                    msg.reset(new JobAnnouncementMessage(jobs));
                peer.first->send(*msg);
            }
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
    auto process = [&candidates, &compilerRequests, available](Peer *p) {
        for (const String &sha : p->jobsAvailable) {
            if (auto compiler = Compiler::compilerBySha256(sha)) {
                if (!compiler) {
                    if (compilerRequests.insert(sha)) {
                        p->connection->send(CompilerRequestMessage(sha));
                    }
                } else if (compiler->isValid()) {
                    candidates.append(std::make_pair(p, sha));
                    if (candidates.size() == available)
                        return false;
                    assert(candidates.size() < available);
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
    int idx = 0;
    assert(candidates.size() <= available);
    while (available-- > 0) {
        auto cand = candidates.at(idx++ % candidates.size());
        mOutstandingJobRequests[mNextJobId] = Rct::monoMs();
        cand.first->connection->send(JobRequestMessage(mNextJobId++, cand.second));
    }
}

void Daemon::checkJobRequstTimeout()
{
    const uint64_t now = Rct::monoMs();
    auto it = mOutstandingJobRequests.begin();
    uint64_t timer = 0;
    while (it != mOutstandingJobRequests.end()) {
        if (now - it->second > 10000) { // people need to get back to us in 10 seconds
            it = mOutstandingJobRequests.erase(it);
        } else {
            const uint64_t elapseTime = it->second + 10000;
            if (!timer || elapseTime < timer)
                timer = elapseTime;
            ++it;
        }
    }
    if (timer) {
        mOutstandingJobRequestsTimer.restart(timer, Timer::SingleShot);
    }
}

void Daemon::onConnectionDisconnected(Connection *conn)
{
#warning need to handle all jobs we had sitting in mJobsByRemoteConnection. Return to pending compilation
    // warning() << "Lost connection" << conn->client()->port();
    std::shared_ptr<Connection> c = conn->shared_from_this();
    mConnections.remove(c);
    if (std::shared_ptr<Job> job = mJobsByLocalConnection.take(c)) {
        if (job->flags & Job::Preprocessing) {
            assert(job->process);
            mPreprocessJobsByProcess.remove(job->process);
            job->process->kill();
        } else if (job->flags & Job::Compiling) {
            assert(job->process);
            mCompileJobsByProcess.remove(job->process);
            job->process->kill();
        }
        removeJob(job);
        assert(job.use_count() == 1);
        // ### need to tell remote connection that we're no longer interested
    } else if (Peer *peer = mPeersByConnection.take(c)) {
        error() << "Lost peer" << peer->host.friendlyName;
        mPeersByHost.remove(peer->host);
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
        for (int i=0; lists[i].list; ++i) {
            if (!lists[i].list->isEmpty()) {
                printf("%s: %d\n", lists[i].name, lists[i].list->size());
                for (const auto &job : *lists[i].list) {
                    printf("Job: %s Received: %s\n",
                           String::join(job->arguments->sourceFiles(), ", ").constData(),
                           String::formatTime(job->received).constData());
                }
            }
        }
    } else if (str == "quit") {
        EventLoop::eventLoop()->quit();
    } else if (str == "peers") {
        for (const auto &peer : mPeersByHost) {
            printf("Peer: %s\n", peer.first.toString().constData());
        }
    } else if (str == "compilers") {
        printf("%s\n", Compiler::dump().constData());
    }
}

void Daemon::handleConsoleCompletion(const String &string, int, int,
                                     String &common, List<String> &candidates)
{
    static const List<String> cands = List<String>() << "jobs" << "quit" << "peers" << "compilers";
    auto res = Console::tryComplete(string, cands);
    // error() << res.text << res.candidates;
    common = res.text;
    candidates = res.candidates;
}

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
#include "Compiler.h"
#include <rct/SHA256.h>

Daemon::Daemon()
    : mExplicitServer(false), mAnnouncementPending(false), mServerConnection(std::make_shared<Connection>())
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

    mServerTimer.timeout().connect([this](Timer *) { reconnectToServer(); });
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
        handleDaemonJobAnnouncementMessage(static_cast<DaemonJobAnnouncementMessage*>(message), connection);
        break;
    case CompilerMessageId:
        handleCompilerMessage(static_cast<CompilerMessage*>(message), connection);
        break;
    case CompilerRequestMessageId:
        handleCompilerRequestMessage(static_cast<CompilerRequestMessage*>(message), connection);
        break;
    case DaemonJobRequestMessageId:
        handleDaemonJobRequestMessage(static_cast<DaemonJobRequestMessage*>(message), connection);
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
    debug() << "Got localjob" << msg->arguments() << msg->cwd();

    List<String> env = msg->environ();
    assert(!env.contains("PLAST=1"));
    env.append("PLAST=1");

    const Path resolvedCompiler = Compiler::resolve(msg->arguments().first());
    std::shared_ptr<Compiler> compiler = Compiler::compiler(resolvedCompiler);
    if (!compiler) {
        warning() << "Can't find compiler for" << resolvedCompiler;
        conn->send(ClientJobResponseMessage());
        conn->close();
        return;
    }
    std::shared_ptr<LocalJob> localJob = std::make_shared<LocalJob>(msg->arguments(), resolvedCompiler, env, msg->cwd(), compiler, conn);
    if (localJob->arguments.mode != CompilerArgs::Compile) {
        warning() << "Not a compile job" << localJob->arguments.modeName();
        conn->send(ClientJobResponseMessage());
        return;
    }
    mLocalJobsByLocalConnection[conn] = localJob;
    addLocalJob(LocalJob::PendingPreprocessing, localJob);
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

void Daemon::handleDaemonJobRequestMessage(const DaemonJobRequestMessage *message, const std::shared_ptr<Connection> &connection)
{

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
                peer = new Peer({ conn, host });
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

void Daemon::handleDaemonJobAnnouncementMessage(const DaemonJobAnnouncementMessage *message, const std::shared_ptr<Connection> &connection)
{
    warning() << "Got announcement" << message->announcement() << "from" << connection->client()->peerString();
    for (const auto &announcement : message->announcement()) {
        if (auto compiler = Compiler::compilerBySha256(announcement.first)) {
            // if (compiler->isValid()) {
            //     error() << "I have the compiler. Lets go";
            // }
        } else {
            error() << "requesting compiler" << announcement.first;
            connection->send(CompilerRequestMessage(announcement.first));
        }
    }
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
    if (!addOutput(process, mLocalCompileJobsByProcess, Output::StdOut, out))
        addOutput(process, mRemoteJobsByProcess, Output::StdOut, out);
}

void Daemon::onCompileProcessReadyReadStdErr(Process *process)
{
    const String out = process->readAllStdErr();
    debug() << "ready read stderr" << process << out;
    if (!addOutput(process, mLocalCompileJobsByProcess, Output::StdErr, out))
        addOutput(process, mRemoteJobsByProcess, Output::StdErr, out);
}

void Daemon::onCompileProcessFinished(Process *process)
{
    std::shared_ptr<LocalJob> localJob = mLocalCompileJobsByProcess.take(process);
    error() << "process finished" << process << localJob;
    if (localJob) {
        assert(localJob->process == process);
        assert(localJob->flags & LocalJob::Compiling);
        mCompilingJobs.erase(localJob->position);
        localJob->localConnection->send(ClientJobResponseMessage(process->returnCode(), localJob->output));
        error() << "Foobar" << process->returnCode() << localJob->output.size();
        mLocalJobsByLocalConnection.remove(localJob->localConnection);
        localJob->process = 0;
        assert(localJob.use_count() == 1);
    }
    EventLoop::deleteLater(process);
    EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));
}

void Daemon::startJobs()
{
    debug() << "startJobs" << mOptions.jobCount << mOptions.preprocessCount << mPreprocessJobsByProcess.size()
            << mLocalCompileJobsByProcess.size() << mRemoteJobsByProcess.size();
    while (mPreprocessJobsByProcess.size() < mOptions.preprocessCount && !mPendingPreprocessJobs.isEmpty()) {
        auto job = mPendingPreprocessJobs.first();
        assert(job->flags & LocalJob::PendingPreprocessing);
        removeLocalJob(job);
        List<String> args = job->arguments.arguments.mid(1);
        if (job->arguments.flags & CompilerArgs::HasOutput) {
            for (int i=0; i<args.size(); ++i) {
                if (args.at(i) == "-o") {
                    if (++i == args.size()) {
                        job->localConnection->send(ClientJobResponseMessage());
                        mLocalJobsByLocalConnection.remove(job->localConnection);
                        job.reset();
                    } else {
                        args[i] = "-";
                    }
                    break;
                }
            }
        }
        if (!job)
            continue;
        args.append("-E");
        // assert(!arguments.isEmpty());
        const Path compiler = Compiler::resolve(job->arguments.arguments.first());
        if (compiler.isEmpty()) {
            job->localConnection->send(ClientJobResponseMessage());
            mLocalJobsByLocalConnection.remove(job->localConnection);
            error() << "Can't resolve compiler for" << args.first();
            continue;
        }
        assert(!job->process);
        job->process = new Process;
        mPreprocessJobsByProcess[job->process] = job;
        EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));

        job->process->setCwd(job->cwd);
        addLocalJob(LocalJob::Preprocessing, job);
        job->process->finished().connect([this, args, compiler](Process *proc) {
                auto job = mPreprocessJobsByProcess.take(proc);
                debug() << "Preprocessjob finished" << job << proc->returnCode();
                if (job) {
                    removeLocalJob(job);
                    const String err = proc->readAllStdErr();
                    if (!err.isEmpty()) {
                        error() << err;
                        job->output.append(Output({ Output::StdErr, err }));
                    }
                    if (proc->returnCode() != 0) {
                        job->localConnection->send(ClientJobResponseMessage());
                        mLocalJobsByLocalConnection.remove(job->localConnection);
                    } else {
                        job->preprocessed = proc->readAllStdOut();
                        error() << "Preprocessing finished" << compiler << args << job->preprocessed.size();
                        addLocalJob(LocalJob::PendingCompiling, job);
                        EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));
                    }
                }
            });

        job->process->start(compiler, args, job->environ);
        debug() << "Starting preprocessing in" << job->cwd << String::format<256>("%s %s", compiler.constData(), String::join(args.mid(1), ' ').constData());
    }

    if (!mOptions.flags & Options::NoLocalJobs) {
        while (mLocalCompileJobsByProcess.size() + mRemoteJobsByProcess.size() < mOptions.jobCount
               && !mPendingCompileJobs.isEmpty()) {
            auto job = mPendingCompileJobs.first();
            removeLocalJob(job);
            String err;
            addLocalJob(LocalJob::Compiling, job);
            job->process = startProcess(job->resolvedCompiler, job->arguments.arguments, job->environ, job->cwd, &err);
            error() << "Starting" << job->resolvedCompiler << job->arguments.arguments;
            if (!job->process) {
                removeLocalJob(job);
                job->localConnection->send(ClientJobResponseMessage());
                mLocalJobsByLocalConnection.remove(job->localConnection);
                assert(job.use_count() == 1);
            } else {
                mLocalCompileJobsByProcess[job->process] = job;
            }
        }
        // ### start compiling our jobs that are being handled by other daemons
    }
    if (!mAnnouncementPending) {
        mAnnouncementPending = true;
        EventLoop::eventLoop()->callLater(std::bind(&Daemon::announceJobs, this));
    }
}

void Daemon::announceJobs()
{
    assert(mAnnouncementPending);
    mAnnouncementPending = false;
    if (!mPeersByConnection.isEmpty() /* || mOptions.flags & Options::NoLocalJobs */) {
        Hash<String, int> announcements;
        for (const auto &it : mPendingCompileJobs) {
            assert(it->compiler);
            ++announcements[it->compiler->sha256()];
        }
        auto it = announcements.begin();
        while (it != announcements.end()) {
            if (it->second == mLastAnnouncements.value(it->first)) {
                it = announcements.erase(it);
            } else {
                mLastAnnouncements[it->first] = it->second;
                ++it;
            }
        }
        it = mLastAnnouncements.begin();
        while (it != mLastAnnouncements.end()) {
            if (!announcements.contains(it->first)) {
                it = mLastAnnouncements.erase(it);
            } else {
                ++it;
            }
        }
        const DaemonJobAnnouncementMessage msg(announcements);
        for (const auto &connection : mPeersByConnection) {
            connection.first->send(msg);
        }
    }
}

void Daemon::onConnectionDisconnected(Connection *conn)
{
    // warning() << "Lost connection" << conn->client()->port();
    std::shared_ptr<Connection> c = conn->shared_from_this();
    mConnections.remove(c);
    if (std::shared_ptr<LocalJob> job = mLocalJobsByLocalConnection.take(c)) {
        if (job->flags & LocalJob::Preprocessing) {
            assert(job->process);
            mPreprocessJobsByProcess.remove(job->process);
            job->process->kill();
        } else if (job->flags & LocalJob::Compiling) {
            assert(job->process);
            mLocalCompileJobsByProcess.remove(job->process);
            job->process->kill();
        }
        removeLocalJob(job);
        assert(job.use_count() == 1);
        // ### need to tell remote connection that we're no longer interested
    } else if (Peer *peer = mPeersByConnection.take(c)) {
        error() << "Lost peer" << peer->host.friendlyName;
        mPeersByHost.remove(peer->host);
        delete peer;
    }
}

Process *Daemon::startProcess(const Path &compiler, const List<String> &arguments, const List<String> &environ, const Path &cwd, String *err)
{
    debug() << "Starting process" << arguments;
    assert(!arguments.isEmpty());
    if (compiler.isEmpty()) {
        error() << "Can't resolve compiler for" << arguments.first();
        return 0;
    }
    Process *process = new Process;
    process->setCwd(cwd);
    process->finished().connect(std::bind(&Daemon::onCompileProcessFinished, this, std::placeholders::_1));
    process->readyReadStdOut().connect(std::bind(&Daemon::onCompileProcessReadyReadStdOut, this, std::placeholders::_1));
    process->readyReadStdErr().connect(std::bind(&Daemon::onCompileProcessReadyReadStdErr, this, std::placeholders::_1));
    if (!process->start(compiler, arguments.mid(1), environ)) {
        error() << "Failed to start compiler" << compiler;
        if (err)
            *err = "Failed to start compiler: " + compiler;
        delete process;
        return 0;
    }
    debug() << "Started process" << compiler << arguments.mid(1) << process;
    return process;
}

void Daemon::handleConsoleCommand(const String &string)
{
    String str = string;
    while (str.endsWith(' '))
        str.chop(1);
    if (str == "jobs") {
        struct {
            LinkedList<std::shared_ptr<LocalJob> > *list;
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
                           String::join(job->arguments.sourceFiles, ", ").constData(),
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

void Daemon::handleConsoleCompletion(const String& string, int, int,
                                     String &common, List<String> &candidates)
{
    static const List<String> cands = List<String>() << "jobs" << "quit" << "peers" << "compilers";
    auto res = Console::tryComplete(string, cands);
    // error() << res.text << res.candidates;
    common = res.text;
    candidates = res.candidates;
}

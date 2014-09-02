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

Daemon::Daemon()
    : mExplicitServer(false), mSentHandshake(false), mServerConnection(std::make_shared<Connection>())
{
    Console::init("plastd> ",
                  std::bind(&Daemon::handleConsoleCommand, this, std::placeholders::_1),
                  std::bind(&Daemon::handleConsoleCompletion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    const auto onNewConnection = [this](SocketServer *server) {
        while (true) {
            auto socket = server->nextConnection();
            if (!socket)
                break;
            std::shared_ptr<Connection> conn = std::make_shared<Connection>(socket);
            conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
            conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
        }
    };
    mLocalServer.newConnection().connect(onNewConnection);
    mRemoteServer.newConnection().connect(onNewConnection);
    mServerConnection->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    mServerConnection->disconnected().connect([this](Connection*) { mSentHandshake = false; restartServerTimer(); });
    mServerConnection->connected().connect([this](Connection *conn) {
            warning() << "Connected to" << conn->client()->peerString();
            sendHandshake();
        });

    mServerConnection->error().connect([this](Connection*) { mSentHandshake = false; restartServerTimer(); });

    mServerTimer.timeout().connect([this](Timer *) { reconnectToServer(); });
    mJobAnnouncementTimer.timeout().connect([this](Timer *) { announceJobs(); });
}

Daemon::~Daemon()
{
    Console::cleanup();
}

bool Daemon::init(const Options &options)
{
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
        error() << "Can't seem to listen on" << mOptions.socketFile;
        return false;
    }

    if (!mRemoteServer.listen(mOptions.port)) {
        error() << "Can't seem to listen on" << mOptions.port;
        return false;
    }

    if (options.discoveryPort) {
        mDiscoverySocket.reset(new SocketClient);
        mDiscoverySocket->bind(options.discoveryPort);
        mDiscoverySocket->readyReadFrom().connect([this](const SocketClient::SharedPtr &, const String &ip, uint16_t port, Buffer &&data) {
                if (port == mOptions.discoveryPort)
                    onDiscoverySocketReadyRead(std::forward<Buffer>(data), ip);
            });
    }

    mOptions = options;
    mExplicitServer = !mOptions.serverHost.isEmpty();
    reconnectToServer();

    return true;
}

void Daemon::onNewMessage(Message *message, Connection *conn)
{
    auto connection = conn->shared_from_this();
    switch (message->messageId()) {
    case ClientJobMessage::MessageId:
        handleClientJobMessage(static_cast<ClientJobMessage*>(message), connection);
        break;
    case QuitMessage::MessageId:
        warning() << "Quitting by request";
        EventLoop::eventLoop()->quit();
        break;
    case ServerJobAnnouncementMessage::MessageId:
        handleServerJobAnnouncementMessage(static_cast<ServerJobAnnouncementMessage*>(message), connection);
        break;
    case CompilerMessage::MessageId:
        handleCompilerMessage(static_cast<CompilerMessage*>(message), connection);
        break;
    case CompilerRequestMessage::MessageId:
        handleCompilerRequestMessage(static_cast<CompilerRequestMessage*>(message), connection);
        break;
    case DaemonJobRequestMessage::MessageId:
        handleDaemonJobRequestMessage(static_cast<DaemonJobRequestMessage*>(message), connection);
        break;
    case DaemonListMessage::MessageId:
        handleDaemonListMessage(static_cast<DaemonListMessage*>(message), connection);
        break;
    default:
        error() << "Unexpected message" << message->messageId();
        break;
    }
}

void Daemon::handleClientJobMessage(ClientJobMessage *msg, const std::shared_ptr<Connection> &conn)
{
    debug() << "Got localjob" << msg->arguments() << msg->environ() << msg->cwd();

    List<String> env = msg->environ();
    assert(!env.contains("PLAST=1"));
    env.append("PLAST=1");
    std::shared_ptr<Compiler> compiler = Compiler::compiler(msg->arguments().first());
    if (!compiler) {
        conn->send(ClientJobResponseMessage());
        return;
    }
    std::shared_ptr<LocalJob> localJob = std::make_shared<LocalJob>(msg->arguments(), env, msg->cwd(), compiler, conn);
    if (localJob->arguments.mode != CompilerArgs::Compile) {
        conn->send(ClientJobResponseMessage());
        return;
    }
    mLocalJobsByLocalConnection[conn] = localJob;
    addLocalJob(LocalJob::PendingPreprocessing, localJob);
    startJobs();
}

void Daemon::handleServerJobAnnouncementMessage(ServerJobAnnouncementMessage *message, const std::shared_ptr<Connection> &conn)
{

}

void Daemon::handleCompilerMessage(CompilerMessage *message, const std::shared_ptr<Connection> &connection)
{
    assert(message->isValid());
    if (!message->writeFiles(mOptions.cacheDir)) {
        error() << "Couldn't write files to" << mOptions.cacheDir;
    }
}

void Daemon::handleCompilerRequestMessage(CompilerRequestMessage *message, const std::shared_ptr<Connection> &connection)
{
    std::shared_ptr<Compiler> compiler = Compiler::compilerBySha256(message->sha256());
    if (compiler) {
        connection->send(CompilerMessage(message->sha256()));
    } else {
        error() << "I don't know nothing.";
    }
}

void Daemon::handleDaemonJobRequestMessage(DaemonJobRequestMessage *message, const std::shared_ptr<Connection> &connection)
{

}

void Daemon::handleDaemonListMessage(DaemonListMessage *message, const std::shared_ptr<Connection> &connection)
{
    for (const auto &host : message->hosts()) {
        std::shared_ptr<Connection> &conn = mHosts[host];
        if (!conn) {
            conn = std::make_shared<Connection>();
            conn->connectTcp(host.address, host.port);
        }
    }
}

void Daemon::reconnectToServer()
{
    if (mServerConnection->client()) {
        switch (mServerConnection->client()->state()) {
        case SocketClient::Connected:
            sendHandshake();
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
        sendHandshake();
    }
}

void Daemon::sendHandshake()
{
    if (!mSentHandshake) {
        mSentHandshake = true;
        mServerConnection->send(HandshakeMessage(Rct::hostName(), mOptions.port, mOptions.jobCount));
    }
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
    debug() << "process finished" << process << localJob;
    if (localJob) {
        assert(localJob->process == process);
        assert(localJob->flags & LocalJob::Compiling);
        mCompilingJobs.erase(localJob->position);
        localJob->localConnection->send(ClientJobResponseMessage(process->returnCode(), localJob->output));
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
        auto job = mPendingPreprocessJobs.takeFirst();
        job->process = new Process;
        assert(job->flags & LocalJob::PendingPreprocessing);
        removeLocalJob(job);
        List<String> args = job->arguments.arguments;
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
        // debug() << "Starting process" << arguments;
        // assert(!arguments.isEmpty());
        const Path compiler = Plast::resolveCompiler(args.first());
        if (compiler.isEmpty()) {
            job->localConnection->send(ClientJobResponseMessage());
            mLocalJobsByLocalConnection.remove(job->localConnection);
            error() << "Can't resolve compiler for" << args.first();
            continue;
        }
        Process *process = new Process;
        mPreprocessJobsByProcess[process] = job;
        EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));

        process->setCwd(job->cwd);
        addLocalJob(LocalJob::Preprocessing, job);
        process->finished().connect([this](Process *proc) {
                auto job = mPreprocessJobsByProcess.take(proc);
                if (job) {
                    removeLocalJob(job);
                    const String err = proc->readAllStdErr();
                    if (!err.isEmpty())
                        job->output.append(Output({ Output::StdErr, err }));
                    if (proc->returnCode() != 0) {
                        job->localConnection->send(ClientJobResponseMessage());
                        mLocalJobsByLocalConnection.remove(job->localConnection);
                    } else {
                        job->preprocessed = proc->readAllStdOut();
                        addLocalJob(LocalJob::PendingCompiling, job);
                        EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));
                    }
                }
            });
    }

    if (!mOptions.flags & Options::NoLocalJobs) {
        while (mLocalCompileJobsByProcess.size() + mRemoteJobsByProcess.size() < mOptions.jobCount
               && !mPendingCompileJobs.isEmpty()) {
            auto job = mPendingCompileJobs.takeFirst();
            removeLocalJob(job);
            String err;
            addLocalJob(LocalJob::Compiling, job);
            job->process = startProcess(job->arguments.arguments, job->environ, job->cwd, &err);
            if (!job->process) {
                removeLocalJob(job);
                job->localConnection->send(ClientJobResponseMessage());
                mLocalJobsByLocalConnection.remove(job->localConnection);
                assert(job.use_count() == 1);
            }
        }
        // ### start compiling our jobs that are being handled by other daemons
    }
    if (!mJobAnnouncementTimer.isRunning())
        mJobAnnouncementTimer.restart(100, Timer::SingleShot);
}

void Daemon::announceJobs()
{
    if (mServerConnection->isConnected()) {
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
        if (!announcements.isEmpty()) {
            mServerConnection->send(DaemonJobAnnouncementMessage(announcements));
        }
    }
}

void Daemon::onConnectionDisconnected(Connection *conn)
{
    debug() << "Lost connection" << conn;
    if (std::shared_ptr<LocalJob> job = mLocalJobsByLocalConnection.take(conn->shared_from_this())) {
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
    }
}

Process *Daemon::startProcess(const List<String> &arguments, const List<String> &environ, const Path &cwd, String *err)
{
    debug() << "Starting process" << arguments;
    assert(!arguments.isEmpty());
    const Path compiler = Plast::resolveCompiler(arguments.first());
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
    }
}

void Daemon::handleConsoleCompletion(const String& string, int, int,
                                     String &common, List<String> &candidates)
{
    static const List<String> cands = List<String>() << "jobs" << "quit";
    auto res = Console::tryComplete(string, cands);
    // error() << res.text << res.candidates;
    common = res.text;
    candidates = res.candidates;
}

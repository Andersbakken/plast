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

Daemon::Daemon()
    : mFirstLocalJob(0), mLastLocalJob(0), mExplicitServer(false), mSentHandshake(false)
{
    Console::init("plastd> ",
                  std::bind(&Daemon::handleConsoleCommand, this, std::placeholders::_1),
                  std::bind(&Daemon::handleConsoleCompletion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    const auto onNewConnection = [this](SocketServer *server) {
        while (true) {
            auto socket = server->nextConnection();
            if (!socket)
                break;
            Connection *conn = new Connection(socket);
            conn->disconnected().connect(std::bind(&Daemon::onConnectionDisconnected, this, std::placeholders::_1));
            conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
        }
    };
    mLocalServer.newConnection().connect(onNewConnection);
    mRemoteServer.newConnection().connect(onNewConnection);
    mServerConnection.newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    mServerConnection.disconnected().connect([this](Connection*) { mSentHandshake = false; restartServerTimer(); });
    mServerConnection.connected().connect([this](Connection *) {
            if (!mSentHandshake) {
                mSentHandshake = true;
                mServerConnection.send(HandshakeMessage(Rct::hostName(), mOptions.jobCount));
            }
        });

    mServerConnection.error().connect([this](Connection*) { mSentHandshake = false; restartServerTimer(); });

    mServerTimer.timeout().connect([this](Timer *) { reconnectToServer(); });
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

void Daemon::onNewMessage(Message *message, Connection *connection)
{
    switch (message->messageId()) {
    case LocalJobMessage::MessageId:
        handleLocalJobMessage(static_cast<LocalJobMessage*>(message), connection);
        break;
    case QuitMessage::MessageId:
        warning() << "Quitting by request";
        EventLoop::eventLoop()->quit();
        break;
    default:
        error() << "Unexpected message" << message->messageId();
        break;
    }
}

void Daemon::handleLocalJobMessage(LocalJobMessage *msg, Connection *conn)
{
    debug() << "Got localjob" << msg->arguments() << msg->environ() << msg->cwd();

    List<String> env = msg->environ();
    assert(!env.contains("PLAST=1"));
    env.append("PLAST=1");
    LocalJob *localJob = new LocalJob(msg->arguments(), env, msg->cwd(), conn);
    Rct::LinkedList::insert(localJob, mFirstLocalJob, mLastLocalJob, mLastLocalJob);
    mLocalJobsByLocalConnection[conn] = localJob;
    conn->disconnected().connect([this](Connection *c) {
            LocalJob *job = mLocalJobsByLocalConnection.take(c);
            if (job) {
                Rct::LinkedList::remove(job, mFirstLocalJob, mLastLocalJob);
                if (job->remoteConnection)
                    mLocalJobsByRemoteConnection.remove(job->remoteConnection);
                if (job->process) {
                    job->process->kill();
                    mLocalJobsByProcess.remove(job->process);
                }
            }
        });
    startJobs();
}

void Daemon::reconnectToServer()
{
    if (mServerConnection.client()) {
        switch (mServerConnection.client()->state()) {
        case SocketClient::Connected:
            if (!mSentHandshake) {
                mSentHandshake = true;
                mServerConnection.send(HandshakeMessage(Rct::hostName(), mOptions.jobCount));
            }
            return;
        case SocketClient::Connecting:
            restartServerTimer();
            return;
        case SocketClient::Disconnected:
            break;
        }
    }

    error() << "Trying to connect" << mOptions.serverHost << mOptions.serverPort;
    if (mOptions.serverHost.isEmpty()) {
        mDiscoverySocket->writeTo("255.255.255.255", mOptions.discoveryPort, "?");
    } else if (!mServerConnection.connectTcp(mOptions.serverHost, mOptions.serverPort)) {
        restartServerTimer();
    } else if (mServerConnection.client()->state() == SocketClient::Connected) {
        mSentHandshake = true;
        mServerConnection.send(HandshakeMessage(Rct::hostName(), mOptions.jobCount));
    } else {
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
        if (!mExplicitServer && !mServerConnection.isConnected()) {
            deserializer >> mOptions.serverHost >> mOptions.serverPort;
            warning() << "found server" << mOptions.serverHost << mOptions.serverPort;
            reconnectToServer();
        }
        break;
    case 'S':
        if (!mExplicitServer && !mServerConnection.isConnected()) {
            deserializer >> mOptions.serverPort;
            mOptions.serverHost = ip;
            warning() << "found server" << mOptions.serverHost << mOptions.serverPort;
            reconnectToServer();
        }
        break;
    case '?':
        if (mServerConnection.isConnected()) {
            String packet;
            {
                Serializer serializer(packet);
                serializer << 's' << mServerConnection.client()->peerName() << mOptions.serverPort;
            }
            warning() << "telling" << ip << "about server" << mServerConnection.client()->peerName() << mOptions.serverPort;
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
static inline bool addOutput(Process *process, Hash<Process*, T*> &hash,
                             Output::Type type, const String &text)
{
    if (T *t = hash.value(process)) {
        t->output.append(Output({type, text}));
        return true;
    }
    return false;
}

void Daemon::onProcessReadyReadStdOut(Process *process)
{
    const String out = process->readAllStdOut();
    debug() << "ready read stdout" << process << out;
    if (!addOutput(process, mLocalJobsByProcess, Output::StdOut, out))
        addOutput(process, mRemoteJobsByProcess, Output::StdOut, out);
}

void Daemon::onProcessReadyReadStdErr(Process *process)
{
    const String out = process->readAllStdErr();
    debug() << "ready read stderr" << process << out;
    if (!addOutput(process, mLocalJobsByProcess, Output::StdErr, out))
        addOutput(process, mRemoteJobsByProcess, Output::StdErr, out);
}

void Daemon::onProcessFinished(Process *process)
{
    LocalJob *localJob = mLocalJobsByProcess.take(process);
    debug() << "process finished" << process << localJob;
    if (localJob) {
        assert(localJob->process == process);
        localJob->localConnection->send(LocalJobResponseMessage(process->returnCode(), localJob->output));
        mLocalJobsByLocalConnection.remove(localJob->localConnection);
        Rct::LinkedList::remove(localJob, mFirstLocalJob, mLastLocalJob);
        delete localJob;
    }
    EventLoop::deleteLater(process);
    EventLoop::eventLoop()->callLater(std::bind(&Daemon::startJobs, this));
}

void Daemon::startJobs()
{
    debug() << "startJobs" << mOptions.jobCount << mLocalJobsByProcess.size() << mRemoteJobsByProcess.size();
    while (mOptions.jobCount > (mLocalJobsByProcess.size() + mRemoteJobsByProcess.size())) {
        if (mLocalJobsByLocalConnection.size() > mLocalJobsByProcess.size()) {
            LocalJob *job = mFirstLocalJob;
            while (job && job->process)
                job = job->next;
            assert(job);
            debug() << "Found job" << job->arguments.sourceFiles;
            String err;
            job->process = startProcess(job->arguments.arguments, job->environ, job->cwd, &err);
            assert(job->process);
            mLocalJobsByProcess[job->process] = job;
        } else {
            break;
        }
    }
}

void Daemon::onConnectionDisconnected(Connection *conn)
{
    debug() << "Lost connection" << conn;
    EventLoop::deleteLater(conn);
    if (LocalJob *job = mLocalJobsByLocalConnection.take(conn)) {
        Rct::LinkedList::remove(job, mFirstLocalJob, mLastLocalJob);
        if (job->process) {
            assert(!job->remoteConnection);
            mLocalJobsByProcess.remove(job->process);
            job->process->kill();
        } else {
            // need to tell remote connection that we're no longer interested
        }
        delete job;
        return;
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
    process->finished().connect(std::bind(&Daemon::onProcessFinished, this, std::placeholders::_1));
    process->readyReadStdOut().connect(std::bind(&Daemon::onProcessReadyReadStdOut, this, std::placeholders::_1));
    process->readyReadStdErr().connect(std::bind(&Daemon::onProcessReadyReadStdErr, this, std::placeholders::_1));
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
        for (LocalJob *job = mFirstLocalJob; job; job = job->next) {
            printf("Job: %s Received: %s\n",
                   String::join(job->arguments.sourceFiles, ", ").constData(),
                   String::formatTime(job->received).constData());
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

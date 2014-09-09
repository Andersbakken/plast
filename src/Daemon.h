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

#ifndef Daemon_h
#define Daemon_h

#include <rct/SocketServer.h>
#include <rct/Connection.h>
#include <rct/Message.h>
#include <rct/Timer.h>
#include <rct/Hash.h>
#include <rct/Process.h>
#include "Plast.h"
#include "Console.h"
#include "ClientJobMessage.h"
#include "ClientJobResponseMessage.h"
#include "CompilerMessage.h"
#include "CompilerRequestMessage.h"
#include "DaemonListMessage.h"
#include "HandshakeMessage.h"
#include "JobAnnouncementMessage.h"
#include "JobDiscardedMessage.h"
#include "JobMessage.h"
#include "JobRequestMessage.h"
#include "JobResponseMessage.h"
#include "QuitMessage.h"
#include "CompilerCache.h"

class Daemon
{
public:
    Daemon();
    ~Daemon();
    struct Options {
        Path socketFile;
        uint16_t serverPort, port, discoveryPort;
        String serverHost;
        Path cacheDir;
        int jobCount, preprocessCount;
        enum Flag {
            None = 0x0,
            NoLocalJobs = 0x1
        };
        unsigned int flags;
    };
    bool init(const Options &options);
    const Options &options() const { return mOptions; }

private:
    void handleConsoleCommand(const String &string);
    void handleConsoleCompletion(const String &string, int start, int end, String &common, List<String> &candidates);

    void restartServerTimer();
    void onNewMessage(Message *message, Connection *connection);
    void handleClientJobMessage(const ClientJobMessage *msg, const std::shared_ptr<Connection> &conn);
    void handleCompilerMessage(const CompilerMessage* message, const std::shared_ptr<Connection> &connection);
    void handleCompilerRequestMessage(const CompilerRequestMessage *message, const std::shared_ptr<Connection> &connection);
    void handleJobRequestMessage(const JobRequestMessage *message, const std::shared_ptr<Connection> &connection);
    void handleJobMessage(const JobMessage *message, const std::shared_ptr<Connection> &connection);
    void handleJobResponseMessage(const JobResponseMessage *message, const std::shared_ptr<Connection> &connection);
    void handleJobDiscardedMessage(const JobDiscardedMessage *message, const std::shared_ptr<Connection> &connection);
    void handleDaemonListMessage(const DaemonListMessage *message, const std::shared_ptr<Connection> &connection);
    void handleHandshakeMessage(const HandshakeMessage *message, const std::shared_ptr<Connection> &connection);
    void handleJobAnnouncementMessage(const JobAnnouncementMessage *message, const std::shared_ptr<Connection> &connection);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data, const String &ip);

    void onConnectionDisconnected(Connection *conn);
    void onCompileProcessReadyReadStdOut(Process *process);
    void onCompileProcessReadyReadStdErr(Process *process);
    void onCompileProcessFinished(Process *process);
    void startJobs();
    void sendHandshake(const std::shared_ptr<Connection> &conn);
    struct Peer;
    void announceJobs(Peer *peer = 0);
    void fetchJobs(Peer *peer = 0);
    void checkJobRequestTimeout();

    struct Job {
        Job(const std::shared_ptr<CompilerArgs> &args, const Path &resolved, const List<String> &env, const Path &dir,
            std::shared_ptr<Compiler> &c, const std::shared_ptr<Connection> &localConn = std::shared_ptr<Connection>())
            : received(Rct::monoMs()), arguments(args), resolvedCompiler(resolved),
              environ(env), cwd(dir), process(0), flags(None), id(0), compiler(c), localConnection(localConn)
        {
        }

        uint64_t received;
        std::shared_ptr<CompilerArgs> arguments;
        Path resolvedCompiler;
        List<String> environ;
        Path cwd;
        List<Output> output;
        Process *process;
        enum Flag {
            None = 0x00,
            Announced = 0x01,
            PendingPreprocessing = 0x02,
            Preprocessing = 0x04,
            PendingCompiling = 0x08,
            Compiling = 0x10,
            FromRemote = 0x20, // this job originated elsewhere
            Remote = 0x40, // remote machine is handling it
            StateMask = Preprocessing|PendingPreprocessing|PendingCompiling|Compiling
        };

        Path tempObjectFile;
        String preprocessed;
        uint32_t flags;
        uint64_t id; // only set if flags & FromRemote
        LinkedList<std::shared_ptr<Job> >::iterator position;
        std::shared_ptr<Compiler> compiler;
        std::shared_ptr<Connection> localConnection, remoteConnection;
    };

    void addJob(Job::Flag flag, const std::shared_ptr<Job> &job);
    void removeJob(const std::shared_ptr<Job> &job);

    LinkedList<std::shared_ptr<Job> > mPendingPreprocessJobs, mPendingCompileJobs, mPreprocessingJobs, mCompilingJobs;

    Hash<std::shared_ptr<Connection>, std::shared_ptr<Job> > mJobsByLocalConnection;
    Hash<std::shared_ptr<Connection>, Hash<uint64_t, std::shared_ptr<Job> > > mJobsByRemoteConnection;
    Hash<Process*, std::shared_ptr<Job> > mCompileJobsByProcess, mPreprocessJobsByProcess;
    Hash<uint64_t, uint64_t> mOutstandingJobRequests; // jobid: Rct::monoMs
    Timer mOutstandingJobRequestsTimer;
    uint64_t mNextJobId;

    struct Peer {
        std::shared_ptr<Connection> connection;
        Host host;
        Set<String> announced, jobsAvailable;
    };
    Hash<Host, Peer*> mPeersByHost;
    Hash<std::shared_ptr<Connection>, Peer*> mPeersByConnection;

    Set<std::shared_ptr<Connection> > mConnections;
    bool mExplicitServer;
    Options mOptions;
    SocketServer mLocalServer, mRemoteServer;
    std::shared_ptr<SocketClient> mDiscoverySocket;
    std::shared_ptr<Connection> mServerConnection;
    std::shared_ptr<CompilerCache> mCompilerCache;
    Timer mServerTimer;
};

#endif

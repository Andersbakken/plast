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
#include <rct/EmbeddedLinkedList.h>
#include "Plast.h"
#include "Console.h"
#include "ClientJobMessage.h"
#include "ClientJobResponseMessage.h"
#include "CompilerMessage.h"
#include "CompilerRequestMessage.h"
#include "MonitorMessage.h"
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
        int jobCount, preprocessCount, rescheduleTimeout;
        enum Flag {
            None = 0x0,
            NoLocalJobs = 0x1
        };
        unsigned int flags;
    };
    bool init(const Options &options);
    const Options &options() const { return mOptions; }

private:
    struct Job {
        Job(const std::shared_ptr<CompilerArgs> &args, const Path &resolved, const List<String> &env, const Path &dir,
            std::shared_ptr<Compiler> &c, const std::shared_ptr<Connection> &localConn = std::shared_ptr<Connection>())
            : received(Rct::monoMs()), arguments(args), resolvedCompiler(resolved),
              environ(env), cwd(dir), process(0), flags(None), id(0), compiler(c), localConnection(localConn)
        {
        }

        unsigned long long received;
        std::shared_ptr<CompilerArgs> arguments;
        Path resolvedCompiler;
        List<String> environ;
        Path cwd;
        List<Output> output;
        Process *process;
        enum Flag {
            None = 0x00,
            PendingPreprocessing = 0x01,
            Preprocessing = 0x02,
            PendingCompiling = 0x04,
            Compiling = 0x08,
            FromRemote = 0x10, // this job originated elsewhere
            Remote = 0x20, // remote machine is handling it
            StateMask = Preprocessing|PendingPreprocessing|PendingCompiling|Compiling
        };

        Path tempObjectFile;
        String preprocessed;
        uint32_t flags;
        uint64_t id; // only set if flags & FromRemote
        LinkedList<std::shared_ptr<Job> >::iterator position;
        std::shared_ptr<Compiler> compiler;
        std::shared_ptr<Connection> localConnection, source;
        Set<std::shared_ptr<Connection> > remoteConnections;
    };

    struct Peer {
        std::shared_ptr<Connection> connection;
        Host host;
        Set<String> announced, jobsAvailable;
    };
    bool mAnnouncementDirty;

    void handleConsoleCommand(const String &string);
    void handleConsoleCompletion(const String &string, int start, int end, String &common, List<String> &candidates);

    void restartServerTimer();
    void onNewMessage(const std::shared_ptr<Message> &message, Connection *connection);
    void handleClientJobMessage(const std::shared_ptr<ClientJobMessage> &msg,
                                const std::shared_ptr<Connection> &conn);
    void handleCompilerMessage(const std::shared_ptr<CompilerMessage> &message,
                               const std::shared_ptr<Connection> &connection);
    void handleCompilerRequestMessage(const std::shared_ptr<CompilerRequestMessage> &message,
                                      const std::shared_ptr<Connection> &connection);
    void handleJobRequestMessage(const std::shared_ptr<JobRequestMessage> &message,
                                 const std::shared_ptr<Connection> &connection);
    void handleJobMessage(const std::shared_ptr<JobMessage> &message,
                          const std::shared_ptr<Connection> &connection);
    void handleJobResponseMessage(const std::shared_ptr<JobResponseMessage> &message,
                                  const std::shared_ptr<Connection> &connection);
    void handleJobDiscardedMessage(const std::shared_ptr<JobDiscardedMessage> &message,
                                   const std::shared_ptr<Connection> &connection);
    void handleDaemonListMessage(const std::shared_ptr<DaemonListMessage> &message,
                                 const std::shared_ptr<Connection> &connection);
    void handleHandshakeMessage(const std::shared_ptr<HandshakeMessage> &message,
                                const std::shared_ptr<Connection> &connection);
    void handleJobAnnouncementMessage(const std::shared_ptr<JobAnnouncementMessage> &message,
                                      const std::shared_ptr<Connection> &connection);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data, const String &ip);

    void onConnectionDisconnected(Connection *conn);
    void onCompileProcessFinished(Process *process);
    void startJobs();
    int startPreprocessingJobs();
    int startCompileJobs();
    void sendHandshake(const std::shared_ptr<Connection> &conn);
    void announceJobs(Peer *peer = 0);
    void fetchJobs(Peer *peer = 0);
    void checkJobRequestTimeout();
    void sendMonitorMessage(const String &message);

    void addJob(Job::Flag flag, const std::shared_ptr<Job> &job);
    void removeJob(const std::shared_ptr<Job> &job);
    void sendJobDiscardedMessage(const std::shared_ptr<Job> &job);
    std::shared_ptr<Job> removeRemoteJob(const std::shared_ptr<Connection> &conn, uint64_t id);
    uint64_t removeRemoteJob(const std::shared_ptr<Connection> &conn, const std::shared_ptr<Job> &job);

    LinkedList<std::shared_ptr<Job> > mPendingPreprocessJobs, mPendingCompileJobs, mPreprocessingJobs, mCompilingJobs;

    Hash<std::shared_ptr<Connection>, std::shared_ptr<Job> > mJobsByLocalConnection;
    struct RemoteJob {
        std::shared_ptr<Job> job;
        uint64_t startTime;
    };
    struct RemoteData {
        Hash<uint64_t, RemoteJob> byId;
        Hash<std::shared_ptr<Job>, uint64_t> byJob;
    };
    Hash<std::shared_ptr<Connection>, std::shared_ptr<RemoteData> > mJobsByRemoteConnection;
    Hash<Process*, std::shared_ptr<Job> > mCompileJobsByProcess, mPreprocessJobsByProcess;
    Hash<uint64_t, uint64_t> mOutstandingJobRequests; // jobid: Rct::monoMs
    Timer mOutstandingJobRequestsTimer;
    uint64_t mNextJobId;

    Hash<Host, Peer*> mPeersByHost;
    Hash<std::shared_ptr<Connection>, Peer*> mPeersByConnection;

    Set<std::shared_ptr<Connection> > mConnections;
    bool mExplicitServer;
    Options mOptions;
    SocketServer mLocalServer, mRemoteServer;
    std::shared_ptr<SocketClient> mDiscoverySocket;
    std::shared_ptr<Connection> mServerConnection;
    std::shared_ptr<CompilerCache> mCompilerCache;
    String mHostName;
    Timer mServerTimer;
};

#endif

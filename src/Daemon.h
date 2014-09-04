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
    void handleDaemonJobRequestMessage(const DaemonJobRequestMessage *message, const std::shared_ptr<Connection> &connection);
    void handleDaemonListMessage(const DaemonListMessage *message, const std::shared_ptr<Connection> &connection);
    void handleHandshakeMessage(const HandshakeMessage *message, const std::shared_ptr<Connection> &connection);
    void handleDaemonJobAnnouncementMessage(const DaemonJobAnnouncementMessage *message, const std::shared_ptr<Connection> &connection);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data, const String &ip);

    void onConnectionDisconnected(Connection *conn);
    void onCompileProcessReadyReadStdOut(Process *process);
    void onCompileProcessReadyReadStdErr(Process *process);
    void onCompileProcessFinished(Process *process);
    void startJobs();
    void sendHandshake(const std::shared_ptr<Connection> &conn);
    void announceJobs();
    void checkJobRequstTimeout();

    Process *startProcess(const Path &compiler, const List<String> &arguments,
                          const List<String> &environ, const Path &cwd, String *error);

    struct Job {
        Job(const List<String> &args, const Path &resolved, const List<String> &env, const Path &dir,
                 std::shared_ptr<Compiler> &c, const std::shared_ptr<Connection> &conn)
            : received(time(0)), arguments(CompilerArgs::create(args)), resolvedCompiler(resolved),
              environ(env), cwd(dir), process(0), flags(0), compiler(c), localConnection(conn)
        {
        }

        time_t received;
        CompilerArgs arguments;
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
            StateMask = Preprocessing|PendingPreprocessing|PendingCompiling|Compiling
        };

        String preprocessed;
        uint32_t flags;
        LinkedList<std::shared_ptr<Job> >::iterator position;
        std::shared_ptr<Compiler> compiler;
        std::shared_ptr<Connection> localConnection, remoteConnection;
    };

    void addJob(Job::Flag flag, const std::shared_ptr<Job> &job)
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

    void removeJob(const std::shared_ptr<Job> &job)
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
            break;
        default:
            assert(0);
        }
    }

    LinkedList<std::shared_ptr<Job> > mPendingPreprocessJobs, mPendingCompileJobs, mPreprocessingJobs, mCompilingJobs;

    Hash<std::shared_ptr<Connection>, std::shared_ptr<Job> > mJobsByLocalConnection, mJobsByRemoteConnection;
    Hash<Process*, std::shared_ptr<Job> > mCompileJobsByProcess, mPreprocessJobsByProcess;
    Hash<uint64_t, time_t> mOutstandingJobRequests;
    uint64_t mNextJobId;

    struct Peer {
        std::shared_ptr<Connection> connection;
        Host host;
    };
    Hash<Host, Peer*> mPeersByHost;
    Hash<std::shared_ptr<Connection>, Peer*> mPeersByConnection;

    Hash<String, int> mLastAnnouncements;

    Set<std::shared_ptr<Connection> > mConnections;
    bool mExplicitServer, mAnnouncementPending;
    Options mOptions;
    SocketServer mLocalServer, mRemoteServer;
    std::shared_ptr<SocketClient> mDiscoverySocket;
    std::shared_ptr<Connection> mServerConnection;
    Timer mServerTimer;
};

#endif

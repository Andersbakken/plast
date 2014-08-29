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
#include "Compiler.h"
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
    void handleConsoleCompletion(const String& string, int start, int end, String& common, List<String>& candidates);

    void restartServerTimer();
    void onNewMessage(Message *message, Connection *connection);
    void handleClientJobMessage(ClientJobMessage *msg, Connection *conn);
    void handleServerJobAnnouncementMessage(ServerJobAnnouncementMessage *message, Connection *conn);
    void handleCompilerMessage(CompilerMessage* message, Connection *connection);
    void handleCompilerRequestMessage(CompilerRequestMessage *message, Connection *connection);
    void handleDaemonJobRequestMessage(DaemonJobRequestMessage *message, Connection *connection);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data, const String &ip);

    void onConnectionDisconnected(Connection *conn);
    void onProcessReadyReadStdOut(Process *process);
    void onProcessReadyReadStdErr(Process *process);
    void onProcessFinished(Process *process);
    void startJobs();
    void sendHandshake();
    void announceJobs();

    Process *startProcess(const List<String> &arguments, const List<String> &environ,
                          const Path &cwd, String *error);

    struct LocalJob {
        LocalJob(const List<String> &args, const List<String> &env, const Path &dir,
                 std::shared_ptr<Compiler> &c, Connection *conn)
            : received(time(0)), arguments(CompilerArgs::create(args)), environ(env), cwd(dir),
              process(0), flags(0), compiler(c), localConnection(conn), remoteConnection(0), next(0), prev(0)
        {
        }

        time_t received;
        CompilerArgs arguments;
        List<String> environ;
        Path cwd;
        List<Output> output;
        Process *process;
        enum Flag {
            None = 0x0,
            Announced = 0x1
        };

        String preprocessed;
        uint32_t flags;
        std::shared_ptr<Compiler> compiler;
        Connection *localConnection, *remoteConnection;
        LocalJob *next, *prev;
    };
    LocalJob *mFirstPendingPreprocessLocalJob, *mLastPendingPreprocessLocalJob; // these jobs are ready to be preprocessed
    LocalJob *mFirstPendingCompileLocalJob, *mLastPendingCompileLocalJob; // these jobs are ready to be compiled

    Hash<Connection*, LocalJob*> mLocalJobsByLocalConnection, mLocalJobsByRemoteConnection;
    Hash<Process*, LocalJob*> mLocalCompileJobsByProcess, mLocalPreprocessJobsByProcess;

    struct RemoteJob {
        List<String> arguments, environ;
        List<Output> output;
        String source;
        Connection *connection;
        Process *process;
    };

    List<RemoteJob*> mRemoteJobList;
    Hash<Connection*, RemoteJob*> mRemoteJobsByConnection;
    Hash<Process*, RemoteJob*> mRemoteJobsByProcess;

    bool mExplicitServer;
    Options mOptions;
    SocketServer mLocalServer, mRemoteServer;
    Connection mServerConnection;
    bool mSentHandshake;
    std::shared_ptr<SocketClient> mDiscoverySocket;
    Timer mServerTimer, mJobAnnouncementTimer;
};

#endif

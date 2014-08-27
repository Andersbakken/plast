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
        int jobCount;
    };
    bool init(const Options &options);
    const Options &options() const { return mOptions; }

private:
    void handleConsoleCommand(const String &string);
    void handleConsoleCompletion(const String& string, int start, int end, String& common, List<String>& candidates);

    void restartServerTimer();
    void onNewMessage(Message *message, Connection *connection);
    void handleLocalJobMessage(LocalJobMessage *msg, Connection *conn);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data);

    void onConnectionDisconnected(Connection *conn);
    void onProcessReadyReadStdOut(Process *process);
    void onProcessReadyReadStdErr(Process *process);
    void onProcessFinished(Process *process);
    void startJobs();
    Process *startProcess(const List<String> &arguments, const List<String> &environ,
                          const Path &cwd, String *error);

    struct LocalJob {
        LocalJob(const List<String> &args, const List<String> &env, const Path &dir, Connection *conn)
            : type(Link), arguments(args), environ(env), cwd(dir), process(0), localConnection(conn), remoteConnection(0), next(0), prev(0)
        {
            for (const String &arg : arguments) {
                if (arg == "-c") {
                    type = Compile;
                } else if (arg == "-E") {
                    type = Preprocess;
                    break;
                }
            }
        }
        enum Type {
            Compile,
            Preprocess,
            Link
        } type;
        List<String> arguments, environ;
        Path cwd;
        List<Output> output;
        Process *process;
        Connection *localConnection, *remoteConnection;
        LocalJob *next, *prev;
    };
    LocalJob *mFirstLocalJob, *mLastLocalJob;

    Hash<Connection*, LocalJob*> mLocalJobsByLocalConnection, mLocalJobsByRemoteConnection;
    Hash<Process*, LocalJob*> mLocalJobsByProcess;

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
    std::shared_ptr<SocketClient> mDiscoverySocket;
    Timer mServerTimer;
};

#endif

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

class Daemon
{
public:
    Daemon();
    struct Options {
        Path socketFile;
        uint16_t serverPort, port, discoveryPort;
        String serverHost;
        int jobCount;
    };
    bool init(const Options &options);
    const Options &options() const { return mOptions; }
private:
    void restartServerTimer();
    void onNewMessage(Message *message, Connection *connection);
    void handleLocalJobMessage(LocalJobMessage *msg, Connection *conn);
    void reconnectToServer();
    void onDiscoverySocketReadyRead(Buffer &&data);

    void onProcessReadyReadStdOut(Process *process);
    void onProcessReadyReadStdErr(Process *process);
    void onProcessFinished(Process *process);
    void startJobs();

    struct LocalJob {
        List<String> arguments;
        Process *process;
        Connection *localConnection, *remoteConnection;
        LocalJob *next, *prev;
    };
    LocalJob *mFirstLocalJob, *mLastLocalJob;

    Hash<Connection*, LocalJob*> mLocalJobsByLocalConnection, mLocalJobsByRemoteConnection;
    Hash<Process*, LocalJob*> mLocalJobsByProcess;

    struct RemoteJob {
        List<String> arguments;
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
    SocketClient mDiscoverySocket;
    Timer mServerTimer;
};

#endif

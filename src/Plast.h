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

#ifndef Plast_h
#define Plast_h

#include <rct/Path.h>

namespace Plast {
bool init();
Path defaultSocketFile();
enum {
    DefaultServerPort = 5166,
    DefaultDaemonPort = 5167,
    DefaultDiscoveryPort = 5168,
    DefaultHttpPort = 5169
};

enum {
    HandshakeMessageId = 100,
    DaemonListMessageId,
    ClientJobMessageId,
    ClientJobResponseMessageId,
    QuitMessageId,
    JobAnnouncementMessageId,
    CompilerMessageId,
    CompilerRequestMessageId,
    JobRequestMessageId,
    JobMessageId,
    JobResponseMessageId,
    JobDiscardedMessageId,
    MonitorMessageId
};
};

#endif

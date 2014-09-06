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

#ifndef DaemonListMessage_h
#define DaemonListMessage_h

#include <rct/Message.h>

class DaemonListMessage : public Message
{
public:
    enum { MessageId = DaemonListMessageId };
    DaemonListMessage(const Set<Host> &hosts = Set<Host>())
        : Message(MessageId), mHosts(hosts)
    {}

    const Set<Host> &hosts() const { return mHosts; }
    virtual void encode(Serializer &serializer) const { serializer << mHosts; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mHosts; }
private:
    Set<Host> mHosts;
};

#endif

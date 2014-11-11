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

#ifndef MonitorMessage_h
#define MonitorMessage_h

#include <rct/Message.h>
#include "Host.h"
#include "Plast.h"

class MonitorMessage : public Message
{
public:
    enum { MessageId = Plast::MonitorMessageId };
    enum Type {
        Start,
        End
    };
    MonitorMessage(Type type = Start, const Host &peer = Host(), void *id = 0,
                   const String &arguments = String(), const Path &path = Path())
        : Message(MessageId), mType(type), mPeer(peer), mId(id), mArguments(arguments), mPath(path)
    {
    }

    static MonitorMessage createStart(const Host &peer, void *id, const String &arguments, const Path &path)
    {
        return MonitorMessage(Start, peer, id, arguments, path);
    }

    static MonitorMessage createEnd(void *id)
    {
        return MonitorMessage(End, Host(), id);
    }

    Type type() const { return mType; }
    Host peer() const { return mPeer; }
    void *id() const { return mId; }
    String arguments() const { return mArguments; }
    Path path() const { return mPath; }

    String toString() const
    {
        switch (mType) {
        case Start:
            return String::format<128>("{\"type\":\"start\","
                                       "\"peer\":\"%s\","
                                       "\"path\":\"%s\","
                                       "\"arguments\":\"%s\","
                                       "\"id\":\"%p\"}",
                                       mPeer.toString().constData(),
                                       mPath.constData(),
                                       mArguments.constData(),
                                       mId);
        case End:
            return String::format<128>("{\"type\":\"end\",\"id\":\"%p\"}", mId);
        }
        return String();
    }

    virtual void encode(Serializer &serializer) const
    {
        serializer << static_cast<uint8_t>(mType) << mPeer
                   << mArguments << reinterpret_cast<unsigned long long>(mId) << mPath;
    }
    virtual void decode(Deserializer &deserializer)
    {
        uint8_t type;
        unsigned long long id;
        deserializer >> type >> mPeer >> mArguments >> id >> mPath;
        mType = static_cast<Type>(type);
        mId = reinterpret_cast<void*>(mId);
    }
private:
    Type mType;
    Host mPeer;
    void *mId;
    String mArguments;
    Path mPath;
};

#endif

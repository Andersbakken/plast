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

#ifndef HandshakeMessage_h
#define HandshakeMessage_h

#include <rct/Message.h>

class HandshakeMessage : public Message
{
public:
    enum { MessageId = HandshakeMessageId };
    HandshakeMessage(const String &f = String(), uint16_t port = 0, int c = -1)
        : Message(MessageId), mFriendlyName(f), mPort(port), mCapacity(c)
    {}

    String friendlyName() const { return mFriendlyName; }
    uint16_t port() const { return mPort; }
    int capacity() const { return mCapacity; }

    virtual void encode(Serializer &serializer) const { serializer << mFriendlyName << mPort << mCapacity; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mFriendlyName >> mPort >> mCapacity; }
private:
    String mFriendlyName;
    uint16_t mPort;
    int mCapacity;
};

#endif

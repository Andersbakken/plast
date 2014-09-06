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

#ifndef JobRequestMessage_h
#define JobRequestMessage_h

#include <rct/Message.h>

class JobRequestMessage : public Message
{
public:
    enum { MessageId = JobRequestMessageId };
    JobRequestMessage(uint64_t id = 0, const String &sha256 = String())
        : Message(MessageId), mId(id), mSha256(sha256)
    {}

    uint64_t id() const { return mId; }
    const String &sha256() const { return mSha256; }
    virtual void encode(Serializer &serializer) const { serializer << mId << mSha256; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mId >> mSha256; }
private:
    uint64_t mId;
    String mSha256;
};

#endif

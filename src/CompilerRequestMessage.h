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

#ifndef CompilerRequestMessage_h
#define CompilerRequestMessage_h

#include <rct/Message.h>

class CompilerRequestMessage : public Message
{
public:
    enum { MessageId = CompilerRequestMessageId };
    CompilerRequestMessage(const String &sha256 = String())
        : Message(MessageId), mSha256(sha256)
    {
    }

    const String &sha256() const { return mSha256; }
    virtual void encode(Serializer &serializer) const { serializer << mSha256; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mSha256; }
private:
    String mSha256;
};

#endif

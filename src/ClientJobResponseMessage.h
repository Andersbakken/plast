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

#ifndef ClientJobResponseMessage_h
#define ClientJobResponseMessage_h

#include <rct/Message.h>

class ClientJobResponseMessage : public Message
{
public:
    enum { MessageId = ClientJobResponseMessageId };

    ClientJobResponseMessage(int status = -1, const List<Output> &output = List<Output>())
        : Message(MessageId), mStatus(status), mOutput(output)
    {
    }

    int status() const { return mStatus; }
    const List<Output> &output() const { return mOutput; }
    void encode(Serializer &serializer) const { serializer << mStatus << mOutput; }
    void decode(Deserializer &deserializer) { deserializer >> mStatus >> mOutput; }
private:
    int mStatus;
    List<Output> mOutput;
};

#endif


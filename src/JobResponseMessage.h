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

#ifndef JobResponseMessage_h
#define JobResponseMessage_h

#include <rct/Message.h>
#include "Plast.h"

class JobResponseMessage : public Message
{
public:
    enum { MessageId = Plast::JobResponseMessageId };
    JobResponseMessage(uint64_t id = 0,
                       int status = -1,
                       const String &objectFileContents = String(),
                       const List<Output> &output = List<Output>())
        : Message(MessageId, Compressed), mId(id), mStatus(status), mObjectFileContents(objectFileContents), mOutput(output)
    {}

    uint64_t id() const { return mId; }
    int status() const { return mStatus; }
    const String &objectFileContents() const { return mObjectFileContents; }
    const List<Output> &output() const { return mOutput; }
    virtual void encode(Serializer &serializer) const { serializer << mId << mStatus << mObjectFileContents << mOutput; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mId >> mStatus >> mObjectFileContents >> mOutput; }
private:
    uint64_t mId;
    int mStatus;
    String mObjectFileContents;
    List<Output> mOutput;
};

#endif

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
#include "Plast.h"

class MonitorMessage : public Message
{
public:
    enum { MessageId = Plast::MonitorMessageId };
    MonitorMessage(const String &message = String())
        : Message(MessageId), mMessage(mMessage)
    {
    }

    const String &message() const { return mMessage; }
    virtual void encode(Serializer &serializer) const { serializer << mMessage; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mMessage; }
private:
    String mMessage;
};

#endif

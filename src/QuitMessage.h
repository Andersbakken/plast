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

#ifndef QuitMessage_h
#define QuitMessage_h

#include <rct/Message.h>
#include "Plast.h"

class QuitMessage : public Message
{
public:
    enum { MessageId = Plast::QuitMessageId };
    enum Mode {
        Quit,
        Rebuild
    };
    QuitMessage(Mode mode = Quit)
        : Message(MessageId), mMode(mode)
    {}

    Mode mode() const { return mMode; }

    virtual void encode(Serializer &serializer) const { serializer << static_cast<uint8_t>(mMode); }
    virtual void decode(Deserializer &deserializer) { uint8_t mode; deserializer >> mode; mMode = static_cast<Mode>(mode); }

private:
    Mode mMode;
};


#endif

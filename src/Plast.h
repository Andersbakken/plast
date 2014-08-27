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

#include <rct/Message.h>

class HandshakeMessage : public Message
{
public:
    enum { MessageId = 100 };
    HandshakeMessage(const String &h = String(), int c = -1)
        : Message(MessageId), mHostName(h), mCapacity(c)
    {}

    String hostName() const { return mHostName; }
    int capacity() const { return mCapacity; }

    virtual void encode(Serializer &serializer) const { serializer << mHostName << mCapacity; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mHostName >> mCapacity; }
private:
    String mHostName;
    int mCapacity;
};

class LocalJobMessage : public Message
{
public:
    enum { MessageId = HandshakeMessage::MessageId + 1 };

    LocalJobMessage(int argc = 0, char **argv = 0)
        : Message(MessageId)
    {
        mArguments.resize(argc);
        for (int i=0; i<argc; ++i)
            mArguments[i] = argv[i];
    }

    const List<String> &arguments() const { return mArguments; }

    virtual void encode(Serializer &serializer) const { serializer << mArguments; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mArguments; }
private:
    List<String> mArguments;
};

class LocalJobResponseMessage : public Message
{
public:
    enum { MessageId = LocalJobMessage::MessageId + 1 };

    struct Output;
    LocalJobResponseMessage(int status = -1, const List<Output> &output = List<Output>())
        : Message(MessageId), mStatus(status), mOutput(output)
    {
    }

    int status() const { return mStatus; }
    struct Output {
        enum Type {
            StdOut,
            StdErr
        };
        Type type;
        String text;
    };
    const List<Output> &output() const { return mOutput; }
    virtual void encode(Serializer &serializer) const;
    virtual void decode(Deserializer &deserializer);
private:
    int mStatus;
    List<Output> mOutput;
};


namespace Plast {
bool init();
}

#endif

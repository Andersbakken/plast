#ifndef HANDSHAKEMESSAGE_H
#define HANDSHAKEMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class HandshakeMessage : public Message
{
public:
    typedef std::shared_ptr<HandshakeMessage> SharedPtr;

    enum { MessageId = plast::HandshakeMessageId };

    HandshakeMessage() : Message(MessageId), mPort(0) {}
    HandshakeMessage(uint16_t port) : Message(MessageId), mPort(port) {}

    uint16_t port() const { return mPort; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    uint16_t mPort;
};

inline void HandshakeMessage::encode(Serializer& serializer) const
{
    serializer << mPort;
}

inline void HandshakeMessage::decode(Deserializer& deserializer)
{
    deserializer >> mPort;
}

#endif

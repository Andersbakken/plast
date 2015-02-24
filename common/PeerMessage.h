#ifndef PEERMESSAGE_H
#define PEERMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class PeerMessage : public Message
{
public:
    typedef std::shared_ptr<PeerMessage> SharedPtr;

    enum { MessageId = plast::PeerMessageId };

    PeerMessage() : Message(MessageId), mPort(0) {}
    PeerMessage(const String& name, uint16_t port = 0) : Message(MessageId), mName(name), mPort(port) {}

    void setName(const String& name) { mName = name; }
    void setPort(uint16_t port) { mPort = port; }

    String name() const { return mName; }
    uint16_t port() const { return mPort; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    String mName;
    uint16_t mPort;
};

inline void PeerMessage::encode(Serializer& serializer) const
{
    serializer << mName << mPort;
}

inline void PeerMessage::decode(Deserializer& deserializer)
{
    deserializer >> mName >> mPort;
}

#endif

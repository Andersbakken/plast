#ifndef HASJOBSMESSAGE_H
#define HASJOBSMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class HasJobsMessage : public Message
{
public:
    typedef std::shared_ptr<HasJobsMessage> SharedPtr;

    enum { MessageId = plast::HasJobsMessageId };

    HasJobsMessage() : Message(MessageId), mCount(0), mPort(0) {}
    HasJobsMessage(int count, uint16_t port = 0) : Message(MessageId), mCount(count), mPort(port) {}

    void setPeer(const String& peer) { mPeer = peer; }
    void setPort(uint16_t port) { mPort = port; }

    String peer() const { return mPeer; }
    uint16_t port() const { return mPort; }
    int count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    int mCount;
    String mPeer;
    uint16_t mPort;
};

inline void HasJobsMessage::encode(Serializer& serializer) const
{
    serializer << mCount << mPeer << mPort;
}

inline void HasJobsMessage::decode(Deserializer& deserializer)
{
    deserializer >> mCount >> mPeer >> mPort;
}

#endif

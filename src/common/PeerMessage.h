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
    PeerMessage(const String& name, uint16_t port = 0, uint32_t jobs = 0)
        : Message(MessageId), mName(name), mPort(port), mJobs(jobs)
    {
    }

    void setName(const String& name) { mName = name; }
    void setPort(uint16_t port) { mPort = port; }
    void setJobs(uint32_t jobs) { mJobs = jobs; }

    String name() const { return mName; }
    uint16_t port() const { return mPort; }
    uint32_t jobs() const { return mJobs; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    String mName;
    uint16_t mPort;
    uint32_t mJobs;
};

inline void PeerMessage::encode(Serializer& serializer) const
{
    serializer << mName << mPort << mJobs;
}

inline void PeerMessage::decode(Deserializer& deserializer)
{
    deserializer >> mName >> mPort >> mJobs;
}

#endif

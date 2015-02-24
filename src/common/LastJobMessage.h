#ifndef LASTJOBMESSAGE_H
#define LASTJOBMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class LastJobMessage : public Message
{
public:
    typedef std::shared_ptr<LastJobMessage> SharedPtr;

    enum { MessageId = plast::LastJobMessageId };

    LastJobMessage() : Message(MessageId), mCount(0) {}
    LastJobMessage(int count) : Message(MessageId), mCount(count) {}

    int count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    int mCount;
};

inline void LastJobMessage::encode(Serializer& serializer) const
{
    serializer << mCount;
}

inline void LastJobMessage::decode(Deserializer& deserializer)
{
    deserializer >> mCount;
}

#endif

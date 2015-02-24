#ifndef LASTJOBMESSAGE_H
#define LASTJOBMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class LastJobMessage : public Message
{
public:
    typedef std::shared_ptr<LastJobMessage> SharedPtr;

    enum { MessageId = plast::LastJobMessageId };

    LastJobMessage() : Message(MessageId), mCount(0), mHasMore(false) {}
    LastJobMessage(int count, bool hasMore) : Message(MessageId), mCount(count), mHasMore(hasMore) {}

    int count() const { return mCount; }
    bool hasMore() const { return mHasMore; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    int mCount, mHasMore;
};

inline void LastJobMessage::encode(Serializer& serializer) const
{
    serializer << mCount << mHasMore;
}

inline void LastJobMessage::decode(Deserializer& deserializer)
{
    deserializer >> mCount >> mHasMore;
}

#endif

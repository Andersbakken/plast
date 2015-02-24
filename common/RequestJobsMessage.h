#ifndef REQUESTJOBSMESSAGE_H
#define REQUESTJOBSMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class RequestJobsMessage : public Message
{
public:
    typedef std::shared_ptr<RequestJobsMessage> SharedPtr;

    enum { MessageId = plast::RequestJobsMessageId };

    RequestJobsMessage() : Message(MessageId), mCount(0) {}
    RequestJobsMessage(int count) : Message(MessageId), mCount(count) {}

    int count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    int mCount;
};

inline void RequestJobsMessage::encode(Serializer& serializer) const
{
    serializer << mCount;
}

inline void RequestJobsMessage::decode(Deserializer& deserializer)
{
    deserializer >> mCount;
}

#endif

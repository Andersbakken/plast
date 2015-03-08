#ifndef JOBRESPONSEMESSAGE_H
#define JOBRESPONSEMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>
#include <cstdint>

class JobResponseMessage : public Message
{
public:
    typedef std::shared_ptr<JobResponseMessage> SharedPtr;

    enum { MessageId = plast::JobResponseMessageId };
    enum Mode { Stdout, Stderr, Compiled, Error };

    JobResponseMessage() : Message(MessageId), mMode(Stdout), mId(0), mSerial(0) {}
    JobResponseMessage(Mode mode, uint64_t id, uint32_t serial, const String& data = String())
        : Message(MessageId), mMode(mode), mId(id), mSerial(serial), mData(data)
    {
    }

    Mode mode() const { return mMode; }
    uint64_t id() const { return mId; }
    String data() const { return mData; }
    uint32_t serial() const { return mSerial; }

    virtual int encodedSize() const;
    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    Mode mMode;
    uint64_t mId;
    uint32_t mSerial;
    String mData;
};

inline int JobResponseMessage::encodedSize() const
{
    return sizeof(int) + sizeof(mId) + sizeof(mSerial) + sizeof(int) + mData.size();
}

inline void JobResponseMessage::encode(Serializer& serializer) const
{
    serializer << static_cast<int>(mMode) << mId << mSerial << mData;
}

inline void JobResponseMessage::decode(Deserializer& deserializer)
{
    int mode;
    deserializer >> mode >> mId >> mSerial >> mData;
    mMode = static_cast<Mode>(mode);
}

#endif

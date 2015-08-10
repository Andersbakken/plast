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
    JobResponseMessage(Mode mode, int exitCode, uint64_t id, uint32_t serial, const String& data = String())
        : Message(MessageId), mMode(mode), mExitCode(exitCode), mId(id), mSerial(serial), mData(data)
    {
    }

    Mode mode() const { return mMode; }
    uint64_t id() const { return mId; }
    String data() const { return mData; }
    uint32_t serial() const { return mSerial; }
    int exitCode() const { return mExitCode; }

    virtual int encodedSize() const;
    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    Mode mMode;
    int mExitCode;
    uint64_t mId;
    uint32_t mSerial;
    String mData;
};

inline int JobResponseMessage::encodedSize() const
{
    return sizeof(int32_t) + sizeof(mExitCode) + sizeof(mId) + sizeof(mSerial) + sizeof(uint32_t) + mData.size();
}

inline void JobResponseMessage::encode(Serializer& serializer) const
{
    serializer << static_cast<uint32_t>(mMode) << mExitCode << mId << mSerial << mData;
}

inline void JobResponseMessage::decode(Deserializer& deserializer)
{
    uint32_t mode;
    deserializer >> mode >> mExitCode >> mId >> mSerial >> mData;
    mMode = static_cast<Mode>(mode);
}

#endif

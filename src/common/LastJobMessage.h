#ifndef LASTJOBMESSAGE_H
#define LASTJOBMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class LastJobMessage : public Message
{
public:
    typedef std::shared_ptr<LastJobMessage> SharedPtr;

    enum { MessageId = plast::LastJobMessageId };

    LastJobMessage() : Message(MessageId), mCompilerType(plast::Unknown), mCompilerMajor(-1), mCount(0), mHasMore(false) {}
    LastJobMessage(plast::CompilerType ctype, int32_t cmajor, const String& ctarget, int32_t count, bool hasMore)
        : Message(MessageId), mCompilerType(ctype), mCompilerMajor(cmajor), mCompilerTarget(ctarget), mCount(count), mHasMore(hasMore)
    {
    }

    plast::CompilerType compilerType() const { return mCompilerType; }
    int32_t compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }
    int32_t count() const { return mCount; }
    bool hasMore() const { return mHasMore; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    plast::CompilerType mCompilerType;
    int32_t mCompilerMajor;
    String mCompilerTarget;
    int32_t mCount, mHasMore;
};

inline void LastJobMessage::encode(Serializer& serializer) const
{
    serializer << static_cast<int32_t>(mCompilerType) << mCompilerMajor << mCompilerTarget << mCount << mHasMore;
}

inline void LastJobMessage::decode(Deserializer& deserializer)
{
    int32_t ctype;
    deserializer >> ctype >> mCompilerMajor >> mCompilerTarget >> mCount >> mHasMore;
    mCompilerType = static_cast<plast::CompilerType>(ctype);
}

#endif

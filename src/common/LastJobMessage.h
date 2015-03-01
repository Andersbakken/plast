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
    LastJobMessage(plast::CompilerType ctype, int cmajor, const String& ctarget, int count, bool hasMore)
        : Message(MessageId), mCompilerType(ctype), mCompilerMajor(cmajor), mCompilerTarget(ctarget), mCount(count), mHasMore(hasMore)
    {
    }

    plast::CompilerType compilerType() const { return mCompilerType; }
    int compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }
    int count() const { return mCount; }
    bool hasMore() const { return mHasMore; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    plast::CompilerType mCompilerType;
    int mCompilerMajor;
    String mCompilerTarget;
    int mCount, mHasMore;
};

inline void LastJobMessage::encode(Serializer& serializer) const
{
    serializer << static_cast<int>(mCompilerType) << mCompilerMajor << mCompilerTarget << mCount << mHasMore;
}

inline void LastJobMessage::decode(Deserializer& deserializer)
{
    int ctype;
    deserializer >> ctype >> mCompilerMajor >> mCompilerTarget >> mCount >> mHasMore;
    mCompilerType = static_cast<plast::CompilerType>(ctype);
}

#endif

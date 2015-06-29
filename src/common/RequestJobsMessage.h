#ifndef REQUESTJOBSMESSAGE_H
#define REQUESTJOBSMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class RequestJobsMessage : public Message
{
public:
    typedef std::shared_ptr<RequestJobsMessage> SharedPtr;

    enum { MessageId = plast::RequestJobsMessageId };

    RequestJobsMessage() : Message(MessageId), mCompilerType(CompilerVersion::Unknown), mCompilerMajor(-1), mCount(0) {}
    RequestJobsMessage(CompilerVersion::Type ctype, int32_t cmajor, const String& ctarget, int32_t count)
        : Message(MessageId), mCompilerType(ctype), mCompilerTarget(ctarget), mCompilerMajor(cmajor), mCount(count)
    {
    }

    CompilerVersion::Type compilerType() const { return mCompilerType; }
    int32_t compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }
    int32_t count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    CompilerVersion::Type mCompilerType;
    String mCompilerTarget;
    int32_t mCompilerMajor, mCount;
};

inline void RequestJobsMessage::encode(Serializer& serializer) const
{
    serializer << mCount << mCompilerMajor << mCompilerTarget << static_cast<int32_t>(mCompilerType);
}

inline void RequestJobsMessage::decode(Deserializer& deserializer)
{
    int32_t ctype;
    deserializer >> mCount >> mCompilerMajor >> mCompilerTarget >> ctype;
    mCompilerType = static_cast<CompilerVersion::Type>(ctype);
}

#endif

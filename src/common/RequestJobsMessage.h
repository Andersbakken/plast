#ifndef REQUESTJOBSMESSAGE_H
#define REQUESTJOBSMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class RequestJobsMessage : public Message
{
public:
    typedef std::shared_ptr<RequestJobsMessage> SharedPtr;

    enum { MessageId = plast::RequestJobsMessageId };

    RequestJobsMessage() : Message(MessageId), mCompilerType(plast::Unknown), mCompilerMajor(-1), mCount(0) {}
    RequestJobsMessage(plast::CompilerType ctype, int cmajor, const String& ctarget, int count)
        : Message(MessageId), mCompilerType(ctype), mCompilerTarget(ctarget), mCompilerMajor(cmajor), mCount(count)
    {
    }

    plast::CompilerType compilerType() const { return mCompilerType; }
    int compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }
    int count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    plast::CompilerType mCompilerType;
    String mCompilerTarget;
    int mCompilerMajor, mCount;
};

inline void RequestJobsMessage::encode(Serializer& serializer) const
{
    serializer << mCount << mCompilerMajor << mCompilerTarget << static_cast<int>(mCompilerType);
}

inline void RequestJobsMessage::decode(Deserializer& deserializer)
{
    int ctype;
    deserializer >> mCount >> mCompilerMajor >> mCompilerTarget >> ctype;
    mCompilerType = static_cast<plast::CompilerType>(ctype);
}

#endif

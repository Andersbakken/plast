#ifndef HASJOBSMESSAGE_H
#define HASJOBSMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class HasJobsMessage : public Message
{
public:
    typedef std::shared_ptr<HasJobsMessage> SharedPtr;

    enum { MessageId = plast::HasJobsMessageId };

    HasJobsMessage() : Message(MessageId), mCompilerType(plast::Unknown), mCompilerMajor(-1), mCount(0), mPort(0) {}
    HasJobsMessage(plast::CompilerType ctype, int cmajor, const String& ctarget, int count, uint16_t port = 0)
        : Message(MessageId), mCompilerType(ctype), mCompilerMajor(cmajor), mCompilerTarget(ctarget), mCount(count), mPort(port)
    {
    }

    plast::CompilerType compilerType() const { return mCompilerType; }
    int compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }

    void setPeer(const String& peer) { mPeer = peer; }
    void setPort(uint16_t port) { mPort = port; }

    String peer() const { return mPeer; }
    uint16_t port() const { return mPort; }
    int count() const { return mCount; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    plast::CompilerType mCompilerType;
    int mCompilerMajor, mCount;
    String mCompilerTarget, mPeer;
    uint16_t mPort;
};

inline void HasJobsMessage::encode(Serializer& serializer) const
{
    serializer << static_cast<int>(mCompilerType) << mCompilerMajor << mCompilerTarget << mCount << mPeer << mPort;
}

inline void HasJobsMessage::decode(Deserializer& deserializer)
{
    int ctype;
    deserializer >> ctype >> mCompilerMajor >> mCompilerTarget >> mCount >> mPeer >> mPort;
    mCompilerType = static_cast<plast::CompilerType>(ctype);
}

#endif

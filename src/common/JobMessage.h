#ifndef JOBMESSAGE_H
#define JOBMESSAGE_H

#include <Plast.h>
#include <rct/List.h>
#include <rct/Message.h>
#include <rct/Path.h>
#include <rct/String.h>
#include <rct/Log.h>
#include <cstdint>

class JobMessage : public Message
{
public:
    typedef std::shared_ptr<JobMessage> SharedPtr;

    enum { MessageId = plast::JobMessageId };

    JobMessage()
        : Message(MessageId), mId(0), mSerial(0), mCompilerType(plast::Unknown), mCompilerMajor(-1)
    {
    }
    JobMessage(const Path& path, const List<String>& args, uint64_t id = 0, const String& pre = String(),
               int serial = 0, const String& remoteName = String(), plast::CompilerType ctype = plast::Unknown,
               int cmajor = 0, const String& ctarget = String())
        : Message(MessageId), mPath(path), mArgs(args), mId(id),
          mPreprocessed(pre), mSerial(serial), mRemoteName(remoteName),
          mCompilerType(ctype), mCompilerMajor(cmajor), mCompilerTarget(ctarget)
    {
    }

    Path path() const { return mPath; }
    List<String> args() const { return mArgs; }
    String preprocessed() const { return mPreprocessed; }
    uint64_t id() const { return mId; }
    int serial() const { return mSerial; }
    String remoteName() const { return mRemoteName; }
    plast::CompilerType compilerType() const { return mCompilerType; }
    int compilerMajor() const { return mCompilerMajor; }
    String compilerTarget() const { return mCompilerTarget; }

    virtual int encodedSize() const;
    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    Path mPath;
    List<String> mArgs;
    uint64_t mId;
    String mPreprocessed;
    int mSerial;
    String mRemoteName;
    plast::CompilerType mCompilerType;
    int mCompilerMajor;
    String mCompilerTarget;
};

inline int JobMessage::encodedSize() const
{
    int size = 0;
    auto addString = [&size](const String &str) {
        size += sizeof(int) + str.size();
    };

    addString(mPath);
    size += sizeof(int);
    for (const auto &arg : mArgs) {
        addString(arg);
    }
    size += sizeof(mId);
    addString(mPreprocessed);
    size += sizeof(mSerial);
    addString(mRemoteName);
    size += sizeof(int) + sizeof(mCompilerMajor);
    addString(mCompilerTarget);
    return size;
}

inline void JobMessage::encode(Serializer& serializer) const
{
    serializer << mPath << mArgs << mId << mPreprocessed << mSerial << mRemoteName << static_cast<int>(mCompilerType) << mCompilerMajor << mCompilerTarget;
}

inline void JobMessage::decode(Deserializer& deserializer)
{
    int ctype;
    deserializer >> mPath >> mArgs >> mId >> mPreprocessed >> mSerial >> mRemoteName >> ctype >> mCompilerMajor >> mCompilerTarget;
    mCompilerType = static_cast<plast::CompilerType>(ctype);
}

#endif

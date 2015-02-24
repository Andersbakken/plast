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
        : Message(MessageId), mId(0), mSerial(0)
    {
    }
    JobMessage(const Path& path, const List<String>& args, uint64_t id = 0, const String& pre = String(),
               int serial = 0, const String& remoteName = String())
        : Message(MessageId), mPath(path), mArgs(args), mId(id),
          mPreprocessed(pre), mSerial(serial), mRemoteName(remoteName)
    {
    }

    Path path() const { return mPath; }
    List<String> args() const { return mArgs; }
    String preprocessed() const { return mPreprocessed; }
    uint64_t id() const { return mId; }
    int serial() const { return mSerial; }
    String remoteName() const { return mRemoteName; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    Path mPath;
    List<String> mArgs;
    uint64_t mId;
    String mPreprocessed;
    int mSerial;
    String mRemoteName;
};

inline void JobMessage::encode(Serializer& serializer) const
{
    serializer << mPath << mArgs << mId << mPreprocessed << mSerial << mRemoteName;
}

inline void JobMessage::decode(Deserializer& deserializer)
{
    deserializer >> mPath >> mArgs >> mId >> mPreprocessed >> mSerial >> mRemoteName;
}

#endif

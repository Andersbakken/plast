#ifndef BUILDINGMESSAGE_H
#define BUILDINGMESSAGE_H

#include <Plast.h>
#include <rct/Message.h>

class BuildingMessage : public Message
{
public:
    typedef std::shared_ptr<BuildingMessage> SharedPtr;

    enum { MessageId = plast::BuildingMessageId };
    enum Type { Start, Stop };

    BuildingMessage() : Message(MessageId), mType(Start), mId(0) { }
    BuildingMessage(const String& peer, const String& file, Type type, uint64_t id)
        : Message(MessageId), mPeer(peer), mFile(file), mType(type), mId(id)
    {
    }
    BuildingMessage(Type type, uint64_t id)
        : Message(MessageId), mType(type), mId(id)
    {
    }

    String peer() const { return mPeer; }
    String file() const { return mFile; }
    Type type() const { return mType; }
    uint64_t id() const { return mId; }

    virtual void encode(Serializer& serializer) const;
    virtual void decode(Deserializer& deserializer);

private:
    String mPeer, mFile;
    Type mType;
    uint64_t mId;
};

inline void BuildingMessage::encode(Serializer& serializer) const
{
    serializer << mPeer << mFile << static_cast<uint8_t>(mType) << mId;
}

inline void BuildingMessage::decode(Deserializer& deserializer)
{
    uint8_t type;
    deserializer >> mPeer >> mFile >> type >> mId;
    mType = static_cast<Type>(type);
}

#endif

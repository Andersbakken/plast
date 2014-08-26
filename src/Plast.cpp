#include "Plast.h"
#include <rct/Messages.h>

namespace Plast {
bool init()
{
    Messages::registerMessage<HandshakeMessage>();
    Messages::registerMessage<LocalJobMessage>();
    Messages::registerMessage<LocalJobResponseMessage>();
    return true;
}
}

template <>
inline Deserializer &operator>>(Deserializer &s, LocalJobResponseMessage::Output &output)
{
    uint8_t type;
    s >> type >> output.text;
    output.type = static_cast<LocalJobResponseMessage::Output::Type>(type);
    return s;
}

template <>
inline Serializer &operator<<(Serializer &s, const LocalJobResponseMessage::Output &output)
{
    s << static_cast<uint8_t>(output.type) << output.text;
    return s;
}

void LocalJobResponseMessage::encode(Serializer &serializer) const
{
    serializer << mStatus << mOutput;
}

void LocalJobResponseMessage::decode(Deserializer &deserializer)
{
    deserializer >> mStatus >> mOutput;
}

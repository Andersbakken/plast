#include "Plast.h"
#include <rct/Messages.h>

using namespace Plast;

bool plast()
{
    Messages::registerMessage<HandshakeMessage>();
    return true;
}

void HandshakeMessage::encode(Serializer &serializer) const
{
    serializer << mHostName << mCapacity;
}

void HandshakeMessage::decode(Deserializer &deserializer)
{
    deserializer >> mHostName >> mCapacity;
}

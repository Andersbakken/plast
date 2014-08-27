#include "Plast.h"
#include <rct/Messages.h>

namespace Plast {
bool init()
{
    Messages::registerMessage<HandshakeMessage>();
    Messages::registerMessage<LocalJobMessage>();
    Messages::registerMessage<LocalJobResponseMessage>();
    Messages::registerMessage<QuitMessage>();
    return true;
}

Path resolveCompiler(const Path &path)
{
    const String fileName = path.fileName();
    const List<String> paths = String(getenv("PATH")).split(':');
    // error() << fileName;
    for (const auto &p : paths) {
        const Path orig = p + "/" + fileName;
        Path exec = orig;
        // error() << "Trying" << exec;
        if (exec.resolve() && strcmp(exec.fileName(), "plastc")) {
            return orig;
        }
    }
    return Path();
}
}

template <>
inline Deserializer &operator>>(Deserializer &s, Output &output)
{
    uint8_t type;
    s >> type >> output.text;
    output.type = static_cast<Output::Type>(type);
    return s;
}

template <>
inline Serializer &operator<<(Serializer &s, const Output &output)
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

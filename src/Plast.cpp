#include "Plast.h"
#include <rct/Messages.h>

namespace Plast {
bool init()
{
    Messages::registerMessage<HandshakeMessage>();
    Messages::registerMessage<LocalJobMessage>();
    return true;
}
}

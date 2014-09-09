#include "Plast.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <rct/Process.h>
#include <rct/SHA256.h>
#include "ClientJobMessage.h"
#include "ClientJobResponseMessage.h"
#include "CompilerMessage.h"
#include "CompilerRequestMessage.h"
#include "DaemonListMessage.h"
#include "HandshakeMessage.h"
#include "JobAnnouncementMessage.h"
#include "JobDiscardedMessage.h"
#include "JobMessage.h"
#include "JobRequestMessage.h"
#include "JobResponseMessage.h"
#include "MonitorMessage.h"
#include "QuitMessage.h"

namespace Plast {
bool init()
{
    Message::registerMessage<ClientJobMessage>();
    Message::registerMessage<ClientJobResponseMessage>();
    Message::registerMessage<CompilerMessage>();
    Message::registerMessage<CompilerRequestMessage>();
    Message::registerMessage<DaemonListMessage>();
    Message::registerMessage<HandshakeMessage>();
    Message::registerMessage<JobAnnouncementMessage>();
    Message::registerMessage<JobDiscardedMessage>();
    Message::registerMessage<JobMessage>();
    Message::registerMessage<JobRequestMessage>();
    Message::registerMessage<JobResponseMessage>();
    Message::registerMessage<MonitorMessage>();
    Message::registerMessage<QuitMessage>();
    return true;
}

Path defaultSocketFile()
{
    return Path::home() + ".plastd.sock";
}
}


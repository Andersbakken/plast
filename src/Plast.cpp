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

Path defaultSocketFile()
{
    return Path::home() + ".plastd.sock";
}

Path resolveCompiler(const Path &path)
{
    const String fileName = path.fileName();
    const List<String> paths = String(getenv("PATH")).split(':');
    // error() << fileName;
    const bool hasRTags = getenv("RTAGS_GCC_WRAPPER");
    for (const auto &p : paths) {
        const Path orig = p + "/" + fileName;
        Path exec = orig;
        // error() << "Trying" << exec;
        if (exec.resolve() && strcmp(exec.fileName(), "plastc") && (hasRTags || strcmp(exec.fileName(), "gcc-rtags-wrapper.sh"))) {
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

static const char *argOptions[] = {
    "-D",
    "-I",
    "-Xpreprocessor",
    "-aux-info",
    "-idirafter",
    "-imacros",
    "-imultilib",
    "-include",
    "-iprefix",
    "-isysroot",
    "-iwithprefix",
    "-iwithprefixbefore",
    "-wrapper",
    "-x"
};

static int compare(const void *s1, const void *s2) {
    const char *key = s1;
    const char * const *arg = s2;
    return strcmp(key, *arg);
}

static inline bool hasArg(const String &arg)
{
    return bsearch(arg.constData(), argOptions, sizeof(argOptions) / sizeof(argOptions[0]),
                   sizeof(const char*), ::compare);
}

CompilerArgs HandshakeMessage::create(const List<String> &args)
{
    int compiler = 0;
    int source = -1;
    int output = -1;
    Type type = Link;
    for (int i=1; i<args.size(); ++i) {
        const String &arg = args[i];
        if (arg == "-c") {
            type = Compile;
        } else if (arg == "-S") {

        } else if (arg == "-E") {

        } else if (arg == "-o") {

        } else if (hasArg(arg)) {

        }
    }

}

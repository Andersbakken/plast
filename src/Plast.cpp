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
    const char *key = reinterpret_cast<const char*>(s1);
    const char * const *arg = reinterpret_cast<const char * const *>(s2);
    return strcmp(key, *arg);
}

static inline bool hasArg(const String &arg)
{
    return bsearch(arg.constData(), argOptions, sizeof(argOptions) / sizeof(argOptions[0]),
                   sizeof(const char*), ::compare);
}

CompilerArgs CompilerArgs::create(const List<String> &args)
{
    CompilerArgs ret = { args, List<Path>(), Path(), args.value(0), Link, None };
    for (int i=1; i<args.size(); ++i) {
        const String &arg = args[i];
        if (arg == "-c") {
            if (ret.mode == Link)
                ret.mode = Compile;
        } else if (arg == "-S") {
            ret.flags |= NoAssemble;
        } else if (arg == "-E") {
            ret.mode = Preprocess;
        } else if (arg == "-o") {
            ret.flags |= HasOutput;
        } else if (hasArg(arg)) {
            ++i;
        } else if (!arg.startsWith("-")) {
            ret.sourceFiles.append(arg);
        }
    }
    return ret;
}

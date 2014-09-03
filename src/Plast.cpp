#include "Plast.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "Compiler.h"

namespace Plast {
bool init()
{
    Message::registerMessage<ClientJobMessage>();
    Message::registerMessage<ClientJobResponseMessage>();
    Message::registerMessage<CompilerMessage>();
    Message::registerMessage<CompilerRequestMessage>();
    Message::registerMessage<DaemonJobAnnouncementMessage>();
    Message::registerMessage<DaemonJobRequestMessage>();
    Message::registerMessage<DaemonJobResponseMessage>();
    Message::registerMessage<DaemonListMessage>();
    Message::registerMessage<HandshakeMessage>();
    Message::registerMessage<QuitMessage>();
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

void ClientJobResponseMessage::encode(Serializer &serializer) const
{
    serializer << mStatus << mOutput;
}

void ClientJobResponseMessage::decode(Deserializer &deserializer)
{
    deserializer >> mStatus >> mOutput;
}

class CompilerPackage
{
public:
    CompilerPackage() {}
    CompilerPackage(const Map<Path, String>& files) : mFiles(files) {}

    static CompilerPackage* loadFromPaths(const Set<Path>& paths);

    bool isEmpty() const { return mFiles.isEmpty(); }
    bool loadFile(const Path& file);

    const Map<Path, String>& files() const { return mFiles; }

private:
    Map<Path, String> mFiles;
};

CompilerPackage* CompilerPackage::loadFromPaths(const Set<Path>& paths)
{
    CompilerPackage* package = new CompilerPackage;
    Set<Path>::const_iterator path = paths.begin();
    const Set<Path>::const_iterator end = paths.end();
    while (path != end) {
        if (!package->loadFile(*path)) {
            error() << "Failed to load file" << *path;
            delete package;
            return 0;
        }
        ++path;
    }
    if (package->isEmpty()) {
        error() << "package is empty" << paths;
        delete package;
        return 0;
    }
    return package;
}

bool CompilerPackage::loadFile(const Path& file)
{
    String data;
    if (!Rct::readFile(file, data))
        return false;
    mFiles[file] = data;
    return true;
}

Serializer& operator<<(Serializer& s, const CompilerPackage& p)
{
    s << p.files();
    return s;
}

Deserializer& operator>>(Deserializer& s, CompilerPackage& p)
{
    Map<Path, String> files;
    s >> files;
    p = CompilerPackage(files);
    return s;
}

Map<String, CompilerPackage*> CompilerMessage::sPackages;

CompilerMessage::CompilerMessage(const std::shared_ptr<Compiler> &compiler)
    : Message(MessageId, Compressed), mPackage(0)
{
    if (compiler) {
        mSha256 = compiler->sha256();
        mCompiler = compiler->path();
        const Map<String, CompilerPackage*>::const_iterator package = sPackages.find(compiler->sha256());
        if (package != sPackages.end()) {
            mPackage = package->second;
            return;
        }
        /*
          for (Set<Path>::const_iterator it = paths.begin(); it != paths.end(); ++it) {
          if (it->isSymLink()) {
          mLinks[*it] = it->followLink();
          } else {
          mFiles[*it] = it->readAll();
          }
          }
        */

        mPackage = loadCompiler(compiler->files());
        if (mPackage) {
            printf("[%s:%d]: if (mPackage) {\n", __FILE__, __LINE__); fflush(stdout);
            sPackages[mSha256] = mPackage;
        } else {
            printf("[%s:%d]: \n", __FILE__, __LINE__); fflush(stdout);
        }
    }
}

CompilerMessage::~CompilerMessage()
{
    delete mPackage;
}

CompilerPackage* CompilerMessage::loadCompiler(const Set<Path>& paths)
{
    error() << "loading" << paths;
    return CompilerPackage::loadFromPaths(paths);
}

void CompilerMessage::encode(Serializer &serializer) const
{
    serializer << mCompiler << mSha256 << (mPackage != 0);
    error() << "Encoding" << mCompiler << mSha256 << (mPackage != 0);
    if (mPackage)
        serializer << *mPackage;
}

void CompilerMessage::decode(Deserializer &deserializer)
{
    bool hasPackage;
    deserializer >> mCompiler >> mSha256 >> hasPackage;
    error() << "Decoding" << mCompiler << mSha256 << hasPackage;
    if (hasPackage) {
        if (!mPackage)
            mPackage = new CompilerPackage;
        deserializer >> *mPackage;
    }
}

bool CompilerMessage::writeFiles(const Path &root) const
{
    if (!mPackage)
        return false;

    for (const auto &file : mPackage->files()) {
        const Path path = root + file.first;
        Path::mkdir(path.parentDir(), Path::Recursive);
        if (!Rct::writeFile(path, file.second)) {
            error() << "Couldn't write file" << path;
            return false;
        }
    }
    return true;
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

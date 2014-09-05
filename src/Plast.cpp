#include "Plast.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <rct/Process.h>
#include <rct/SHA256.h>

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
    Message::registerMessage<JobMessage>();
    Message::registerMessage<JobRequestMessage>();
    Message::registerMessage<JobResponseMessage>();
    Message::registerMessage<QuitMessage>();
    return true;
}

Path defaultSocketFile()
{
    return Path::home() + ".plastd.sock";
}
}

Hash<String, std::shared_ptr<Compiler> > Compiler::sBySha;
Hash<String, std::shared_ptr<Compiler> > Compiler::sByPath;
List<String> Compiler::sEnviron;

void Compiler::ensureEnviron()
{
    if (!sEnviron.isEmpty())
        return;
    sEnviron = Process::environment();
    for (int i = 0; i < sEnviron.size(); ++i) {
        if (sEnviron.at(i).startsWith("PATH=")) {
            sEnviron.removeAt(i);
            break;
        }
    }
}

static inline bool printProgName(const Path &path, const String &prog, Set<Path> &files, List<String> &shaList)
{
    Process process;
    if (!process.exec(path, List<String>() << "-print-prog-name=" + prog)) {
        error() << "Couldn't invoke compiler" << path;
        return false;
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    for (const Path &path : lines) {
        if (path.isFile()) {
            shaList.append(path.fileName());
            files.insert(path);
        } else if (!path.contains('/')) {
            const Path usrBinFile = "/usr/bin/" + path;
            if (usrBinFile.isFile()) {
                shaList.append(path);
                files.insert(usrBinFile);
            }
        }
    }
    return true;
}

std::shared_ptr<Compiler> Compiler::compiler(const Path &compiler, const String &path)
{
    ensureEnviron();

    auto it = sByPath.find(compiler);
    if (it != sByPath.end())
        return it->second;
    std::shared_ptr<Compiler> &c = sByPath[compiler];
    assert(!c);
    const Path resolved = compiler.resolved();
    std::shared_ptr<Compiler> &resolvedCompiler = sByPath[resolved];
    if (resolvedCompiler) {
        c = resolvedCompiler;
        return c;
    }
    SHA256 sha;
    c.reset(new Compiler);
    List<String> shaList;
    const char *progNames[] = { "as", "cc1", "cc1plus" };
    for (size_t i=0; i < sizeof(progNames) / sizeof(progNames[0]); ++i) {
        if (!printProgName(resolved, progNames[i], c->mFiles, shaList)) {
            c.reset();
            return c;
        }
    }
    Process process;
    if (!process.exec(
#ifdef OS_Darwin
            "otool", List<String>() << "-L" << "-X" << resolved
#else
            "ldd", List<String>() << resolved
#endif
            )) {
        error() << "Couldn't ldd compiler" << resolved;
        return std::shared_ptr<Compiler>();
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    shaList.append(compiler.fileName());
    for (const String &line : lines) {
        error() << line;
#ifdef OS_Darwin
        const int idx = line.indexOf('/');
        if (idx == -1) {
            continue;
        }
#else
        int idx = line.indexOf("=> /");
        if (idx == -1)
            continue;
        idx += 3;
#endif
        const int end = line.indexOf(' ', idx + 2);
        if (end == -1) {
            continue;
        }
        const Path unresolved = line.mid(idx, end - idx);
        const Path path = Path::resolved(unresolved);
        if (path.isFile()) {
            shaList.append(unresolved.fileName());
            c->mFiles.insert(unresolved);
        } else {
            error() << "Couldn't resolve path" << unresolved;
        }
    }
    if (c) {
        shaList.sort();
        for (const String &s : shaList) {
            sha.update(s);
            error() << s;
        }

        c->mSha256 = sha.hash(SHA256::Hex);
        debug() << "producing compiler" << c->mSha256 << shaList;

        error() << "GOT SHA" << c->mSha256;
        c->mPath = compiler;
        sBySha[c->mSha256] = c;
        sByPath[compiler] = c;
        if (!resolvedCompiler)
            resolvedCompiler = c;
        warning() << "Created package" << compiler << c->mFiles << c->mSha256;
#warning should write these files to local cache here
    }
    return c;
}

String Compiler::dump()
{
    String ret;
    for (const auto &compiler : sBySha) {
        if (!ret.isEmpty())
            ret << '\n';
        ret << (compiler.first + ":");
        for (const auto &path : compiler.second->mFiles) {
            if (path == compiler.second->path()) {
                ret << "\n    " << path << "*";
            } else {
                ret << "\n    " << path;
            }
        }
    }
    return ret;
}

void Compiler::insert(const Path &executable, const String &sha256, const Set<Path> &files)
{
    std::shared_ptr<Compiler> &c = sBySha[sha256];
    assert(!c);
    assert(!sByPath.contains(executable));
    c.reset(new Compiler);
    c->mPath = executable;
    c->mSha256 = sha256;
    c->mFiles = files;
    sByPath[executable] = c;
}

Path Compiler::resolve(const Path &path)
{
    const String fileName = path.fileName();
    const List<String> paths = String(getenv("PATH")).split(':');
    // error() << fileName;
    for (const auto &p : paths) {
        const Path orig = p + "/" + fileName;
        Path exec = orig;
        // error() << "Trying" << exec;
        if (exec.resolve()) {
            const char *fileName = exec.fileName();
            if (strcmp(fileName, "plastc") && strcmp(fileName, "gcc-rtags-wrapper.sh") && strcmp(fileName, "icecc")) {
                return orig;
            }
        }
    }
    return Path();
}

void ClientJobResponseMessage::encode(Serializer &serializer) const
{
    serializer << mStatus << mOutput;
}

void ClientJobResponseMessage::decode(Deserializer &deserializer)
{
    deserializer >> mStatus >> mOutput;
}

class CompilerPackage {
public:
    struct File;
    CompilerPackage()
    {
    }
    CompilerPackage(const Path &executable, const Hash<Path, File> &files)
        : mExecutable(executable), mFiles(files)
    {
    }

    static CompilerPackage *loadFromPaths(const Path &executable, const Set<Path> &paths);

    bool isEmpty() const
    {
        return mFiles.isEmpty();
    }
    bool loadFile(const Path &file);

    const Path &executable() const { return mExecutable; }
    struct File {
        String contents;
        mode_t perm;
    };

    const Hash<Path, File> &files() const { return mFiles; }
private:
    Path mExecutable;
    Hash<Path, File> mFiles;
};

CompilerPackage *CompilerPackage::loadFromPaths(const Path &executable, const Set<Path> &paths)
{
    CompilerPackage *package = new CompilerPackage;
    if (!package->loadFile(executable)) {
        delete package;
        return 0;
    }
    package->mExecutable = executable;
    for (const auto &it : paths) {
        if (!package->loadFile(it)) {
            error() << "Failed to load file" << it;
            delete package;
            return 0;
        }
    }
    if (package->isEmpty()) {
        error() << "package is empty" << paths;
        delete package;
        return 0;
    }
    return package;
}

bool CompilerPackage::loadFile(const Path &file)
{
    String data;
    mode_t perm;
    if (!Rct::readFile(file, data, &perm))
        return false;
    mFiles[file] = { data, perm };
    return true;
}

Serializer &operator<<(Serializer &s, const CompilerPackage::File &p)
{
    s << p.contents << static_cast<uint32_t>(p.perm);
    return s;
}

Deserializer &operator>>(Deserializer &s, CompilerPackage::File &p)
{
    uint32_t perm;
    s >> p.contents >> perm;
    p.perm = static_cast<mode_t>(perm);
    return s;
}

Serializer &operator<<(Serializer &s, const CompilerPackage &p)
{
    s << p.executable() << p.files();
    return s;
}

Deserializer &operator>>(Deserializer &s, CompilerPackage &p)
{
    Path compiler;
    Hash<Path, CompilerPackage::File> files;
    s >> compiler >> files;
    p = CompilerPackage(compiler, files);
    return s;
}

Hash<String, CompilerPackage *> CompilerMessage::sPackages;

CompilerMessage::CompilerMessage(const std::shared_ptr<Compiler> &compiler)
    : Message(MessageId, Compressed), mPackage(0)
{
    if (compiler) {
        mSha256 = compiler->sha256();
        mCompiler = compiler->path();
        const Hash<String, CompilerPackage *>::const_iterator package = sPackages.find(compiler->sha256());
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

        mPackage = loadCompiler(compiler->path(), compiler->files());
        if (mPackage) {
            sPackages[mSha256] = mPackage;
        }
    }
}

CompilerMessage::~CompilerMessage()
{
}

CompilerPackage *CompilerMessage::loadCompiler(const Path &compiler, const Set<Path> &paths)
{
    error() << "loading" << paths;
    return CompilerPackage::loadFromPaths(compiler, paths);
}

void CompilerMessage::encode(Serializer &serializer) const
{
    serializer << mCompiler << mSha256 << (mPackage != 0);
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

    Path::mkdir(root);
    Set<Path> files;
    for (const auto &file : mPackage->files()) {
        // const Path path = root + file.first;
        // Path::mkdir(path.parentDir(), Path::Recursive);
        const char *fileName = file.first.fileName();
        const Path path = root + fileName;
        // Path::mkdir(path.parentDir(), Path::Recursive);
        if (!Rct::writeFile(path, file.second.contents, file.second.perm)) {
            error() << "Couldn't write file" << path;
            for (const Path &f : root.files(Path::File))
                Path::rm(f);
            rmdir(root.constData());
            return false;
        }
        files.insert(path);
    }
    const char *compilerFileName = mCompiler.fileName();
    if (symlink(compilerFileName, (root + "COMPILER").constData())) {
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        rmdir(root.constData());
        error() << "Failed to create symlink" << errno << strerror(errno) << (root + "COMPILER");
        return false;
    }
    const Path compilerPath = root + compilerFileName;
    error() << "chmoding" << compilerPath;
    if (chmod(compilerPath.constData(), S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) {
        error() << "Failed to chmod compiler" << errno << strerror(errno) << compilerPath;
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        rmdir(root.constData());
        return false;
    }

    Process process;
    if (!process.exec(compilerPath, List<String>() << "-v")) { // compiler can't run. Cache as bad?
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        Rct::writeFile(root + "BAD", String());
        return false;
    }
    Compiler::insert(compilerPath, mSha256, files);
    return true;
}

static const char *argOptions[] = {
    "-D",
    "-I",
    "-MQ",
    "-MT"
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
    "-wrapper"
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

std::shared_ptr<CompilerArgs> CompilerArgs::create(const List<String> &args)
{
    std::shared_ptr<CompilerArgs> ret(new CompilerArgs);
    ret->commandLine = args;
    ret->mode = Link;
    ret->flags = None;
    ret->objectFileIndex = -1;
    for (int i=1; i<args.size(); ++i) {
        const String &arg = args[i];
        if (arg == "-c") {
            if (ret->mode == Link)
                ret->mode = Compile;
        } else if (arg == "-S") {
            ret->flags |= NoAssemble;
        } else if (arg == "-E") {
            ret->mode = Preprocess;
        } else if (arg == "-o") {
            ret->flags |= HasDashO;
            ret->objectFileIndex = ++i;
        } else if (arg == "-MF") {
            ret->flags |= HasDashMF;
            ++i;
        } else if (arg == "-MMD") {
            ret->flags |= HasDashMMD;
        } else if (arg == "-T") {
            ret->flags |= HasDashMT;
            ++i;
        } else if (arg == "-x") {
            ret->flags |= HasDashX;
            if (++i == args.size())
                return std::shared_ptr<CompilerArgs>();
            const String lang = args.value(i);
            const CompilerArgs::Flag languages[] = { CPlusPlus, C, CPreprocessed, CPlusPlusPreprocessed, ObjectiveC, AssemblerWithCpp, Assembler };
            for (size_t j=0; j<sizeof(languages) / sizeof(languages[0]); ++j) {
                if (lang == CompilerArgs::languageName(languages[j])) {
                    ret->flags &= ~LanguageMask;
                    ret->flags |= languages[j];
                    // -x takes precedence
                    break;
                }
            }
        } else if (hasArg(arg)) {
            ++i;
        } else if (!arg.startsWith("-")) {
            ret->sourceFileIndexes.append(i);
            if (!(ret->flags & LanguageMask)) {
                const int lastDot = arg.lastIndexOf('.');
                if (lastDot != -1) {
                    const char *ext = arg.constData() + lastDot + 1;
                    struct {
                        const char *suffix;
                        const Flag flag;
                    } static const suffixes[] = {
                        { "C", CPlusPlus },
                        { "cxx", CPlusPlus },
                        { "cpp", CPlusPlus },
                        { "c", C },
                        { "i", CPreprocessed },
                        { "ii", CPlusPlusPreprocessed },
                        { "m", ObjectiveC },
                        { "S", Assembler },
                        { "s", AssemblerWithCpp },
                        { 0, None }
                    };
                    for (int i=0; suffixes[i].suffix; ++i) {
                        if (!strcmp(ext, suffixes[i].suffix)) {
                            ret->flags |= suffixes[i].flag;
                            break;
                        }
                    }
                }
            }
        } else if (arg == "-") {
            ret->flags |= StdinInput;
        }
    }
    return ret;
}

const char *CompilerArgs::languageName(Flag flag)
{
    switch (flag) {
    case CPlusPlus: return "c++";
    case C: return "c";
    case CPreprocessed: return "cpp-output";
    case CPlusPlusPreprocessed: return "c++-cpp-output";
    case ObjectiveC: return "objective-c"; // ### what about ObjectiveC++?
    case AssemblerWithCpp: return "assembler-with-cpp";
    case Assembler: return "assembler";
    default: break;
    }
    return "";
}

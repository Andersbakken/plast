#include "CompilerMessage.h"
#include <rct/Process.h>

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

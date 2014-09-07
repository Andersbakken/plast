#include "CompilerMessage.h"
#include <rct/Process.h>

#if 0
class CompilerPackage {
public:
    struct File;
    CompilerPackage()
    {
    }
    CompilerPackage(const Path &executable, const Hash<Path, std::pair<String, mode_t> > &files)
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
    const Hash<Path, std::pair<String, mode_t> > &files() const { return mFiles; }
private:
    Path mExecutable;
    Hash<Path, std::pair<String, mode_t> > mFiles;
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

Serializer &operator<<(Serializer &s, const std::pair<String, mode_t> &p)
{
    s << p.first << static_cast<uint32_t>(p.second);
    return s;
}

Deserializer &operator>>(Deserializer &s, std::pair<String, mode_t> &p)
{
    uint32_t perm;
    s >> p.first >> perm;
    p.second = static_cast<mode_t>(perm);
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
    Hash<Path, std::pair<String, mode_t> > files;
    s >> compiler >> files;
    p = CompilerPackage(compiler, files);
    return s;
}

#endif


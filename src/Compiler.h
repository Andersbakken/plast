#ifndef COMPILER_H
#define COMPILER_H

#include <memory>
#include <rct/Hash.h>
#include <rct/List.h>
#include <rct/Path.h>
#include <rct/Set.h>
#include <rct/String.h>

class Compiler
{
public:
    static std::shared_ptr<Compiler> compiler(const Path& executable, const String& path = String());
    static void cleanup();

    String sha256() const { return mSha256; }
    Path path() const { return mPath; }
    Set<Path> files() const { return mFiles; }

private:
    static void ensureEnviron();

    static Hash<String, std::shared_ptr<Compiler> > sBySha, sByPath;
    static List<String> sEnviron;

private:
    String mSha256;
    Path mPath;
    Set<Path> mFiles;
};

#endif

#ifndef COMPILERVERSION_H
#define COMPILERVERSION_H

#include "CompilerArgs.h"
#include <rct/Map.h>
#include <rct/Hash.h>
#include <rct/Path.h>
#include <rct/String.h>
#include <memory>
#include <Plast.h>

class CompilerVersion
{
public:
    typedef std::shared_ptr<CompilerVersion> SharedPtr;
    typedef std::weak_ptr<CompilerVersion> WeakPtr;

    static void init(const Path& path, unsigned int flags = 0, const String& target = String());
    static SharedPtr version(const Path& path, unsigned int flags = 0, const String& target = String());
    static SharedPtr version(plast::CompilerType compiler, int major, const String& target);
    static bool hasCompiler(plast::CompilerType compiler, int major, const String& target);

    plast::CompilerType compiler() const { return mCompiler; }

    int major() const { return mVersion.major; }
    int minor() const { return mVersion.minor; }
    int patch() const { return mVersion.patch; }
    String versionString() const { return mVersion.str; }

    String target() const { return mKey.target; }

    Path path() const { return mKey.path; }

    bool isValid() { return mCompiler != plast::Unknown; }

private:
    plast::CompilerType mCompiler;
    struct {
        int major;
        int minor;
        int patch;
        String str;
    } mVersion;

    enum { FlagMask = CompilerArgs::HasDashM32 };
    struct PathKey {
        Path path;
        unsigned int flags;
        String target;

        bool operator<(const PathKey& other) const
        {
            if (path < other.path)
                return true;
            if (path > other.path)
                return false;
            if (target < other.target)
                return true;
            if (target > other.target)
                return false;
            return flags < other.flags;
        }
    } mKey;
    static Map<PathKey, SharedPtr> sVersions;
    static Map<plast::CompilerKey, WeakPtr> sVersionsByKey;

private:
    CompilerVersion(const Path& path, unsigned int flags, const String& target);
    CompilerVersion(const CompilerVersion&) = delete;
    CompilerVersion& operator=(const CompilerVersion&) = delete;
};

inline bool CompilerVersion::hasCompiler(plast::CompilerType compiler, int major, const String& target)
{
    return (version(compiler, major, target).get() != 0);
}

#endif

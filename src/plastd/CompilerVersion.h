#ifndef COMPILERVERSION_H
#define COMPILERVERSION_H

#include "CompilerArgs.h"
#include <rct/Map.h>
#include <rct/Hash.h>
#include <rct/Set.h>
#include <rct/Path.h>
#include <rct/List.h>
#include <rct/String.h>
#include <Plast.h>
#include <memory>
#include <cstdint>

class CompilerVersion
{
public:
    typedef std::shared_ptr<CompilerVersion> SharedPtr;
    typedef std::weak_ptr<CompilerVersion> WeakPtr;

    static SharedPtr version(const Path& path, uint32_t flags = 0, const String& target = String());
    static SharedPtr version(plast::CompilerType compiler, int32_t major, const String& target);
    static bool hasCompiler(plast::CompilerType compiler, int32_t major, const String& target);

    plast::CompilerType compiler() const { return mCompiler; }

    int32_t major() const { return mVersion.major; }
    int32_t minor() const { return mVersion.minor; }
    int32_t patch() const { return mVersion.patch; }
    String versionString() const { return mVersion.str; }

    String target() const { return mKey.target; }
    Set<String> multiLibs() const { return mMultiLibs; }
    List<String> extraArgs() const { return mExtraArgs; }
    void setExtraArgs(const List<String>& extra) { mExtraArgs = extra; }

    Path path() const { return mKey.path; }

    bool isValid() { return mCompiler != plast::Unknown; }

private:
    plast::CompilerType mCompiler;
    struct {
        int32_t major;
        int32_t minor;
        int32_t patch;
        String str;
    } mVersion;
    Set<String> mMultiLibs;
    List<String> mExtraArgs;

    enum { FlagMask = CompilerArgs::HasDashM32 };
    struct PathKey {
        Path path;
        uint32_t flags;
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
    CompilerVersion(const Path& path, uint32_t flags, const String& target);
    CompilerVersion(const CompilerVersion&) = delete;
    CompilerVersion& operator=(const CompilerVersion&) = delete;
};

inline bool CompilerVersion::hasCompiler(plast::CompilerType compiler, int32_t major, const String& target)
{
    return (version(compiler, major, target).get() != 0);
}

#endif

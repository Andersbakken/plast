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

    enum Type {
        Unknown,
        Clang,
        GCC
    };

    enum Bits {
        Bits_32 = 0x1,
        Bits_64 = 0x2
    };

    struct Key
    {
        Key()
            : major(0), minor(0), patch(0), bits(Bits_32), type(Unknown)
        {}
        int32_t major, minor, patch;
        Bits bits;
        Type type;
        String target;

        bool operator==(const Key &other) const
        {
            return (major == other.major
                    && minor == other.minor
                    && patch == other.patch
                    && bits == other.bits
                    && type == other.type
                    && target == other.target);
        }
        bool operator!=(const Key &other) const { return !operator==(other); }
        bool operator<(const Key &other) const
        {
            if (major < other.major)
                return true;
            if (major > other.major)
                return false;
            if (minor < other.minor)
                return true;
            if (minor > other.minor)
                return false;
            if (patch < other.patch)
                return true;
            if (patch > other.patch)
                return false;
            if (bits < other.bits)
                return true;
            if (bits > other.bits)
                return false;
            if (type < other.type)
                return true;
            if (type > other.type)
                return false;
            return target < other.target;
        }
    };

    Key key() const { return mKey; }
    int32_t major() const { return mKey.major; }
    int32_t minor() const { return mKey.minor; }
    int32_t patch() const { return mKey.patch; }
    String versionString() const { return mVersionString; }
    Bits bits() const { return mKey.bits; }
    Type type() const { return mKey.type; }

    String target() const { return mKey.target; }
    Set<String> multiLibs() const { return mMultiLibs; }
    List<String> extraArgs() const { return mExtraArgs; }
    void setExtraArgs(const List<String>& extra) { mExtraArgs = extra; }

    Path path() const { return mPath; }
    bool isValid() { return mKey.type != Unknown; }

    SharedPtr clone() const
    {
        return SharedPtr(new CompilerVersion(mKey, mVersionString, mPath, mMultiLibs, mExtraArgs));
    }

    static SharedPtr create(const std::shared_ptr<CompilerArgs> &args);
    static void create(const Path &path, const List<String> &targets = List<String>());
    static SharedPtr find(const Key &key);
    static SharedPtr create(const Key &key, const String &versionString, const Set<String> &multiLibs,
                            const List<String> &extraArgs, const Path &path);
    static void loadDB(const Path &path);
    static void saveDB(const Path &path);
private:
    Key mKey;
    String mVersionString;
    Path mPath;
    Set<String> mMultiLibs;
    List<String> mExtraArgs;

    static Map<Path, List<CompilerVersion::WeakPtr> > sByPath;
    static Map<Key, CompilerVersion::SharedPtr> sByKey;
private:
    CompilerVersion(const Key &key, const String &versionString,
                    const Path &path, const Set<String> &multiLibs,
                    const List<String> &extraArgs);
    CompilerVersion(const CompilerVersion&) = delete;
    CompilerVersion& operator=(const CompilerVersion&) = delete;
};

inline Serializer &operator<<(Serializer &s, const CompilerVersion::Key &key)
{
    s << key.major << key.minor << key.patch << static_cast<uint8_t>(key.bits)
      << static_cast<uint8_t>(key.type) << key.target;
    return s;
}

inline Deserializer &operator>>(Deserializer &d, CompilerVersion::Key &key)
{
    uint8_t bits, type;
    d >> key.major >> key.minor >> key.patch >> bits >> type >> key.target;
    key.bits = static_cast<CompilerVersion::Bits>(bits);
    key.type = static_cast<CompilerVersion::Type>(type);
    return d;
}

inline Serializer &operator<<(Serializer &s, const CompilerVersion::SharedPtr &version)
{
    assert(version);
    s << version->key() << version->versionString() << version->multiLibs() << version->extraArgs() << version->path();
    return s;
}

inline Deserializer &operator>>(Deserializer &d, CompilerVersion::SharedPtr &version)
{
    assert(!version);
    CompilerVersion::Key key;
    d >> key;
    version = CompilerVersion::find(key);
    String versionString;
    Set<String> multiLibs;
    List<String> extraArgs;
    Path path;
    d >> versionString >> multiLibs >> extraArgs >> path;
    if (!version) {
        version = CompilerVersion::create(key, versionString, multiLibs, extraArgs, path);
        assert(version);
    }
    return d;
    return d;
}

inline bool CompilerVersion::hasCompiler(plast::CompilerType compiler, int32_t major, const String& target)
{
    return (version(compiler, major, target).get() != 0);
}

#endif

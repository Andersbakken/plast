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
        Bits_None = 0x0,
        Bits_32 = 0x1,
        Bits_64 = 0x2
    };

    bool operator==(const CompilerVersion &other) const
    {
        return (mMajorVersion == other.mMajorVersion
                && mMinorVersion == other.mMinorVersion
                && mPatchVersion == other.mPatchVersion
                && mBits == other.mBits
                && mType == other.mType
                && mTarget == other.mTarget);
    }
    bool operator!=(const CompilerVersion &other) const { return !operator==(other); }
    bool operator<(const CompilerVersion &other) const
    {
        if (mMajorVersion < other.mMajorVersion)
            return true;
        if (mMajorVersion > other.mMajorVersion)
            return false;
        if (mMinorVersion < other.mMinorVersion)
            return true;
        if (mMinorVersion > other.mMinorVersion)
            return false;
        if (mPatchVersion < other.mPatchVersion)
            return true;
        if (mPatchVersion > other.mPatchVersion)
            return false;
        if (mBits < other.mBits)
            return true;
        if (mBits > other.mBits)
            return false;
        if (mType < other.mType)
            return true;
        if (mType > other.mType)
            return false;
        return mTarget < other.mTarget;
    }


    int32_t majorVersion() const { return mMajorVersion; }
    int32_t minorVersion() const { return mMinorVersion; }
    int32_t patchVersion() const { return mPatchVersion; }
    String versionString() const { return mVersionString; }
    Bits bits() const { return mBits; }
    Type type() const { return mType; }

    String target() const { return mTarget; }
    Set<String> multiLibs() const { return mMultiLibs; }
    List<String> extraArgs() const { return mExtraArgs; }
    void setExtraArgs(const List<String>& extra) { mExtraArgs = extra; }

    Path path() const { return mPath; }
    bool isValid() { return mType != Unknown; }

    static CompilerVersion create(const std::shared_ptr<CompilerArgs> &args);
    static void create(const Path &path, const List<String> &targets = List<String>());
    static bool resolve(CompilerVersion &version);
    static void loadDB(const Path &path);
    static void saveDB(const Path &path);
private:
    int32_t mMajorVersion, mMinorVersion, mPatchVersion;
    Bits mBits;
    Type mType;
    String mTarget;
    String mVersionString;
    Path mPath;
    Set<String> mMultiLibs;
    List<String> mExtraArgs;

    // static Map<Path, List<CompilerVersion::WeakPtr> > sByPath;
    // static Map<Key, List<CompilerVersion::SharedPtr> > sByKey;
    friend Serializer &operator<<(Serializer &, const CompilerVersion &);
    friend Deserializer &operator>>(Deserializer &, CompilerVersion &);
private:
    CompilerVersion();
    CompilerVersion(const CompilerVersion&) = delete;
    CompilerVersion& operator=(const CompilerVersion&) = delete;
};

inline Serializer &operator<<(Serializer &s, const CompilerVersion &version)
{
    s << version.mMajorVersion << version.mMinorVersion << version.mPatchVersion << static_cast<uint8_t>(version.mBits)
      << static_cast<uint8_t>(version.mType) << version.mTarget
      << version.mVersionString << version.mMultiLibs << version.mExtraArgs << version.mPath;
    return s;
}

inline Deserializer &operator>>(Deserializer &d, CompilerVersion &version)
{
    uint8_t bits, type;
    d >> version.mMajorVersion >> version.mMinorVersion >> version.mPatchVersion >> bits >> type >> version.mTarget
      >> version.mVersionString >> version.mMultiLibs >> version.mExtraArgs >> version.mPath;

    version.mBits = static_cast<CompilerVersion::Bits>(bits);
    version.mType = static_cast<CompilerVersion::Type>(type);
    return d;
}

#endif

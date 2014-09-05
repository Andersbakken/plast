/* This file is part of Plast.

   Plast is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Plast is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Plast.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef Plast_h
#define Plast_h

#include <rct/Message.h>

namespace Plast {
bool init();
Path defaultSocketFile();
enum {
    DefaultServerPort = 5160,
    DefaultDaemonPort = 5161,
    DefaultDiscoveryPort = 5162
};
}

class Compiler
{
public:
    static Path resolve(const Path &path);
    static std::shared_ptr<Compiler> compiler(const Path& executable, const String& path = String());
    static std::shared_ptr<Compiler> compilerBySha256(const String &sha256) { return sBySha.value(sha256); }
    static void insert(const Path &executable, const String &sha256, const Set<Path> &files);
    static String dump();
    static void cleanup();
    static int count() { return sBySha.size(); }

    String sha256() const { return mSha256; }
    Path path() const { return mPath; }
    Set<Path> files() const { return mFiles; }
    bool isValid() const { return !mPath.isEmpty(); }
private:
    static void ensureEnviron();

    static Hash<String, std::shared_ptr<Compiler> > sBySha, sByPath;
    static List<String> sEnviron;

private:
    String mSha256;
    Path mPath;
    Set<Path> mFiles;
};


struct CompilerArgs
{
    List<String> commandLine;
    List<int> sourceFileIndexes;
    List<Path> sourceFiles() const
    {
        List<Path> ret;
        ret.reserve(sourceFileIndexes.size());
        for (int idx : sourceFileIndexes) {
            ret.append(commandLine.at(idx));
        }
        return ret;
    }

    enum Mode {
        Compile,
        Preprocess,
        Link
    } mode;

    const char *modeName() const
    {
        switch (mode) {
        case Compile: return "compile";
        case Preprocess: return "preprocess";
        case Link: return "link";
        }
        return "";
    }

    enum Flag {
        None = 0x0000,
        NoAssemble = 0x0001,
        MultiSource = 0x0002,
        HasOutput = 0x0004,
        HasDashX = 0x0008,
        StdinInput = 0x0010,
        CPlusPlus = 0x0020,
        C = 0x0040,
        CPreprocessed = 0x0080,
        CPlusPlusPreprocessed = 0x0100,
        ObjectiveC = 0x0200,
        AssemblerWithCpp = 0x0400,
        Assembler = 0x0800,
        LanguageMask = CPlusPlus|C|CPreprocessed|CPlusPlusPreprocessed|ObjectiveC|AssemblerWithCpp|Assembler
    };
    static const char *languageName(Flag flag);
    unsigned int flags;

    static std::shared_ptr<CompilerArgs> create(const List<String> &args);

    Path sourceFile(int idx = 0) const { return commandLine.value(sourceFileIndexes.value(idx, -1)); }
};

inline Serializer &operator<<(Serializer &serializer, const CompilerArgs &args)
{
    serializer << args.commandLine << args.sourceFileIndexes << static_cast<uint8_t>(args.mode) << static_cast<uint32_t>(args.flags);
    return serializer;
}

inline Deserializer &operator>>(Deserializer &deserializer, CompilerArgs &args)
{
    uint8_t mode;
    deserializer >> args.commandLine >> args.sourceFileIndexes >> mode >> args.flags;
    args.mode = static_cast<CompilerArgs::Mode>(mode);
    return deserializer;
}

struct Host
{
    String address;
    uint16_t port;
    String friendlyName;
    String toString() const { return String::format<128>("%s:%d", friendlyName.isEmpty() ? address.constData() : friendlyName.constData(), port); }
    bool operator==(const Host &other) const { return address == other.address && port == other.port; }
    bool operator<(const Host &other) const
    {
        const int cmp = address.compare(other.address);
        return cmp < 0 || (!cmp && port < other.port);
    }
};

namespace std
{
template <> struct hash<Host> : public unary_function<Host, size_t>
{
    size_t operator()(const Host& value) const
    {
        std::hash<String> h1;
        std::hash<uint16_t> h2;
        return h1(value.address) | h2(value.port);
    }
};
}

inline Serializer &operator<<(Serializer &serializer, const Host &host)
{
    serializer << host.address << host.port << host.friendlyName;
    return serializer;
}

inline Deserializer &operator>>(Deserializer &deserializer, Host &host)
{
    deserializer >> host.address >> host.port >> host.friendlyName;
    return deserializer;
}

struct Output {
    enum Type {
        StdOut,
        StdErr
    };
    Type type;
    String text;
};

enum {
    HandshakeMessageId = 100,
    DaemonListMessageId,
    ClientJobMessageId,
    ClientJobResponseMessageId,
    QuitMessageId,
    DaemonJobAnnouncementMessageId,
    CompilerMessageId,
    CompilerRequestMessageId,
    JobRequestMessageId,
    JobMessageId,
    JobResponseMessageId
};

class HandshakeMessage : public Message
{
public:
    enum { MessageId = HandshakeMessageId };
    HandshakeMessage(const String &f = String(), uint16_t port = 0, int c = -1)
        : Message(MessageId), mFriendlyName(f), mPort(port), mCapacity(c)
    {}

    String friendlyName() const { return mFriendlyName; }
    uint16_t port() const { return mPort; }
    int capacity() const { return mCapacity; }

    virtual void encode(Serializer &serializer) const { serializer << mFriendlyName << mPort << mCapacity; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mFriendlyName >> mPort >> mCapacity; }
private:
    String mFriendlyName;
    uint16_t mPort;
    int mCapacity;
};

class DaemonListMessage : public Message
{
public:
    enum { MessageId = DaemonListMessageId };
    DaemonListMessage(const Set<Host> &hosts = Set<Host>())
        : Message(MessageId), mHosts(hosts)
    {}

    const Set<Host> &hosts() const { return mHosts; }
    virtual void encode(Serializer &serializer) const { serializer << mHosts; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mHosts; }
private:
    Set<Host> mHosts;
};

class ClientJobMessage : public Message
{
public:
    enum { MessageId = ClientJobMessageId };

    ClientJobMessage(const std::shared_ptr<CompilerArgs> &args = std::shared_ptr<CompilerArgs>(),
                     const Path &resolvedCompiler = Path(),
                     const List<String> &environ = List<String>(),
                     const Path &cwd = Path())
        : Message(MessageId), mArguments(args), mResolvedCompiler(resolvedCompiler), mEnviron(environ), mCwd(cwd)
    {
    }

    const std::shared_ptr<CompilerArgs> &arguments() const { return mArguments; }
    const Path &resolvedCompiler() const { return mResolvedCompiler; }
    const List<String> &environ() const { return mEnviron; }
    const Path &cwd() const { return mCwd; }

    virtual void encode(Serializer &serializer) const
    {
        serializer << *mArguments << mResolvedCompiler << mEnviron << mCwd;;
    }
    virtual void decode(Deserializer &deserializer)
    {
        mArguments.reset(new CompilerArgs);
        deserializer >> *mArguments >> mResolvedCompiler >> mEnviron >> mCwd;
    }
private:
    std::shared_ptr<CompilerArgs> mArguments;
    Path mResolvedCompiler;
    List<String> mEnviron;
    Path mCwd;
};

class ClientJobResponseMessage : public Message
{
public:
    enum { MessageId = ClientJobResponseMessageId };

    ClientJobResponseMessage(int status = -1, const List<Output> &output = List<Output>())
        : Message(MessageId), mStatus(status), mOutput(output)
    {
    }

    int status() const { return mStatus; }
    const List<Output> &output() const { return mOutput; }
    virtual void encode(Serializer &serializer) const;
    virtual void decode(Deserializer &deserializer);
private:
    int mStatus;
    List<Output> mOutput;
};

class QuitMessage : public Message
{
public:
    enum { MessageId = QuitMessageId };
    QuitMessage()
        : Message(MessageId)
    {}
};

class JobAnnouncementMessage : public Message
{
public:
    enum { MessageId = DaemonJobAnnouncementMessageId };
    JobAnnouncementMessage(const Set<String> &announcement = Set<String>())
        : Message(MessageId), mAnnouncement(announcement)
    {}

    const Set<String> &announcement() const { return mAnnouncement; }

    virtual void encode(Serializer &serializer) const { serializer << mAnnouncement; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mAnnouncement; }
private:
    Set<String> mAnnouncement;
};

class CompilerPackage;

class CompilerMessage : public Message
{
public:
    enum { MessageId = CompilerMessageId };
    CompilerMessage(const std::shared_ptr<Compiler> &compiler = std::shared_ptr<Compiler>());
    ~CompilerMessage();

    virtual void encode(Serializer &serializer) const;
    virtual void decode(Deserializer &deserializer);
    Path compiler() const { return mCompiler; }
    String sha256() const { return mSha256; }

    bool isValid() const { return mPackage != 0; }

    bool writeFiles(const Path& path) const;
private:
    CompilerPackage* loadCompiler(const Path &compiler, const Set<Path> &paths);

private:
    static Hash<String, CompilerPackage*> sPackages; // keyed on sha256

    Path mCompiler;
    String mSha256;
    CompilerPackage* mPackage;
};

class CompilerRequestMessage : public Message
{
public:
    enum { MessageId = CompilerRequestMessageId };
    CompilerRequestMessage(const String &sha256 = String())
        : Message(MessageId), mSha256(sha256)
    {
    }

    const String &sha256() const { return mSha256; }
    virtual void encode(Serializer &serializer) const { serializer << mSha256; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mSha256; }
private:
    String mSha256;
};

class JobRequestMessage : public Message
{
public:
    enum { MessageId = JobRequestMessageId };
    JobRequestMessage(uint64_t id = 0, const String &sha256 = String())
        : Message(MessageId), mId(id), mSha256(sha256)
    {}

    uint64_t id() const { return mId; }
    const String &sha256() const { return mSha256; }
    virtual void encode(Serializer &serializer) const { serializer << mId << mSha256; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mId >> mSha256; }
private:
    uint64_t mId;
    String mSha256;
};

class JobMessage : public Message
{
public:
    enum { MessageId = JobMessageId };
    JobMessage(uint64_t id = 0,
               const String &preprocessed = String(),
               const List<String> &args = List<String>())
        : Message(MessageId, Compressed), mId(id), mPreprocessed(preprocessed), mArgs(args)
    {}

    uint64_t id() const { return mId; }
    const String &preprocessed() const { return mPreprocessed; }
    const List<String> &args() const { return mArgs; }
    virtual void encode(Serializer &serializer) const { serializer << mId << mPreprocessed << mArgs; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mId >> mPreprocessed >> mArgs; }
private:
    uint64_t mId;
    String mPreprocessed;
    List<String> mArgs;
};

class JobResponseMessage : public Message
{
public:
    enum { MessageId = JobResponseMessageId };
    JobResponseMessage(uint64_t id = 0, const Hash<Path, String> &files = Hash<Path, String>())
        : Message(MessageId, Compressed), mId(id), mFiles(files)
    {}

    uint64_t id() const { return mId; }
    const Hash<Path, String> &files() const { return mFiles; }
    virtual void encode(Serializer &serializer) const { serializer << mId << mFiles; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mId >> mFiles; }
private:
    uint64_t mId;
    Hash<Path, String> mFiles;
};

#endif

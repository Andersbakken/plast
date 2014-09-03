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
#include "Compiler.h"

namespace Plast {
Path resolveCompiler(const Path &path);
bool init();
Path defaultSocketFile();
enum {
    DefaultServerPort = 5160,
    DefaultDaemonPort = 5161,
    DefaultDiscoveryPort = 5162
};
}


struct CompilerArgs
{
    List<String> arguments;
    List<Path> sourceFiles;

    Path output, compiler;
    enum Mode {
        Compile,
        Preprocess,
        Link
    } mode;

    enum Flag {
        None = 0x0,
        NoAssemble = 0x1,
        MultiSource = 0x2,
        HasOutput = 0x4
    };
    unsigned int flags;

    static CompilerArgs create(const List<String> &args);

    Path sourceFile(int idx = 0) const { return sourceFiles.value(idx); }
};

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
    DaemonJobRequestMessageId,
    DaemonJobResponseMessageId
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

    ClientJobMessage(int argc = 0, char **argv = 0, const List<String> &environ = List<String>(), const Path &cwd = Path())
        : Message(MessageId), mEnviron(environ), mCwd(cwd)
    {
        mArguments.resize(argc);
        for (int i=0; i<argc; ++i)
            mArguments[i] = argv[i];
    }

    const List<String> &arguments() const { return mArguments; }
    const List<String> &environ() const { return mEnviron; }
    const Path &cwd() const { return mCwd; }

    virtual void encode(Serializer &serializer) const { serializer << mArguments << mEnviron << mCwd;; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mArguments >> mEnviron >> mCwd; }
private:
    List<String> mArguments, mEnviron;
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

class DaemonJobAnnouncementMessage : public Message
{
public:
    enum { MessageId = DaemonJobAnnouncementMessageId };
    DaemonJobAnnouncementMessage(const Hash<String, int> &announcement = Hash<String, int>())
        : Message(MessageId), mAnnouncement(announcement)
    {}

    const Hash<String, int> &announcement() const { return mAnnouncement; }

    virtual void encode(Serializer &serializer) const { serializer << mAnnouncement; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mAnnouncement; }
private:
    Hash<String, int> mAnnouncement;
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

    bool writeFiles(const Path& path);

private:
    CompilerPackage* loadCompiler(const Set<Path> &paths);

private:
    static Map<String, CompilerPackage*> sPackages; // keyed on sha256

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

class DaemonJobRequestMessage : public Message
{
public:
    enum { MessageId = DaemonJobRequestMessageId };
    DaemonJobRequestMessage(uint64_t id = 0, const String &sha256 = String())
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

class DaemonJobResponseMessage : public Message
{
public:
    enum { MessageId = DaemonJobResponseMessageId };
    DaemonJobResponseMessage(uint64_t id = 0,
                             const String &preprocessed = String(),
                             const List<String> &args = List<String>())
        : Message(MessageId), mId(id), mPreprocessed(preprocessed), mArgs(args)
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

#endif

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
Path resolveCompiler(const Path &path);
bool init();
Path defaultSocketFile();
enum {
    DefaultServerPort = 5160,
    DefaultDaemonPort = 5161,
    DefaultDiscoveryPort = 5162
};
}

struct Host
{
    String address;
    uint16_t port;
    String toString() const { return String::format<128>("%s:%d", address.constData(), port); }
    bool operator==(const Host &other) const { return address == other.address && port == other.port; }
    bool operator<(const Host &other) const
    {
        const int cmp = address.compare(other.address);
        return cmp < 0 || (!cmp && port < other.port);
    }
};
struct Output {
    enum Type {
        StdOut,
        StdErr
    };
    Type type;
    String text;
};

class HandshakeMessage : public Message
{
public:
    enum { MessageId = 100 };
    HandshakeMessage(const String &h = String(), int c = -1)
        : Message(MessageId), mHostName(h), mCapacity(c)
    {}

    String hostName() const { return mHostName; }
    int capacity() const { return mCapacity; }

    virtual void encode(Serializer &serializer) const { serializer << mHostName << mCapacity; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mHostName >> mCapacity; }
private:
    String mHostName;
    int mCapacity;
};

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

class ClientJobMessage : public Message
{
public:
    enum { MessageId = HandshakeMessage::MessageId + 1 };

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
    enum { MessageId = ClientJobMessage::MessageId + 1 };

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
    enum { MessageId = ClientJobResponseMessage::MessageId + 1 };
    QuitMessage()
        : Message(MessageId)
    {}
};

class DaemonJobAnnouncementMessage : public Message
{
public:
    enum { MessageId = QuitMessage::MessageId + 1 };
    DaemonJobAnnouncementMessage(const Hash<String, int> &announcement = Hash<String, int>())
        : Message(MessageId), mAnnouncement(announcement)
    {}

    const Hash<String, int> &announcement() const { return mAnnouncement; }

    virtual void encode(Serializer &serializer) const { serializer << mAnnouncement; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mAnnouncement; }
private:
    Hash<String, int> mAnnouncement;
};

class ServerJobAnnouncementMessage : public Message
{
public:
    enum { MessageId = DaemonJobAnnouncementMessage::MessageId + 1 };
    ServerJobAnnouncementMessage(int count = 0, const String &sha256 = String(), const Path &compiler = Path(),
                                 const String &host = String(), uint16_t port = 0)
        : Message(MessageId), mCount(0), mSha256(sha256), mCompiler(compiler), mHost(host), mPort(port)
    {}

    int count() const { return mCount; }
    const String &sha256() const { return mSha256; }
    const Path &compiler() const { return mCompiler; }

    const String &host() const { return mHost; }
    uint16_t port() const { return mPort; }

    virtual void encode(Serializer &serializer) const { serializer << mCount << mSha256 << mCompiler; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mCount >> mSha256 >> mCompiler; }
private:
    int mCount;
    String mSha256;
    Path mCompiler;
    String mHost;
    uint16_t mPort;
};

class CompilerPackage;

class CompilerMessage : public Message
{
public:
    enum { MessageId = ServerJobAnnouncementMessage::MessageId + 1 };
    CompilerMessage(const String& sha256 = String()) : Message(MessageId), mPackage(0) { mSha256 = sha256; }
    CompilerMessage(const Path &compiler, const Set<Path> &paths, const String &sha256);
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
    static Map<Path, CompilerPackage*> sPackages;

    Path mCompiler;
    String mSha256;
    CompilerPackage* mPackage;
};

class CompilerRequestMessage : public Message
{
public:
    enum { MessageId = CompilerMessage::MessageId + 1 };
    CompilerRequestMessage(const String &sha256 = String())
        : Message(MessageId)
    {}

    const String &sha256() const { return mSha256; }
    virtual void encode(Serializer &serializer) const { serializer << mSha256; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mSha256; }
private:
    String mSha256;
};

class DaemonJobRequestMessage : public Message
{
public:
    enum { MessageId = CompilerRequestMessage::MessageId + 1 };
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
    enum { MessageId = DaemonJobRequestMessage::MessageId + 1 };
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

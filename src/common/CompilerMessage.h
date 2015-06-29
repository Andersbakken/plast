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

#ifndef CompilerMessage_h
#define CompilerMessage_h

#include <rct/Message.h>
#include <rct/Flags.h>
#include "Plast.h"
#include <CompilerVersion.h>

class CompilerMessage : public Message
{
public:
    enum { MessageId = plast::CompilerMessageId };

    struct FileData {
        mode_t mode;
        enum Flag {
            File = 0x1,
            Link = 0x2,
            Compiler = 0x4
        };
        Flags<Flag> flags;
        String contents;
    };

    CompilerMessage(const CompilerVersion::SharedPtr &version, const Hash<Path, FileData> &contents = Hash<Path, FileData>())
        : Message(MessageId, Compressed), mVersion(version), mContents(contents)
    {
        assert(version);
    }
    virtual void encode(Serializer &serializer) const;
    virtual void decode(Deserializer &deserializer);

    bool isValid() const { return mVersion.get(); }
    const Hash<Path, FileData> &contents() const { return mContents; }

private:
    CompilerVersion::SharedPtr mVersion;
    Hash<Path, FileData> mContents;
};

RCT_FLAGS(CompilerMessage::FileData::Flag);

inline Serializer &operator<<(Serializer &s, const CompilerMessage::FileData &fileData)
{
    s << static_cast<uint32_t>(fileData.mode) << fileData.flags << fileData.contents;
    return s;
}

inline Deserializer &operator>>(Deserializer &d, CompilerMessage::FileData &fileData)
{
    uint32_t mode;
    d >> mode >> fileData.flags >> fileData.contents;
    fileData.mode = static_cast<mode_t>(mode);
    return d;
}

inline void CompilerMessage::encode(Serializer &serializer) const
{
    serializer << mVersion << mContents;
}

inline void CompilerMessage::decode(Deserializer &deserializer)
{
    deserializer >> mVersion >> mContents;
}


#endif


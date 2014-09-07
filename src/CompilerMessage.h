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
#include "Plast.h"
#include "Compiler.h"

class CompilerMessage : public Message
{
public:
    enum { MessageId = Plast::CompilerMessageId };
    CompilerMessage(const Path &compiler = Path(), const String &sha256 = String(),
                    const Hash<Path, std::pair<String, uint32_t> > &contents = Hash<Path, std::pair<String, uint32_t> >())
        : Message(MessageId, Compressed), mCompiler(compiler), mSha256(sha256), mContents(contents)
    {
    }
    virtual void encode(Serializer &serializer) const { serializer << mCompiler << mSha256 << mContents; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mCompiler >> mSha256 >> mContents; }

    Path compiler() const { return mCompiler; }
    String sha256() const { return mSha256; }

    bool isValid() const { return !mContents.isEmpty(); }
    Hash<Path, std::pair<String, uint32_t> > contents() const { return mContents; }

private:
    Path mCompiler;
    String mSha256;
    Hash<Path, std::pair<String, uint32_t> > mContents;
};

#endif

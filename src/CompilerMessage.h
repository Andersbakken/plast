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

class CompilerPackage;
class CompilerMessage : public Message
{
public:
    enum { MessageId = Plast::CompilerMessageId };
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

#endif

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

#ifndef ClientJobMessage_h
#define ClientJobMessage_h

#include <rct/Message.h>
#include "Plast.h"
#include "CompilerArgs.h"

class ClientJobMessage : public Message
{
public:
    enum { MessageId = Plast::ClientJobMessageId };

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

#endif

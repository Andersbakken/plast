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

#ifndef JobMessage_h
#define JobMessage_h

#include <rct/Message.h>
#include "Plast.h"

class JobMessage : public Message
{
public:
    enum { MessageId = Plast::JobMessageId };
    JobMessage(uint64_t id = 0,
               const String &sha = String(),
               const String &preprocessed = String(),
               const std::shared_ptr<CompilerArgs> &args = std::shared_ptr<CompilerArgs>())
        : Message(MessageId, Compressed), mId(id), mSha256(sha), mPreprocessed(preprocessed), mArgs(args)
    {}
    // empty preprocessed means there's no compilerArgs and that we didn't have jobs
    uint64_t id() const { return mId; }
    const String &sha256() const { return mSha256; }
    const String &preprocessed() const { return mPreprocessed; }
    const std::shared_ptr<CompilerArgs> &args() const { return mArgs; }
    virtual void encode(Serializer &serializer) const
    {
        serializer << mId << mSha256 << mPreprocessed;
        if (!mPreprocessed.isEmpty()) {
            assert(mArgs);
            serializer << *mArgs;
        }
    }
    virtual void decode(Deserializer &deserializer)
    {
        deserializer >> mId >> mSha256 >> mPreprocessed;
        if (!mPreprocessed.isEmpty()) {
            mArgs.reset(new CompilerArgs);
            deserializer >> *mArgs;
        }
    }

private:
    uint64_t mId;
    String mSha256;
    String mPreprocessed;
    std::shared_ptr<CompilerArgs> mArgs;
};

#endif

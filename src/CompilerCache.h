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

#ifndef CompilerCache_h
#define CompilerCache_h

#include "Compiler.h"
#include <rct/Path.h>
#include <rct/Set.h>
#include <rct/Hash.h>
#include <rct/LinkedList.h>
#include <memory>

class CompilerCache
{
public:
    CompilerCache(const Path &path, int maxCacheSize = 10);
    Path path() const { return mPath; }
    std::shared_ptr<Compiler> findByPath(const Path &executable) const { return mByPath.value(executable); }
    std::shared_ptr<Compiler> findBySha256(const String &sha256) const { return mBySha256.value(sha256); }
    std::shared_ptr<Compiler> create(const Path &executable);
    std::shared_ptr<Compiler> create(const Path &executable, const String &sha256, const Hash<Path, std::pair<String, uint32_t> > &files);
    int count() const { return mBySha256.size(); }
    String dump() const;
    Hash<Path, std::pair<String, uint32_t> > contentsForSha256(const String &sha256);
private:
    const Path mPath;
    const int mCacheSize;
    List<String> mEnviron;
    Hash<Path, std::shared_ptr<Compiler> > mByPath;
    Hash<String, std::shared_ptr<Compiler> > mBySha256;
    struct Cache {
        String sha256;
        Hash<Path, std::pair<String, uint32_t> > contents;
    };
    LinkedList<std::shared_ptr<Cache> > mContentsCache;
};

#endif

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

#ifndef Compiler_h
#define Compiler_h

#include <rct/Path.h>
#include <rct/Set.h>
#include <rct/Hash.h>
#include <memory>

class Compiler
{
public:
    String sha256() const { return mSha256; }
    Path path() const { return mPath; }
    Set<Path> files() const { return mFiles; }
    bool isValid() const { return !mPath.isEmpty(); }

    static Path resolve(const Path &path);
private:
    static void ensureEnviron();

    static Hash<String, std::shared_ptr<Compiler> > sBySha, sByPath;
    static List<String> sEnviron;

private:
    String mSha256;
    Path mPath;
    Set<Path> mFiles;
    friend class CompilerCache;
};

#endif

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

#ifndef COMPILER_H
#define COMPILER_H

#include <memory>
#include <rct/Hash.h>
#include <rct/List.h>
#include <rct/Path.h>
#include <rct/Set.h>
#include <rct/String.h>

class Compiler
{
public:
    static std::shared_ptr<Compiler> compiler(const Path& executable, const String& path = String());
    static std::shared_ptr<Compiler> compilerBySha256(const String &sha256) { return sBySha.value(sha256); }
    static void insert(const Path &executable, const String &sha256, const Set<Path> &files);
    static String dump();
    static void cleanup();

    String sha256() const { return mSha256; }
    Path path() const { return mPath; }
    Set<Path> files() const { return mFiles; }

private:
    static void ensureEnviron();

    static Hash<String, std::shared_ptr<Compiler> > sBySha, sByPath;
    static List<String> sEnviron;

private:
    String mSha256;
    Path mPath;
    Set<Path> mFiles;
};

#endif

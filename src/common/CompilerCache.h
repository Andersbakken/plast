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
#include "CompilerInvocation.h"
#include "CompilerVersion.h"
#include <rct/Path.h>
#include <rct/Hash.h>

class CompilerCache
{
public:
    CompilerCache(const Path &path);
    bool load();
    void clear();
    Path path() const { return mPath; }
    CompilerInvocation createInvocation(const std::shared_ptr<CompilerArgs> &args);
    bool contains(const CompilerVersion &args);
private:
    CompilerCache(const CompilerCache &) = delete;
    CompilerCache &operator=(const CompilerCache &) = delete;

    Map<CompilerVersion, std::shared_ptr<Compiler> > mCompilers;
    Path mPath;
};

#endif

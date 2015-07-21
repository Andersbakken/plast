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

#include "CompilerCache.h"

CompilerCache::CompilerCache(const Path &path)
    : mPath(path)
{
}

bool CompilerCache::load()
{
    // mPath.visit([this](const Path &path) {
    //         if (path.isDir())

    //     });
}

void CompilerCache::clear()
{
    mCompilers.clear();
    Rct::removeDirectory(mPath);
}

CompilerInvocation CompilerCache::createInvocation(const std::shared_ptr<CompilerArgs> &args)
{
}

bool CompilerCache::contains(const CompilerVersion &args)
{
    return mCompilers.contains(args);
}


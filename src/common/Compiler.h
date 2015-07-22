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

#include <rct/Flags.h>
#include <rct/Path.h>
#include <rct/Hash.h>

class Compiler
{
public:
    struct FileData {
        mode_t mode;
        enum Flag {
            Link = 0x1,
            Compiler = 0x2
        };
        Flags<Flag> flags;
        String contents;
    };

    Compiler(const Path &basePath = Path(), const Hash<Path, FileData> &files = Hash<Path, FileData>())
        : mBasePath(basePath), mFiles(files)
    {}
    bool init(const Path &executable);

    Hash<Path, FileData> files() const;
    Path basePath() const { return mBasePath; }
    Path executable() const
    {
        for (const auto &f : mFiles) {
            if (f.second.flags & FileData::Compiler)
                return f.first;
        }
        return Path();
    }

private:
    Compiler(const Compiler &) = delete;
    Compiler &operator=(const Compiler &) = delete;

    Path mBasePath;
    Hash<Path, FileData> mFiles;
    friend class CompilerCache;
};

#endif

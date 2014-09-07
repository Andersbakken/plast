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

#include "Compiler.h"
#include <rct/Process.h>
#include <rct/Log.h>
#include <rct/SHA256.h>

Path Compiler::resolve(const Path &path)
{
    const String fileName = path.fileName();
    const List<String> paths = String(getenv("PATH")).split(':');
    // error() << fileName;
    for (const auto &p : paths) {
        const Path orig = p + "/" + fileName;
        Path exec = orig;
        // error() << "Trying" << exec;
        if (exec.resolve()) {
            const char *fileName = exec.fileName();
            if (strcmp(fileName, "plastc") && strcmp(fileName, "gcc-rtags-wrapper.sh") && strcmp(fileName, "icecc")) {
                return orig;
            }
        }
    }
    return Path();
}


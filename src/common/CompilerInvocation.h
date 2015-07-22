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

#ifndef CompilerInvocation_h
#define CompilerInvocation_h

#include "Compiler.h"
#include "CompilerVersion.h"
#include <memory>
#include <rct/Process.h>

class CompilerInvocation
{
public:
    bool isValid() const { return mCompiler.get(); }
    std::shared_ptr<Compiler> compiler() const { return mCompiler; }
    const CompilerVersion &version() const { return mVersion; }
    std::shared_ptr<Process> invoke(const std::shared_ptr<CompilerArgs> &args);
private:
    CompilerInvocation(const std::shared_ptr<Compiler> &compiler = std::shared_ptr<Compiler>(),
                       const CompilerVersion &version = CompilerVersion())
        : mCompiler(compiler), mVersion(version)
    {}

    std::shared_ptr<Compiler> mCompiler;
    CompilerVersion mVersion;

    friend class CompilerCache;
};

#endif

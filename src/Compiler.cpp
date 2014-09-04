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
#include <rct/Log.h>
#include <rct/Process.h>
#include <rct/SHA256.h>

Hash<String, std::shared_ptr<Compiler> > Compiler::sBySha;
Hash<String, std::shared_ptr<Compiler> > Compiler::sByPath;
List<String> Compiler::sEnviron;

void Compiler::ensureEnviron()
{
    if (!sEnviron.isEmpty())
        return;
    sEnviron = Process::environment();
    for (int i = 0; i < sEnviron.size(); ++i) {
        if (sEnviron.at(i).startsWith("PATH=")) {
            sEnviron.removeAt(i);
            break;
        }
    }
}

std::shared_ptr<Compiler> Compiler::compiler(const Path &compiler, const String &path)
{
    ensureEnviron();

    auto it = sByPath.find(compiler);
    if (it != sByPath.end())
        return it->second;
    std::shared_ptr<Compiler> &c = sByPath[compiler];
    assert(!c);
    const Path resolved = compiler.resolved();
    std::shared_ptr<Compiler> &resolvedCompiler = sByPath[resolved];
    if (resolvedCompiler) {
        c = resolvedCompiler;
        return c;
    }
    SHA256 sha;
#ifdef OS_Linux
    c.reset(new Compiler);
    List<String> shaList;
    {
        Process process;
        if (!process.exec(resolved, List<String>() << "-print-prog-name=cc1")) {
            error() << "Couldn't invoke compiler" << resolved;
            c.reset();
            return std::shared_ptr<Compiler>();
        }
        const List<String> lines = process.readAllStdOut().split('\n');
        for (const Path &path : lines) {
            if (path.isFile()) {
                shaList.append(path.fileName());
                c->mFiles.insert(path);
            }
        }
    }
    {
        Process process;
        if (!process.exec(resolved, List<String>() << "-print-prog-name=cc1plus")) {
            error() << "Couldn't invoke compiler" << resolved;
            c.reset();
            return std::shared_ptr<Compiler>();
        }
        const List<String> lines = process.readAllStdOut().split('\n');
        for (const Path &path : lines) {
            if (path.isFile()) {
                shaList.append(path.fileName());
                c->mFiles.insert(path);
            }
        }
    }
    Process process;
    if (!process.exec("ldd", List<String>() << resolved)) {
        error() << "Couldn't ldd compiler" << resolved;
        return std::shared_ptr<Compiler>();
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    shaList.append(compiler.fileName());
    for (const String &line : lines) {
        const int idx = line.indexOf("=> /");
        if (idx == -1)
            continue;
        const int end = line.indexOf(' ', idx + 5);
        if (end == -1)
            continue;
        const Path unresolved = line.mid(idx + 3, end - idx - 3);
        const Path path = Path::resolved(unresolved);
        if (path.isFile()) {
            shaList.append(unresolved.fileName());
            c->mFiles.insert(unresolved);
        } else {
            error() << "Couldn't resolve path" << unresolved;
        }
    }
    shaList.sort();
    for (const String &s : shaList) {
        sha.update(s);
        error() << s;
    }
#elif defined(OS_Darwin)

#endif
    if (c) {
        c->mSha256 = sha.hash(SHA256::Hex);
        error() << "GOT SHA" << c->mSha256;
        c->mPath = compiler;
        sBySha[c->mSha256] = c;
        sByPath[compiler] = c;
        if (!resolvedCompiler)
            resolvedCompiler = c;
        warning() << "Created package" << compiler << c->mFiles << c->mSha256;
    }
    return c;
}

String Compiler::dump()
{
    String ret;
    for (const auto &compiler : sBySha) {
        if (!ret.isEmpty())
            ret << '\n';
        ret << (compiler.first + ":") << compiler.second->path();
        for (const auto &path : compiler.second->mFiles) {
            ret << "\n    " << path;
        }
    }
    return ret;
}
void Compiler::insert(const Path &executable, const String &sha256, const Set<Path> &files)
{
    std::shared_ptr<Compiler> &c = sBySha[sha256];
    assert(!c);
    assert(!sByPath.contains(executable));
    c.reset(new Compiler);
    c->mPath = executable;
    c->mSha256 = sha256;
    c->mFiles = files;
    sByPath[executable] = c;
}

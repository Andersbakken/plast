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
#include <rct/Process.h>
#include <rct/SHA256.h>
#include <rct/Log.h>
#include <rct/Rct.h>

CompilerCache::CompilerCache(const Path &path, int cacheSize)
    : mPath(path), mCacheSize(cacheSize)
{
    mEnviron = Process::environment();
    for (int i = 0; i < mEnviron.size(); ++i) {
        if (mEnviron.at(i).startsWith("PATH=")) {
            mEnviron.removeAt(i);
            break;
        }
    }

    for (const Path &p : mPath.files(Path::Directory)) {
        // error() << path;
        if (Path(p + "BAD").isFile()) {
            // ### not accessible by path
            mBySha256[p.name()].reset(new Compiler);
            continue;
        }
        List<String> shaList;
        Set<Path> files;
        for (const Path &file : p.files(Path::File)) {
            const char *fileName = file.fileName();
            if (strcmp(fileName, "COMPILER")) {
                shaList.append(fileName);
                files.insert(file);
                // error() << file.fileName();
            }
        }
        SHA256 sha;
        shaList.sort();
        for (const String &fn : shaList) {
            sha.update(fn);
        }
        const String sha256 = sha.hash(SHA256::Hex);
        if (sha256 != p.name()) {
            error() << "Invalid compiler" << p << sha256 << shaList;
            Path::rmdir(p);
        } else {
            const Path exec = Path::resolved(p + "/COMPILER");
            if (!exec.isFile()) {
                error() << "Can't find COMPILER symlink";
            } else {
                warning() << "Got compiler" << sha256 << p.fileName();
                std::shared_ptr<Compiler> compiler(new Compiler);
                compiler->mPath = exec;
                compiler->mSha256 = sha256;
                compiler->mFiles = files;
                mByPath[exec] = compiler;
                mBySha256[sha256] = compiler;
            }
        }
    }
}

static inline bool printProgName(const Path &path, const String &prog, Set<Path> &files, List<String> &shaList)
{
    Process process;
    if (!process.exec(path, List<String>() << "-print-prog-name=" + prog)) {
        error() << "Couldn't invoke compiler" << path;
        return false;
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    for (const Path &path : lines) {
        if (path.isFile()) {
            shaList.append(path.fileName());
            files.insert(path);
        } else if (!path.contains('/')) {
            const Path usrBinFile = "/usr/bin/" + path;
            if (usrBinFile.isFile()) {
                shaList.append(path);
                files.insert(usrBinFile);
            }
        }
    }
    return true;
}

std::shared_ptr<Compiler> CompilerCache::create(const Path &compiler)
{
    assert(!mByPath.contains(compiler));
    std::shared_ptr<Compiler> &c = mByPath[compiler];
    const Path resolved = compiler.resolved();
    std::shared_ptr<Compiler> &resolvedCompiler = mByPath[resolved];
    if (resolvedCompiler) {
        c = resolvedCompiler;
        return c;
    }
    SHA256 sha;
    c.reset(new Compiler);
    c->mFiles.insert(compiler);
    List<String> shaList;
    const char *progNames[] = { "as", "cc1", "cc1plus" };
    for (size_t i=0; i < sizeof(progNames) / sizeof(progNames[0]); ++i) {
        if (!printProgName(resolved, progNames[i], c->mFiles, shaList)) {
            c.reset();
            return c;
        }
    }
    Process process;
    if (!process.exec(
#ifdef OS_Darwin
            "otool", List<String>() << "-L" << "-X" << resolved
#else
            "ldd", List<String>() << resolved
#endif
            )) {
        error() << "Couldn't ldd compiler" << resolved;
        c.reset();
        return c;
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    shaList.append(compiler.fileName());
    for (const String &line : lines) {
        error() << line;
#ifdef OS_Darwin
        const int idx = line.indexOf('/');
        if (idx == -1) {
            continue;
        }
#else
        int idx = line.indexOf("=> /");
        if (idx == -1)
            continue;
        idx += 3;
#endif
        const int end = line.indexOf(' ', idx + 2);
        if (end == -1) {
            continue;
        }
        const Path unresolved = line.mid(idx, end - idx);
        const Path path = Path::resolved(unresolved);
        if (path.isFile()) {
            shaList.append(unresolved.fileName());
            c->mFiles.insert(unresolved);
        } else {
            error() << "Couldn't resolve path" << unresolved;
        }
    }
    if (c) {
        shaList.sort();
        for (const String &s : shaList) {
            sha.update(s);
            error() << s;
        }

        c->mSha256 = sha.hash(SHA256::Hex);
        debug() << "producing compiler" << c->mSha256 << shaList;

        error() << "GOT SHA" << c->mSha256;
        c->mPath = compiler;
        mBySha256[c->mSha256] = c;
        mByPath[compiler] = c;
        if (!resolvedCompiler)
            resolvedCompiler = c;
        warning() << "Created package" << compiler << c->mFiles << c->mSha256;
        const Path root = mPath + c->mSha256 + '/';
        Path::mkdir(root, Path::Recursive);
        bool ok = true;
        error() << root;
        for (const Path &file : c->mFiles) {
            error() << "Linking" << file << (root + file.fileName());
            if (symlink(file.constData(), (root + file.fileName()).constData())) {
                error() << "Failed to create symlink" << errno << strerror(errno) << (root + file.fileName()) << file;
                ok = false;
                break;
            }
        }

        if (ok && symlink(c->mPath.fileName(), (root + "COMPILER").constData())) {
            error() << "Failed to create symlink" << errno << strerror(errno) << (root + "COMPILER");
            ok = false;
        }


        error() << "SHIT" << ok;
        if (!ok) {
            for (const Path &f : root.files(Path::File))
                Path::rm(f);
            rmdir(root.constData());
        }
    }
    return c;
}

std::shared_ptr<Compiler> CompilerCache::create(const Path &executable, const String &sha256, const Hash<Path, std::pair<String, uint32_t> > &contents)
{
    const Path root = mPath + sha256 + '/';
    Path::mkdir(root, Path::Recursive);
    Set<Path> files;
    for (const auto &file : contents) {
        // const Path path = root + file.first;
        // Path::mkdir(path.parentDir(), Path::Recursive);
        const char *fileName = file.first.fileName();
        const Path path = root + fileName;
        // Path::mkdir(path.parentDir(), Path::Recursive);
        if (!Rct::writeFile(path, file.second.first, file.second.second)) {
            error() << "Couldn't write file" << path;
            for (const Path &f : root.files(Path::File))
                Path::rm(f);
            rmdir(root.constData());
            return std::shared_ptr<Compiler>();
        }
        files.insert(path);
    }
    const char *compilerFileName = executable.fileName();
    if (symlink(compilerFileName, (root + "COMPILER").constData())) {
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        rmdir(root.constData());
        error() << "Failed to create symlink" << errno << strerror(errno) << (root + "COMPILER");
        return std::shared_ptr<Compiler>();
    }
    const Path compilerPath = root + compilerFileName;
    error() << "chmoding" << compilerPath;
    if (chmod(compilerPath.constData(), S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) {
        error() << "Failed to chmod compiler" << errno << strerror(errno) << compilerPath;
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        rmdir(root.constData());
        return std::shared_ptr<Compiler>();
    }

    Process process;
    if (!process.exec(compilerPath, List<String>() << "-v")) { // compiler can't run. Cache as bad?
        for (const Path &f : root.files(Path::File))
            Path::rm(f);
        Rct::writeFile(root + "BAD", String());
        return std::shared_ptr<Compiler>();
    }
    warning() << "wrote compiler to" << root;

    std::shared_ptr<Compiler> compiler(new Compiler);
    compiler->mPath = compilerPath;
    compiler->mSha256 = sha256;
    compiler->mFiles = files;
    mByPath[compilerPath] = compiler;
    mBySha256[sha256] = compiler;
    return compiler;
}

String CompilerCache::dump() const
{
    String ret;
    for (const auto &compiler : mBySha256) {
        if (!ret.isEmpty())
            ret << '\n';
        ret << (compiler.first + ":");
        for (const auto &path : compiler.second->mFiles) {
            if (path == compiler.second->path()) {
                ret << "\n    " << path << "*";
            } else {
                ret << "\n    " << path;
            }
        }
    }
    return ret;
}
Hash<Path, std::pair<String, uint32_t> > CompilerCache::contentsForSha256(const String &sha256)
{
    for (auto it = mContentsCache.begin(); it != mContentsCache.end(); ++it) {
        if ((*it)->sha256 == sha256) {
            if (it != mContentsCache.begin()) {
                auto cache = *it;
                mContentsCache.erase(it);
                mContentsCache.insert(mContentsCache.begin(), cache);
            }
            return (*it)->contents;
        }
    }
    const auto &compiler = mBySha256[sha256];
    assert(compiler);
    std::shared_ptr<Cache> cache(new Cache);
    cache->sha256 = sha256;
    for (const auto &path : compiler->mFiles) {
        auto &ref = cache->contents[path.fileName()];
        mode_t mode;
        if (!Rct::readFile(path, ref.first, &mode)) {
            error() << "Failed to load file" << path;
            return Hash<Path, std::pair<String, uint32_t> >();
        }
        ref.second = static_cast<uint32_t>(mode);
    }
    assert(mContentsCache.size() <= mCacheSize);
    if (mContentsCache.size() == mCacheSize)
        mContentsCache.pop_back();
    mContentsCache.insert(mContentsCache.begin(), cache);
    return cache->contents;
}

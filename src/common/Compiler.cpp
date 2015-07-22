#include "Compiler.h"
#include <rct/Process.h>
#include <rct/Log.h>
#include <rct/Rct.h>

static inline bool printFileName(const Path &path, const String &prog, Hash<Path, Compiler::FileData> &files)
{
    Process process;
    if (!process.exec(path, List<String>() << "-print-file-name=" + prog)) {
        error() << "Couldn't invoke compiler" << path;
        return false;
    }
    const List<String> lines = process.readAllStdOut().split('\n', String::SkipEmpty);
    // error() << path << lines;
    for (Path p : lines) {
        if (!p.exists())
            p = "/usr/bin/" + p;
        if (p.isFile()) {
            auto &f = files[p];
            f.mode = p.mode();
            if (p.isSymLink()) {
                f.flags |= Compiler::FileData::Link;
            }
        } else {
            error() << "Can't resolve" << p << path << prog;
            return false;
        }
    }
    return true;
}

bool Compiler::init(const Path &compiler)
{
    {
        auto &f = mFiles[compiler];
        f.mode = compiler.mode();
        f.flags = FileData::Compiler;
    }
    const char *progNames[] = { "as", "cc1", "cc1plus", "liblto_plugin.so" }; //, "libopcodes-2.24-system.so", "libbfd-2.24-system.so" };
    for (size_t i=0; i < sizeof(progNames) / sizeof(progNames[0]); ++i) {
        if (!printFileName(compiler, progNames[i], mFiles)) {
            return false;
        }
    }
    Process process;
    if (!process.exec(
#ifdef OS_Darwin
            "otool", List<String>() << "-L" << "-X" << compiler
#else
            "ldd", List<String>() << compiler
#endif
            )) {
        error() << "Couldn't ldd compiler" << compiler;
        return false;
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    for (const String &line : lines) {
        // error() << line;
#ifdef OS_Darwin
        const int idx = line.indexOf('/');
        if (idx == -1) {
            continue;
        }
#else
        int idx = line.indexOf("=> /");
        if (idx == -1) {
            idx = 0;
            while (idx < line.size() && isspace(line.at(idx)))
                ++idx;
        } else {
            idx += 3;
        }
#endif
        const int end = line.indexOf(' ', idx + 2);
        if (end == -1) {
            continue;
        }
        const Path unresolved = line.mid(idx, end - idx);
        const Path path = Path::resolved(unresolved);
        if (path.isFile()) {
            auto &f = mFiles[unresolved];
            f.mode = unresolved.mode();
            if (unresolved.isSymLink()) {
                f.flags |= Compiler::FileData::Link;
            }
        } else {
            error() << "Couldn't resolve path" << unresolved;
        }
    }


    // auto fisk = files();
    // Rct::removeDirectory("/tmp/balle");
    // for (const auto &f : fisk) {
    //     const Path p = "/tmp/balle" + f.first;
    //     Path::mkdir(p.parentDir(), Path::Recursive);
    //     // error() << f.first << (f.second.flags & FileData::Link);
    //     if (f.second.flags & FileData::Link) {
    //         symlink(f.second.contents.constData(), p.constData());
    //     } else {
    //         int fd = open(p.constData(), O_WRONLY | O_CREAT | O_TRUNC, f.second.mode);
    //         write(fd, f.second.contents.constData(), f.second.contents.size());
    //         close(fd);
    //     }
    // }
    return true;
}

Hash<Path, Compiler::FileData> Compiler::files() const
{
    Hash<Path, FileData> ret;
    std::function<void(const Path &, const FileData &)> add = [&](const Path &file, const FileData &fileData) {
        Path p;
        if (mBasePath.isEmpty()) {
            p = mBasePath + file;
        } else {
            p = file;
        }
        auto &data = ret[file];
        data.mode = fileData.mode;
        data.flags = fileData.flags;
        if (data.flags & FileData::Link) {
            Path linked;
            data.contents = linked = p.followLink();
            if (!linked.isAbsolute())
                linked.prepend(p.parentDir());
            FileData fd;
            fd.mode = linked.mode();
            if (linked.isSymLink())
                fd.flags |= FileData::Link;
            add(linked, fd);
        } else {
            data.contents = p.readAll();
        }
    };

    for (const auto &f : mFiles) {
        add(f.first, f.second);
    }
    return ret;
}


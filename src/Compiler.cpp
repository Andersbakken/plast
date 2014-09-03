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

static Path::VisitResult visitor(const Path &path, void *userData)
{
    Set<Path> &files = *reinterpret_cast<Set<Path> *>(userData);
    if (path.isSymLink()) {
        if (path.isFile()) {
            Path link = path.followLink();
            link.resolve(Path::RealPath, path.parentDir());
            visitor(link, userData);
        }
    } else if (path.isDir()) {
        return Path::Recurse;
    } else if (path.mode() & 0111) {
        char *buf = 0;
        const int read = path.readAll(buf, 3);
        if (read != 3 || strncmp(buf, "#!/", read))
            files.insert(path);
        delete[] buf;
    } else if (path.contains(".so")) {
        files.insert(path);
    }
    return Path::Continue;
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
#if 0
    Process process;
    List<String> environment;
    if (!path.isEmpty()) {
        environment = sEnviron;
        environment += ("PATH=" + path);
    }
    if (!process.exec(compiler, List<String>() << "-v" << "-E" << "-", environment)) {
        error() << "Failed to package compiler" << compiler << path;
        return std::shared_ptr<Compiler>();
    }

    Set<String> compilerPaths;
    const List<String> lines = process.readAllStdErr().split('\n');
    String version;
    for (int i=0; i<lines.size(); ++i) {
        const String &line = lines.at(i);
        sha.update(line);

        if (compilerPaths.isEmpty() && line.size() > 14 && line.startsWith("COMPILER_PATH=")) {
            compilerPaths = line.mid(14).split(':').toSet();
        } else if (version.isEmpty() && line.size() > 12 && line.startsWith("gcc version")) {
            int space = line.indexOf(' ', 12);
            if (space == -1)
                space = line.size();

            version = line.mid(12, space - 12);
            warning() << "Compiler version" << version;
        }
    }

    if (!compilerPaths.isEmpty()) {
        c.reset(new Compiler);
        for (Set<String>::const_iterator it = compilerPaths.begin(); it != compilerPaths.end(); ++it) {
            const Path p = Path::resolved(*it);
            if (p.isDir()) {
                p.visit(visitor, &c->mFiles);
            }
        }
    }
#else
    Process process;
    if (!process.exec("ldd", List<String>() << resolved)) {
        error() << "Couldn't ldd compiler" << resolved;
        return std::shared_ptr<Compiler>();
    }
    c.reset(new Compiler);
    const List<String> lines = process.readAllStdOut().split('\n');
    for (const String &line : lines) {
        const int idx = line.indexOf("=> /");
        if (idx == -1)
            continue;
        const int end = line.indexOf(' ', idx + 5);
        if (end == -1)
            continue;
        const Path path = Path::resolved(line.mid(idx + 3, end - idx - 3));
        if (path.isFile()) {
            sha.update(path.fileName());
            c->mFiles.insert(path);
        } else {
            error() << "Couldn't resolve path" << line.mid(idx + 3, end - idx - 3);
        }
    }
#endif
    if (c) {
        c->mSha256 = sha.hash(SHA256::Hex);
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

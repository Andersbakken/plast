#include "Compiler.h"

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
    c.reset(new Compiler);
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
        return std::shared_ptr<Compiler>();
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
        sBySha[c->mSha256] = c;
        sByPath[compiler] = c;
        if (!resolvedCompiler)
            resolvedCompiler = c;
        warning() << "Created package" << compiler << c->mFiles << c->mSha256;
#warning should write these files to local cache here
    }
    return c;
}

String Compiler::dump()
{
    String ret;
    for (const auto &compiler : sBySha) {
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


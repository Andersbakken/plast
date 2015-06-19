#include <rct/EventLoop.h>
#include <rct/Log.h>
#include <rct/Process.h>
#include <stdio.h>
#include <CompilerMessage.h>


static inline bool printFileName(const Path &path, const String &prog, Set<Path> &files, List<String> &shaList)
{
    Process process;
    if (!process.exec(path, List<String>() << "-print-file-name=" + prog)) {
        error() << "Couldn't invoke compiler" << path;
        return false;
    }
    const List<String> lines = process.readAllStdOut().split('\n');
    // error() << path << lines;
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

std::shared_ptr<CompilerMessage> createCompilerMessage(const Path &compiler)
{
    const Path resolved = compiler.resolved();
    c.reset(new Compiler);
    Hash<Path, std::pair<String, uint32_t> > contents;

    auto insertFile = [&contents](const Path &path) {
        if (path.isSymLink()) {

        }

    };
    c->mFiles.insert(compiler);
    List<String> shaList;
    const char *progNames[] = { "as", "cc1", "cc1plus", "liblto_plugin.so", "libopcodes-2.24-system.so", "libbfd-2.24-system.so" };
    for (size_t i=0; i < sizeof(progNames) / sizeof(progNames[0]); ++i) {
        if (!printFileName(resolved, progNames[i], c->mFiles, shaList)) {
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
            warning() << "Linking" << file << (root + file.fileName());
            if (symlink(file.constData(), (root + file.fileName()).constData())) {
                error() << "Failed to create symlink" << errno << strerror(errno)
                        << (root + file.fileName()) << file;
                ok = false;
                break;
            }
        }

        if (ok && symlink(c->mPath.fileName(), (root + "COMPILER").constData())) {
            error() << "Failed to create symlink" << errno << strerror(errno) << (root + "COMPILER");
            ok = false;
        }

        if (!ok) {
            c.reset();
            for (const Path &f : root.files(Path::File))
                Path::rm(f);
            rmdir(root.constData());
        }
    }
    return c;
}


int main(int argc, char** argv)
{
    const char *logFile = 0;
    Flags<LogFileFlag> logFlags;
    Path logPath;
    const LogLevel logLevel = LogLevel::Error;
    if (!initLogging(argv[0], LogStderr, logLevel, logPath.constData(), logFlags)) {
        fprintf(stderr, "Can't initialize logging with %d %s %s\n",
                logLevel.toInt(), logFile ? logFile : "", logFlags.toString().constData());
        return 1;
    }

    EventLoop::SharedPtr loop(new EventLoop);
    loop->init(EventLoop::MainEventLoop|EventLoop::EnableSigIntHandler);

    loop->exec();

    return client.exitCode();
}

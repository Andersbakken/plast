#include "CompilerVersion.h"
#include "CompilerArgs.h"
#include <rct/Process.h>
#include <rct/Log.h>
#include <regex>

Hash<Path, CompilerVersion::SharedPtr> CompilerVersion::sVersions;
Map<plast::CompilerKey, CompilerVersion::WeakPtr> CompilerVersion::sVersionsByKey;

CompilerVersion::CompilerVersion(const Path& path, unsigned int flags)
    : mCompiler(plast::Unknown), mPath(path)
{
    Process proc;
    List<String> args = { "-v" };
    if (flags & CompilerArgs::HasDashM32)
        args.append("-m32");
    if (proc.exec(path, args) != Process::Done)
        return;
    String data = proc.readAllStdErr();
    if (data.isEmpty())
        data = proc.readAllStdOut();
    if (data.isEmpty())
        return;
    const auto list = data.split('\n');

    const std::regex verrx("^(Apple LLVM|clang|gcc) version (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
    const std::regex target("^Target: (.*)", std::regex_constants::ECMAScript);
    std::smatch match;
    for (const auto& line : list) {
        if (std::regex_match(line.ref(), match, verrx)) {
            assert(match.size() >= 4);
            const String c = match[1].str();
            if (c == "Apple LLVM") {
                mCompiler = plast::ClangApple;
            } else if (c == "clang") {
                mCompiler = plast::Clang;
            } else if (c == "gcc") {
                mCompiler = plast::GCC;
            } else {
                assert(false);
            }
            mVersion.major = std::stoi(match[2].str());
            mVersion.minor = std::stoi(match[3].str());
            if (match.size() == 5 && !match[4].str().empty()) {
                assert(match[4].str().size() > 1);
                mVersion.patch = std::stoi(match[4].str().substr(1));
            } else {
                mVersion.patch = 0;
            }
            mVersion.str = line;
        } else if (std::regex_match(line.ref(), match, target)) {
            assert(match.size() == 2);
            mTarget = match[1].str();
        }
    }
    if (mTarget.isEmpty())
        mCompiler = plast::Unknown;
}

CompilerVersion::SharedPtr CompilerVersion::version(const Path& path, unsigned int flags)
{
    const Path r = path.resolved();
    auto it = sVersions.find(r);
    if (it == sVersions.end()) {
        CompilerVersion::SharedPtr ver(new CompilerVersion(r, flags));
        if (!ver->isValid()) {
            return CompilerVersion::SharedPtr();
        }
        sVersions[r] = ver;
        sVersionsByKey[{ ver->compiler(), ver->major(), ver->target() }] = ver;
        return ver;
    }
    return it->second;
}

void CompilerVersion::init(const Path& path, unsigned int flags)
{
    version(path, flags);
}

CompilerVersion::SharedPtr CompilerVersion::version(plast::CompilerType compiler, int major, const String& target)
{
    const auto v = sVersionsByKey.find({ compiler, major, target });
    if (v == sVersionsByKey.end())
        return SharedPtr();
    return v->second.lock();
}

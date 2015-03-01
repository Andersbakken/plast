#include "CompilerVersion.h"
#include <rct/Process.h>
#include <rct/Log.h>
#include <regex>

Map<CompilerVersion::PathKey, CompilerVersion::SharedPtr> CompilerVersion::sVersions;
Map<plast::CompilerKey, CompilerVersion::WeakPtr> CompilerVersion::sVersionsByKey;

CompilerVersion::CompilerVersion(const Path& path, unsigned int flags, const String& target)
    : mCompiler(plast::Unknown)
{
    if (!target.isEmpty())
        mTarget = target;

    mKey = { path, flags };
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

    const std::regex verrx("^(clang|gcc) version (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
    const std::regex aaplrx("^(Apple LLVM).*based on LLVM (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
    const std::regex targetrx("^Target: (.*)", std::regex_constants::ECMAScript);
    std::smatch match;
    for (const auto& line : list) {
        auto parse = [this](const String& line, const std::smatch& match) {
            assert(match.size() >= 4);
            const String c = match[1].str();
            if (c == "Apple LLVM" || c == "clang") {
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
        };
        if (std::regex_match(line.ref(), match, verrx)) {
            parse(line, match);
        } else if (std::regex_match(line.ref(), match, aaplrx)) {
            parse(line, match);
        } else if (mTarget.isEmpty() && std::regex_match(line.ref(), match, targetrx)) {
            assert(match.size() == 2);
            mTarget = match[1].str();
        }
    }
    if (mTarget.isEmpty())
        mCompiler = plast::Unknown;
}

CompilerVersion::SharedPtr CompilerVersion::version(const Path& path, unsigned int flags, const String& target)
{
    const PathKey k = { path.resolved(), flags & FlagMask };
    auto it = sVersions.find(k);
    if (it == sVersions.end()) {
        CompilerVersion::SharedPtr ver(new CompilerVersion(k.path, k.flags, target));
        if (!ver->isValid()) {
            return CompilerVersion::SharedPtr();
        }
        sVersions[k] = ver;
        sVersionsByKey[{ ver->compiler(), ver->major(), ver->target() }] = ver;
        error() << "registered compiler" << path << ver->compiler() << ver->major() << ver->target();
        return ver;
    }
    return it->second;
}

void CompilerVersion::init(const Path& path, unsigned int flags, const String& target)
{
    version(path, flags, target);
}

CompilerVersion::SharedPtr CompilerVersion::version(plast::CompilerType compiler, int major, const String& target)
{
    const auto v = sVersionsByKey.find({ compiler, major, target });
    if (v == sVersionsByKey.end())
        return SharedPtr();
    return v->second.lock();
}

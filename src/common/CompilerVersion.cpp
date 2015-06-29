#include "CompilerVersion.h"
#include <rct/Process.h>
#include <rct/Log.h>
#include <regex>

// Map<Path, List<CompilerVersion::WeakPtr> > CompilerVersion::sByPath;
// Map<CompilerVersion::Key, CompilerVersion::SharedPtr> CompilerVersion::sByKey;

CompilerVersion::CompilerVersion()
    : mMajorVersion(0), mMinorVersion(0), mPatchVersion(0),
      mBits(Bits_None), mType(Unknown)
{
}

void CompilerVersion::create(const Path &path, const List<String> &targets)
{
#if 0
    if (sByPath.contains(path))
        return;
    List<CompilerVersion::WeakPtr> &weakList = sByPath[path];
    const std::regex verrx("^(clang|gcc) version (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
    const std::regex aaplrx("^(Apple LLVM).*based on LLVM (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
    const std::regex targetrx("^Target: (.*)", std::regex_constants::ECMAScript);
    const std::regex confrx("^Configured with.+--with-multilib-list=([^ $]+).*$", std::regex_constants::ECMAScript);
    std::smatch match;

    auto updateBits = [](const CompilerVersion::SharedPtr &compiler) {
        if (compiler->mKey.target.contains("x86_64")) {
            compiler->mKey.bits = Bits_64;
        } else {
            compiler->mKey.bits = Bits_32;
        }
    };

    auto create = [&](const String &arg = String()) {
        List<String> args;
        args << "-v";
        if (arg.isEmpty())
            args << arg;
        Process proc;
        if (proc.exec(path, args) != Process::Done) {
            error() << "Couldn't exec" << path << args;
            return CompilerVersion::SharedPtr();
        }
        String data = proc.readAllStdErr();
        if (data.isEmpty())
            data = proc.readAllStdOut();
        if (data.isEmpty()) {
            error() << "No output exec" << path << args;
            return CompilerVersion::SharedPtr();
        }

        const auto list = data.split('\n');

        Key key;
        String versionString;
        Set<String> multiLibs;
        List<String> extraArgs;
        for (const auto &line : list) {
            auto parse = [&](const String& line, const std::smatch& match) {
                assert(match.size() >= 4);
                const String c = match[1].str();
                if (c == "Apple LLVM" || c == "clang") {
                    key.type = Clang;
                } else if (c == "gcc") {
                    key.type = GCC;
                } else {
                    error() << "Can't parse this type" << c;
                    return CompilerVersion::SharedPtr();
                }
                key.majorVersion = std::stoi(match[2].str());
                key.minorVersion = std::stoi(match[3].str());
                if (match.size() == 5 && !match[4].str().empty()) {
                    assert(match[4].str().size() > 1);
                    key.patchVersion = std::stoi(match[4].str().substr(1));
                } else {
                    key.patchVersion = 0;
                }
                versionString = line;
            };

            if (std::regex_match(line.ref(), match, verrx)) {
                parse(line, match);
            } else if (std::regex_match(line.ref(), match, aaplrx)) {
                parse(line, match);
            } else if (std::regex_match(line.ref(), match, targetrx)) {
                assert(match.size() == 2);
                key.target = match[1].str();
            } else if (std::regex_match(line.ref(), match, confrx)) {
                assert(match.size() == 2);
                assert(multiLibs.isEmpty());
                multiLibs << String(match[1].str()).split(',');
            }
        }
        const auto ret = CompilerVersion::SharedPtr(new CompilerVersion(key, versionString, path, multiLibs, extraArgs));
        updateBits(ret);
        return ret;
    };

    auto compiler = create();
    if (!compiler) {
        error() << "Can't parse compiler output" << path;
        return;
    }

    auto add = [&](const String &target = String()) {
        if (!target.isEmpty()) {
            compiler->mKey.target = target;
            updateBits(compiler);
        }
        sByKey[compiler->mKey] = compiler;
        weakList.append(compiler);
    };

    if (!targets.isEmpty()) {
        for (int i=0; i<targets.size(); ++i) {
            if (i)
                compiler = compiler->clone();
            add(targets.at(i));
        }
    } else if (compiler->mMultiLibs.isEmpty()) {
        add();
        if (compiler->mKey.type == Clang) {
            compiler = create("-m32");
            if (compiler)
                add();
        }
    } else {
        if (compiler->mMultiLibs.contains("m32")) {
            compiler->mExtraArgs << "-m32";
            add("i686-linux-gnu");
        }

        if (compiler->mMultiLibs.contains("m64")) {
            compiler = compiler->clone();
            compiler->mExtraArgs << "-m64";
            add("x86_64-linux-gnu");
        }
    }
#endif
}

void CompilerVersion::loadDB(const Path &path)
{
#warning need to do
}

void CompilerVersion::saveDB(const Path &path)
{
#warning need to do
}

CompilerVersion CompilerVersion::create(const std::shared_ptr<CompilerArgs> &args)
{
//     const Path resolved = plast::resolveCompiler(args->commandLine.first());
//     auto it = sByPath.find(resolved);
//     if (it != sByPath.end()) {
//         while (it != sByPath.end()) {
//             for (auto weak : it->second) {
//                 auto compiler = weak.lock();
// #warning match against args
//                 assert(compiler);
//             }
//         }
//     }

    // if (!com
    //     for (int i=0; i<10; ++i) {
    //     }

}

#if 0
CompilerVersion::SharedPtr CompilerVersion::find(const Key &key)
{
    return sByKey.value(key);
}

CompilerVersion::SharedPtr CompilerVersion::create(const Key &key, const String &versionString, const Set<String> &multiLibs,
                                                   const List<String> &extraArgs, const Path &path)
{
    auto &ref = sByKey[key];
    if (!ref) {
        ref.reset(new CompilerVersion(key, versionString, path, multiLibs, extraArgs));
        // ### should not add to path
    }
    return ref;
}
#endif

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

CompilerCache::CompilerCache(const Path &path)
    : mPath(path)
{
}

void CompilerCache::load()
{
    mPath.visit([this](const Path &path) {
            if (path.isDir()) {
                assert(path.endsWith('/'));
                const String manifest = Path(path + ".manifest").readAll();
                if (manifest.isEmpty()) {
                    error() << "Can't load compiler" << path << "no manifest";
                    return Path::Continue;
                }
                Deserializer deserializer(manifest);
                List<CompilerVersion> versions;
                Path executable;
                deserializer >> executable >> versions;
                if (versions.isEmpty() || !executable.isExecutable()) {
                    error() << "Can't load compiler" << path << "versions";
                    return Path::Continue;
                }
                Hash<Path, Compiler::FileData> files;
                path.visit([&files, &executable](const Path &file) {
                        if (file.isDir()) {
                            return Path::Recurse;
                        }

                        auto &f = files[file];
                        f.mode = file.mode();
                        if (file.isSymLink()) {
                            f.flags |= Compiler::FileData::Link;
                        }
                        if (file == executable) {
                            f.flags |= Compiler::FileData::Compiler;
                        }
                        return Path::Continue;
                    });

                std::shared_ptr<Compiler> compiler(new Compiler(path, files));
                for (const auto &version : versions) {
                    mCompilers[version] = compiler;
                }
                mByPath[executable] = versions;
            }
            return Path::Continue;
        });
}

void CompilerCache::clear()
{
    mCompilers.clear();
    Rct::removeDirectory(mPath);
}

// void CompilerVersion::create(const Path &path, const List<String> &targets)
// {
//     if (sByPath.contains(path))
//         return;
//     List<CompilerVersion::WeakPtr> &weakList = sByPath[path];
//     const std::regex verrx("^(clang|gcc) version (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
//     const std::regex aaplrx("^(Apple LLVM).*based on LLVM (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
//     const std::regex targetrx("^Target: (.*)", std::regex_constants::ECMAScript);
//     const std::regex confrx("^Configured with.+--with-multilib-list=([^ $]+).*$", std::regex_constants::ECMAScript);
//     std::smatch match;

//     auto updateBits = [](const CompilerVersion::SharedPtr &compiler) {
//         if (compiler->mKey.target.contains("x86_64")) {
//             compiler->mKey.bits = Bits_64;
//         } else {
//             compiler->mKey.bits = Bits_32;
//         }
//     };

//     auto create = [&](const String &arg = String()) {
//         List<String> args;
//         args << "-v";
//         if (arg.isEmpty())
//             args << arg;
//         Process proc;
//         if (proc.exec(path, args) != Process::Done) {
//             error() << "Couldn't exec" << path << args;
//             return CompilerVersion::SharedPtr();
//         }
//         String data = proc.readAllStdErr();
//         if (data.isEmpty())
//             data = proc.readAllStdOut();
//         if (data.isEmpty()) {
//             error() << "No output exec" << path << args;
//             return CompilerVersion::SharedPtr();
//         }

//         const auto list = data.split('\n');

//         Key key;
//         String versionString;
//         Set<String> multiLibs;
//         List<String> extraArgs;
//         for (const auto &line : list) {
//             auto parse = [&](const String& line, const std::smatch& match) {
//                 assert(match.size() >= 4);
//                 const String c = match[1].str();
//                 if (c == "Apple LLVM" || c == "clang") {
//                     key.type = Clang;
//                 } else if (c == "gcc") {
//                     key.type = GCC;
//                 } else {
//                     error() << "Can't parse this type" << c;
//                     return CompilerVersion::SharedPtr();
//                 }
//                 key.majorVersion = std::stoi(match[2].str());
//                 key.minorVersion = std::stoi(match[3].str());
//                 if (match.size() == 5 && !match[4].str().empty()) {
//                     assert(match[4].str().size() > 1);
//                     key.patchVersion = std::stoi(match[4].str().substr(1));
//                 } else {
//                     key.patchVersion = 0;
//                 }
//                 versionString = line;
//             };

//             if (std::regex_match(line.ref(), match, verrx)) {
//                 parse(line, match);
//             } else if (std::regex_match(line.ref(), match, aaplrx)) {
//                 parse(line, match);
//             } else if (std::regex_match(line.ref(), match, targetrx)) {
//                 assert(match.size() == 2);
//                 key.target = match[1].str();
//             } else if (std::regex_match(line.ref(), match, confrx)) {
//                 assert(match.size() == 2);
//                 assert(multiLibs.isEmpty());
//                 multiLibs << String(match[1].str()).split(',');
//             }
//         }
//         const auto ret = CompilerVersion::SharedPtr(new CompilerVersion(key, versionString, path, multiLibs, extraArgs));
//         updateBits(ret);
//         return ret;
//     };

//     auto compiler = create();
//     if (!compiler) {
//         error() << "Can't parse compiler output" << path;
//         return;
//     }

//     auto add = [&](const String &target = String()) {
//         if (!target.isEmpty()) {
//             compiler->mKey.target = target;
//             updateBits(compiler);
//         }
//         sByKey[compiler->mKey] = compiler;
//         weakList.append(compiler);
//     };

//     if (!targets.isEmpty()) {
//         for (int i=0; i<targets.size(); ++i) {
//             if (i)
//                 compiler = compiler->clone();
//             add(targets.at(i));
//         }
//     } else if (compiler->mMultiLibs.isEmpty()) {
//         add();
//         if (compiler->mKey.type == Clang) {
//             compiler = create("-m32");
//             if (compiler)
//                 add();
//         }
//     } else {
//         if (compiler->mMultiLibs.contains("m32")) {
//             compiler->mExtraArgs << "-m32";
//             add("i686-linux-gnu");
//         }

//         if (compiler->mMultiLibs.contains("m64")) {
//             compiler = compiler->clone();
//             compiler->mExtraArgs << "-m64";
//             add("x86_64-linux-gnu");
//         }
//     }
// #endif
// }

static List<String> invokeCompiler(const Path &path, const List<String> &args)
{
    List<String> ret;
    Process proc;
    if (proc.exec(path, args) != Process::Done) {
        error() << "Couldn't exec" << path << args;
        return ret;
    }
    ret = proc.readAllStdErr().split('\n');
    ret += proc.readAllStdOut().split('\n');
    return ret;
}

CompilerInvocation CompilerCache::createInvocation(const std::shared_ptr<CompilerArgs> &compilerArgs)
{
    const Path resolved = plast::resolveCompiler(compilerArgs->commandLine.first());
    auto it = mByPath.find(resolved);
    if (it == mByPath.end()) {
        List<CompilerVersion> &versions = mByPath[resolved];
        const std::regex verrx("^(clang|gcc) version (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
        const std::regex aaplrx("^(Apple LLVM).*based on LLVM (\\d+)\\.(\\d+)(\\.\\d+)?.*", std::regex_constants::ECMAScript);
        const std::regex targetrx("^Target: (.*)", std::regex_constants::ECMAScript);
        const std::regex confrx("^Configured with.+--with-multilib-list=([^ $]+).*$", std::regex_constants::ECMAScript);
        std::smatch match;

        versions.resize(1);
        CompilerVersion &version = versions.front();

        for (const auto &line : invokeCompiler(resolved, List<String>() << "-v" << "-m32")) {
            auto parse = [&](const String &line, const std::smatch& match) {
                assert(match.size() >= 4);
                const String c = match[1].str();
                if (c == "Apple LLVM" || c == "clang") {
                    version.mType = CompilerVersion::Clang;
                } else if (c == "gcc") {
                    version.mType = CompilerVersion::GCC;
                } else {
                    error() << "Can't parse this type" << c;
                    return false;
                }
                version.mMajorVersion = std::stoi(match[2].str());
                version.mMinorVersion = std::stoi(match[3].str());
                if (match.size() == 5 && !match[4].str().empty()) {
                    assert(match[4].str().size() > 1);
                    version.mPatchVersion = std::stoi(match[4].str().substr(1));
                } else {
                    version.mPatchVersion = 0;
                }
                version.mVersionString = line;
                return true;
            };

            if (std::regex_match(line.ref(), match, verrx)) {
                if (!parse(line, match))
                    return CompilerInvocation();
            } else if (std::regex_match(line.ref(), match, aaplrx)) {
                if (!parse(line, match))
                    return CompilerInvocation();
            } else if (std::regex_match(line.ref(), match, targetrx)) {
                assert(match.size() == 2);
                version.mTarget = match[1].str();
            } else if (std::regex_match(line.ref(), match, confrx)) {
                assert(match.size() == 2);
                assert(version.mMultiLibs.isEmpty());
                version.mMultiLibs << String(match[1].str()).split(',');
            }
        }

        auto add = [&versions](const String &target, const String &extra = String()) {
            assert(!versions.isEmpty());
            versions.resize(versions.size() + 1);
            CompilerVersion &v = versions.back();
            v = versions.front();
            v.mTarget = target;
            if (!extra.isEmpty())
                v.mExtraArgs << extra;
        };

        switch (version.type()) {
        case CompilerVersion::Clang:
            for (const auto &line : invokeCompiler(resolved, (List<String>() << "-v" << "-m64"))) {
                if (std::regex_match(line.ref(), match, targetrx)) {
                    assert(match.size() == 2);
                    if (match[1].str() != version.mTarget.ref()) { // What if it's only 64-bit?
                        add(match[1].str(), "-m64");
                        version.mExtraArgs << "-m32";
                    }
                    break;
                }
            }
            break;
        case CompilerVersion::GCC:
            if (version.mTarget == "x86_64-linux-gnu" && version.mMultiLibs.contains("m32")) {
                add("i686-linux-gnu", "-m32");
            } else if (version.mTarget == "i686-linux-gnu" && version.mMultiLibs.contains("m64")) {
                add("x86_64-linux-gnu", "-m64");
            } else if (!version.mMultiLibs.isEmpty()) {
                error() << "Unhandled multilibs" << resolved << version.mMultiLibs << version.mTarget;
            }
            break;
        default:
            return CompilerInvocation();
        }
        std::shared_ptr<Compiler> compiler(new Compiler(resolved));
    }
    for (const auto &version : it->second) {
        const int bits = version.bits();
        if (bits == 32) {

        } else if (bits == 64) {


        }

    }
    return CompilerInvocation();
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


// CompilerInvocation CompilerCache::createInvocation(const std::shared_ptr<CompilerArgs> &args)
// {
// }

bool CompilerCache::contains(const CompilerVersion &args)
{
    return mCompilers.contains(args);
}


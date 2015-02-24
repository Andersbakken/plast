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

#include "CompilerArgs.h"

static const char *argOptions[] = {
    "-D",
    "-I",
    "-MQ",
    "-Xpreprocessor",
    "-aux-info",
    "-idirafter",
    "-imacros",
    "-imultilib",
    "-include",
    "-iprefix",
    "-isysroot",
    "-iwithprefix",
    "-iwithprefixbefore",
    "-wrapper"
};

static int compare(const void *s1, const void *s2) {
    const char *key = reinterpret_cast<const char*>(s1);
    const char * const *arg = reinterpret_cast<const char * const *>(s2);
    return strcmp(key, *arg);
}

static inline bool hasArg(const String &arg)
{
    return bsearch(arg.constData(), argOptions, sizeof(argOptions) / sizeof(argOptions[0]),
                   sizeof(const char*), ::compare);
}

std::shared_ptr<CompilerArgs> CompilerArgs::create(const List<String> &args)
{
    std::shared_ptr<CompilerArgs> ret(new CompilerArgs);
    ret->commandLine = args;
    ret->mode = Link;
    ret->flags = None;
    ret->objectFileIndex = -1;
    for (int i=1; i<args.size(); ++i) {
        const String &arg = args[i];
        if (arg == "-c") {
            if (ret->mode == Link)
                ret->mode = Compile;
        } else if (arg == "-S") {
            ret->flags |= NoAssemble;
        } else if (arg == "-E") {
            ret->mode = Preprocess;
        } else if (arg == "-o") {
            ret->flags |= HasDashO;
            ret->objectFileIndex = ++i;
        } else if (arg == "-MF") {
            ret->flags |= HasDashMF;
            ++i;
        } else if (arg == "-MMD") {
            ret->flags |= HasDashMMD;
        } else if (arg == "-MT") {
            ret->flags |= HasDashMT;
            ++i;
        } else if (arg == "-x") {
            ret->flags |= HasDashX;
            if (++i == args.size())
                return std::shared_ptr<CompilerArgs>();
            const String lang = args.value(i);
            const CompilerArgs::Flag languages[] = { CPlusPlus, C, CPreprocessed, CPlusPlusPreprocessed, ObjectiveC, AssemblerWithCpp, Assembler };
            for (size_t j=0; j<sizeof(languages) / sizeof(languages[0]); ++j) {
                if (lang == CompilerArgs::languageName(languages[j])) {
                    ret->flags &= ~LanguageMask;
                    ret->flags |= languages[j];
                    // -x takes precedence
                    break;
                }
            }
        } else if (hasArg(arg)) {
            ++i;
        } else if (!arg.startsWith("-")) {
            ret->sourceFileIndexes.append(i);
            if (!(ret->flags & LanguageMask)) {
                const int lastDot = arg.lastIndexOf('.');
                if (lastDot != -1) {
                    const char *ext = arg.constData() + lastDot + 1;
                    struct {
                        const char *suffix;
                        const Flag flag;
                    } static const suffixes[] = {
                        { "C", CPlusPlus },
                        { "cc", CPlusPlus },
                        { "cxx", CPlusPlus },
                        { "cpp", CPlusPlus },
                        { "c", C },
                        { "i", CPreprocessed },
                        { "ii", CPlusPlusPreprocessed },
                        { "m", ObjectiveC },
                        { "mm", ObjectiveCPlusPlus },
                        { "S", Assembler },
                        { "s", AssemblerWithCpp },
                        { 0, None }
                    };
                    for (int i=0; suffixes[i].suffix; ++i) {
                        if (!strcmp(ext, suffixes[i].suffix)) {
                            ret->flags |= suffixes[i].flag;
                            break;
                        }
                    }
                }
            }
        } else if (arg == "-") {
            ret->flags |= StdinInput;
        }
    }
    return ret;
}

const char *CompilerArgs::languageName(Flag flag)
{
    switch (flag) {
    case CPlusPlus: return "c++";
    case C: return "c";
    case CPreprocessed: return "cpp-output";
    case CPlusPlusPreprocessed: return "c++-cpp-output";
    case ObjectiveC: return "objective-c"; // ### what about ObjectiveC++?
    case AssemblerWithCpp: return "assembler-with-cpp";
    case Assembler: return "assembler";
    default: break;
    }
    return "";
}

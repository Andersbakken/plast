#ifndef CompilerArgs_h
#define CompilerArgs_h

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

#include <rct/List.h>
#include <rct/String.h>
#include <rct/Path.h>
#include <rct/Serializer.h>
#include <memory>

struct CompilerArgs
{
    List<String> commandLine;
    List<int> sourceFileIndexes;
    int objectFileIndex;
    List<Path> sourceFiles() const
    {
        List<Path> ret;
        ret.reserve(sourceFileIndexes.size());
        for (int idx : sourceFileIndexes) {
            ret.append(commandLine.at(idx));
        }
        return ret;
    }

    enum Mode {
        Compile,
        Preprocess,
        Link
    } mode;

    const char *modeName() const
    {
        switch (mode) {
        case Compile: return "compile";
        case Preprocess: return "preprocess";
        case Link: return "link";
        }
        return "";
    }

    enum Flag {
        None = 0x00000,
        NoAssemble = 0x00001,
        MultiSource = 0x00002,
        HasDashO = 0x00004,
        HasDashX = 0x00008,
        HasDashMF = 0x00010,
        HasDashMMD = 0x00020,
        HasDashMT = 0x00040,
        StdinInput = 0x00080,
        // Languages
        CPlusPlus = 0x01000,
        C = 0x02000,
        CPreprocessed = 0x04000,
        CPlusPlusPreprocessed = 0x08000,
        ObjectiveC = 0x10000,
        ObjectiveCPlusPlus = 0x20000,
        AssemblerWithCpp = 0x40000,
        Assembler = 0x80000,
        LanguageMask = CPlusPlus|C|CPreprocessed|CPlusPlusPreprocessed|ObjectiveC|AssemblerWithCpp|Assembler
    };
    static const char *languageName(Flag flag);
    unsigned int flags;

    static std::shared_ptr<CompilerArgs> create(const List<String> &args);

    Path sourceFile(int idx = 0) const { return commandLine.value(sourceFileIndexes.value(idx, -1)); }
    Path output() const
    {
        if (flags & HasDashO) {
            assert(objectFileIndex);
            return commandLine.value(objectFileIndex);
        } else {
            Path source = sourceFile();
            const int lastDot = source.lastIndexOf('.');
            if (lastDot != -1 && lastDot > source.lastIndexOf('/')) {
                source.chop(source.size() - lastDot - 1);
            }
            source.append('o');
            return source;
        }
    }
};

inline Serializer &operator<<(Serializer &serializer, const CompilerArgs &args)
{
    serializer << args.commandLine << args.sourceFileIndexes << args.objectFileIndex
               << static_cast<uint8_t>(args.mode) << static_cast<uint32_t>(args.flags);
    return serializer;
}

inline Deserializer &operator>>(Deserializer &deserializer, CompilerArgs &args)
{
    uint8_t mode;
    deserializer >> args.commandLine >> args.sourceFileIndexes >> args.objectFileIndex >> mode >> args.flags;
    args.mode = static_cast<CompilerArgs::Mode>(mode);
    return deserializer;
}

#endif

#include "Plast.h"

namespace plast {

Path defaultSocketFile()
{
    return Path::home() + ".plastd.sock";
}

Path resolveCompiler(const Path &path)
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

} // namespace plast

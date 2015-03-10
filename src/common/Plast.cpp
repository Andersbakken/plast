#include "Plast.h"

namespace plast {

Path defaultSocketFile()
{
    return Path::home() + ".plastd.sock";
}

Path resolveCompiler(const Path &path)
{
    if (path.isAbsolute()) {
        const Path resolved = path.resolved();
        const char *fn = resolved.fileName();
        if (strcmp(fn, "plastc") && strcmp(fn, "gcc-rtags-wrapper.sh") && strcmp(fn, "icecc")) {
            return path;
        }
    }

    const List<String> paths = String(getenv("PATH")).split(':');
    const String fileName = path.fileName();
    // error() << fileName;
    for (const auto &p : paths) {
        const Path orig = p + "/" + fileName;
        Path exec = orig;
        // error() << "Trying" << exec;
        if (exec.resolve()) {
            const char *fn = exec.fileName();
            if (strcmp(fn, "plastc") && strcmp(fn, "gcc-rtags-wrapper.sh") && strcmp(fn, "icecc")) {
                return orig;
            }
        }
    }
    return Path();
}

} // namespace plast

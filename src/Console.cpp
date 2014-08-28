#include "Console.h"
#include <locale.h>
#include <unistd.h>
#include <readline/readline.h>
#include <rct/EventLoop.h>

std::function<void(const String&)> Console::sCommandHandler;
std::function<void(const String&, int, int, String&, List<String>&)> Console::sCompletionHandler;

void Console::handleReadLine(char* line)
{
    if (sCommandHandler)
        sCommandHandler(line);
}

char** Console::attemptShellCompletion(const char* text, int start, int end)
{
    if (sCompletionHandler) {
        String common;
        List<String> candidates;
        sCompletionHandler(text, start, end, common, candidates);
        char** arr = static_cast<char**>(malloc((candidates.size() + 2) * sizeof(char*)));
        arr[0] = strdup(common.constData());
        arr[candidates.size() + 1] = 0;
        for (int idx = 0; idx < candidates.size(); ++idx) {
            arr[idx + 1] = strdup(candidates[idx].constData());
        }
        return arr;
    }
    return 0;
}

Console::Console()
{
}

void Console::cleanup()
{
    rl_callback_handler_remove();
}

void Console::init(const String& prompt,
                   std::function<void(const String&)>&& handleCommand,
                   std::function<void(const String&, int, int, String&, List<String>&)>&& handleCompletions)
{
    sCommandHandler = std::move(handleCommand);
    sCompletionHandler = std::move(handleCompletions);

    setlocale(LC_ALL, "");

    rl_readline_name = strdup("console");
    rl_attempted_completion_function = attemptShellCompletion;
    rl_completer_quote_characters = "'\"";

    rl_callback_handler_install(prompt.constData(), handleReadLine);

    EventLoop::eventLoop()->registerSocket(STDIN_FILENO, EventLoop::SocketRead|EventLoop::SocketLevelTriggered, [](int, unsigned int) {
            rl_callback_read_char();
        });
}

static int common(const String &l, const String &r)
{
    int common = 0;
    const int ll = l.size();
    const int rl = r.size();
    const int minLength = std::min(ll, rl);

    while (common < minLength && l.at(common) == r.at(common)) {
        ++common;
    }
    return common;
}

Console::TryCompleteResults Console::tryComplete(const String &text, const List<String> &candidates)
{
    int best = -1;
    TryCompleteResults ret;
    if (text.isEmpty()) {
        ret.candidates = candidates;
        for (int i=1; i<candidates.size(); ++i) {
            const int c = common(candidates.first(), candidates.at(i));
            if (best == -1) {
                best = c;
            } else {
                best = std::min(c, best);
            }
        }
        if (best > 0)
            ret.text = candidates.first().left(best);
    } else {
        for (const String &candidate : candidates) {
            const int c = common(text, candidate);
            if (c > 0) {
                ret.candidates.append(candidate);
                if (best == -1) {
                    best = c;
                } else {
                    best = std::min(best, c);
                }
            }
        }
        if (ret.candidates.size() == 1) {
            ret.text = ret.candidates.first();
            ret.candidates.clear();
        } else {
            ret.text = text.left(best);
        }
    }
    return ret;
}

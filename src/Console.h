#ifndef CONSOLE_H
#define CONSOLE_H

#include <rct/String.h>
#include <rct/List.h>

class Console
{
public:
    static void init(const String& prompt,
                     std::function<void(const String&)>&& handleCommand,
                     std::function<void(const String&, int, int, String&, List<String>&)>&& handleCompletions);
    static void cleanup();

    static String tryComplete(const String &text, const List<String> &candidates);
private:
    Console();
    Console(const Console& other) = delete;
    Console& operator=(const Console& other) = delete;

    static void handleReadLine(char* line);
    static char** attemptShellCompletion(const char* text, int start, int end);

private:
    static std::function<void(const String&)> sCommandHandler;
    static std::function<void(const String&, int, int, String&, List<String>&)> sCompletionHandler;
};

#endif

#pragma once
// #1258: the one place this program decides whether a person is at the keyboard, and the
// one way it asks them something.
//
// WHY ONE FUNCTION. Two slices ask a question at start: #1258 asks which three cameras,
// and #1259 asks for the pairing code. If each decided "is this interactive" in its own
// words, a board started one way could be asked one question and not the other, and a
// service could be made to wait on the second. So the rule lives here, once:
//
//   ASK only when the program was started in an interactive console.
//   NEVER when it was started without a console (a shortcut with no window, a scheduled
//   task, a service), and never when its input is redirected (`< NUL`, `< file`, a pipe).
//
// What decides it is the INPUT handle, not the output one. A person who redirects the
// log to a file and types at the console is still at the keyboard; a board whose input is
// NUL is not, whatever its output is. On Windows `_isatty` is the wrong test and was
// deliberately not used: NUL is a character device, so `_isatty(_fileno(stdin))` answers
// true for `< NUL` and a board started that way would wait forever. GetConsoleMode only
// succeeds on a real console input buffer, which is exactly the rule above.
//
// WHY TWO LINES. Every prompt and every message is said in Finnish and then in English,
// one line each (#1258's acceptance criteria), so a Text carries both and nothing that
// asks can forget either.

#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
// windows.h arrives through od_platform_first.hpp, force-included on MSVC.
#include "od_platform_first.hpp"
#else
#include <unistd.h>
#endif

namespace console_prompt
{
    /** One thing said to the person: the Finnish line, then the English line. */
    struct Text
    {
        std::string fi;
        std::string en;
    };

    /**
     * True when the program was started in an interactive console, by the rule at the top
     * of this file. #1259 calls this, and nothing else, before asking for a code.
     */
    inline bool isInteractiveConsole()
    {
#ifdef _WIN32
        HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
        if (in == NULL || in == INVALID_HANDLE_VALUE)
        {
            return false; // no console at all: a service, a detached or GUI start
        }
        if (::GetFileType(in) != FILE_TYPE_CHAR)
        {
            return false; // a file or a pipe
        }
        DWORD mode = 0;
        return ::GetConsoleMode(in, &mode) != 0; // NUL is FILE_TYPE_CHAR and fails here
#else
        return ::isatty(STDIN_FILENO) == 1;
#endif
    }

    /** Where a question is said and its answer read. The standard streams, or a script. */
    class Console
    {
    public:
        virtual ~Console() = default;
        virtual void say(const Text &text) = 0;
        /** A line that is the same in both languages: a name, a number. */
        virtual void sayVerbatim(const std::string &line) = 0;
        /** One line of input without its line ending. False when input has ended. */
        virtual bool readLine(std::string &line) = 0;
    };

    /** The console the program was started in. */
    class StdConsole : public Console
    {
    public:
        void say(const Text &text) override
        {
            beforeSaying();
            std::cout << text.fi << "\n" << text.en << std::endl;
        }

        void sayVerbatim(const std::string &line) override
        {
            beforeSaying();
            std::cout << line << std::endl;
        }

        bool readLine(std::string &line) override
        {
            if (!std::getline(std::cin, line))
            {
                return false;
            }
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            return true;
        }

    private:
        /**
         * The Finnish lines carry ä and ö and the sources are UTF-8 (/utf-8); a console left
         * on its OEM code page prints each as two characters. Set on the first line actually
         * said, not on construction: the code page belongs to the console the board was
         * started from and outlives this process, so a start that says nothing -- input
         * redirected, or all three cameras open -- must leave it as it found it. Output only:
         * the answers asked for are digits, and UTF-8 console INPUT is unreliable on Windows 10.
         */
        void beforeSaying()
        {
            if (code_page_set_)
            {
                return;
            }
            code_page_set_ = true;
#ifdef _WIN32
            ::SetConsoleOutputCP(CP_UTF8);
#endif
        }

        bool code_page_set_ = false;
    };

    /** What a judge says about one answer: accepted, or refused with the reason to say. */
    struct Verdict
    {
        bool accepted = false;
        Text refusal;

        static Verdict accept() { return Verdict{true, Text{}}; }
        static Verdict refuse(const Text &why) { return Verdict{false, why}; }
    };

    /**
     * Say the question, read a line, and hand it to the judge; a refused answer is said
     * plainly and the question asked again. Returns true with the accepted line in
     * `answer`, or false when input ended before an answer was accepted -- the caller
     * then behaves as if nothing had been asked, and never loops on a closed input.
     */
    inline bool askUntil(Console &console, const Text &question,
                         const std::function<Verdict(const std::string &)> &judge, std::string &answer)
    {
        for (;;)
        {
            console.say(question);
            std::string line;
            if (!console.readLine(line))
            {
                return false;
            }
            Verdict verdict = judge(line);
            if (verdict.accepted)
            {
                answer = line;
                return true;
            }
            console.say(verdict.refusal);
        }
    }
}

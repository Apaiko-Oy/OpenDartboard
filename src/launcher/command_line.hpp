#pragma once
// #1303: the arguments an install carries, handed on unchanged.
//
// WHY THERE IS ANY CODE HERE AT ALL. On POSIX a child is given an argv array and nothing
// can be lost. On Windows there is no such thing: CreateProcess takes ONE string, and the
// child's own startup code splits it again with the MSVCRT rules. So every Windows
// launcher re-quotes, and a launcher that re-quotes badly is a launcher that silently
// changes what the install was configured to do -- `--credentials "C:\Program
// Files\od\credentials.json"` arriving as two arguments, or a trailing backslash eating
// the closing quote and swallowing the argument after it.
//
// The rules implemented are the ones CommandLineToArgvW undoes, and they are the awkward
// pair rather than the obvious one: a run of backslashes is doubled ONLY when a quote
// follows it, and a quote is escaped by a backslash. `C:\od\` inside quotes is
// `"C:\od\\"`, because `"C:\od\"` ends nowhere.
//
// It is pure string work, so testers/i1303_check.sh runs it on Linux; and the Windows
// harness measures the same thing end to end, by having the stub print the argv it really
// received and comparing it byte for byte with what was passed in.

#include <string>
#include <vector>

namespace launcher
{
    /** True when this argument can be written into a command line as it stands. */
    inline bool needsQuoting(const std::string &argument)
    {
        return argument.empty() || argument.find_first_of(" \t\n\v\"") != std::string::npos;
    }

    /** One argument, in the spelling MSVCRT's splitter turns back into the same bytes. */
    inline std::string quoteArgument(const std::string &argument)
    {
        if (!needsQuoting(argument))
        {
            return argument;
        }
        std::string quoted = "\"";
        for (size_t i = 0;; i++)
        {
            size_t backslashes = 0;
            while (i < argument.size() && argument[i] == '\\')
            {
                i++;
                backslashes++;
            }
            if (i == argument.size())
            {
                // At the end the run is doubled, so the closing quote is not escaped by it.
                quoted.append(backslashes * 2, '\\');
                break;
            }
            if (argument[i] == '"')
            {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back('"');
            }
            else
            {
                quoted.append(backslashes, '\\');
                quoted.push_back(argument[i]);
            }
        }
        quoted.push_back('"');
        return quoted;
    }

    /**
     * The whole command line: the program, then every argument in order. argv[0] is the
     * program's own path, which is what the child reads back as argv[0], so the detector's
     * own `--version` line and its log say the file that is really running.
     */
    inline std::string buildCommandLine(const std::string &program, const std::vector<std::string> &arguments)
    {
        std::string line = quoteArgument(program);
        for (size_t i = 0; i < arguments.size(); i++)
        {
            line.push_back(' ');
            line += quoteArgument(arguments[i]);
        }
        return line;
    }

    // ------------------------------------------------------------------ where it lives

    /**
     * The directory a path is in, with no trailing separator; empty when the path names no
     * directory at all. Both separators, because a Windows path may carry either and a
     * shortcut's target often carries both.
     */
    inline std::string directoryOf(const std::string &path)
    {
        const size_t cut = path.find_last_of("\\/");
        if (cut == std::string::npos)
        {
            return std::string();
        }
        if (cut == 0)
        {
            return path.substr(0, 1); // "/thing" -> "/"
        }
        return path.substr(0, cut);
    }

    /** A file in the same directory as another. A bare name when there is no directory. */
    inline std::string beside(const std::string &path, const std::string &name)
    {
        const std::string directory = directoryOf(path);
        if (directory.empty())
        {
            return name;
        }
        if (directory.size() == 1 && (directory[0] == '\\' || directory[0] == '/'))
        {
            return directory + name;
        }
#ifdef _WIN32
        return directory + "\\" + name;
#else
        return directory + "/" + name;
#endif
    }
}

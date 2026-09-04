#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <algorithm>

using namespace std;

// ---- i803: one directory helper, used at the 21 former system("mkdir -p") sites ----
#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#ifndef OD_SHELL_MKDIR
#include <filesystem>
#endif

namespace odfs
{
    inline std::atomic<long> ensure_calls{0};
    inline std::atomic<long> ensure_failures{0};

    // Returns true when the directory exists after the call.
    inline bool ensureDirectory(const char *path)
    {
        ensure_calls++;
#ifdef OD_SHELL_MKDIR
        // Pre-change behaviour, kept behind a flag so the two can be measured against
        // each other: fork + exec of /bin/sh, return value discarded.
        std::string cmd = std::string("mkdir -p ") + path;
        (void)::system(cmd.c_str());
        return true;
#else
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec && !std::filesystem::is_directory(path))
        {
            if (ensure_failures++ < 8) // say it, but do not flood a per-cycle loop
            {
                std::cerr << "[ERROR] cannot create directory " << path << ": " << ec.message() << std::endl;
            }
            return false;
        }
        return true;
#endif
    }
}

namespace logging
{
    enum class LogLevel
    {
        ERROR = 0,   // Most important - always show
        WARNING = 1, // Important - usually show
        INFO = 2,    // Normal - sometimes show
        DEBUG = 3    // Least important - rarely show
    };

    // ---- i817: one copy of the logging state, and a log target that outlives exit ----
    //
    // These were four `static` variables at namespace scope in a header included by 17
    // translation units, so the program held 17 copies of each and `setLogLevel` from
    // main() configured whichever one the linker happened to keep. `inline` makes each
    // of them one object across the whole program, which is what every reader already
    // assumed. All three scalars are constant-initialised, so no order of
    // initialisation across translation units can reach them before they have a value.
    inline LogLevel globalLogLevel = LogLevel::INFO; // Default: show ERROR and WARNING only
    inline bool showTimestamp = false;               // Console timestamps off by default
    inline bool enableFileLogging = false;           // File logging off by default

    // The path cannot be one of those, because a std::string has a destructor and this
    // program logs from threads that are still running when exit() runs the destructors:
    // a destroyed string's freed heap pointer was being read as a filename. A
    // function-local static is ordered by the standard rather than by link order, and
    // this one is deliberately never destroyed, so there is no window in which a log
    // call can read it after its lifetime has ended.
    inline string &logFilePath()
    {
        static string *path = new string("debug_frames/opendartboard.log"); // owned for the life of the process
        return *path;
    }

    // Set the global log level
    inline void setLogLevel(LogLevel level)
    {
        globalLogLevel = level;
    }

    // Enable/disable console timestamps
    inline void setShowTimestamp(bool show)
    {
        showTimestamp = show;
    }

    // Enable/disable file logging
    inline void setFileLogging(bool enable, const string &filepath = "debug_frames/opendartboard.log")
    {
        enableFileLogging = enable;
        logFilePath() = filepath;

        if (enable)
        {
            // Create the directory the log is appended to, which is the one named by
            // the path rather than always "debug_frames" (i817: the two disagreed the
            // moment a caller passed a path). If it cannot be created there is nowhere
            // to append to, so file logging is turned back off rather than silently
            // dropping every line.
            const size_t slash = filepath.find_last_of("/\\");
            if (slash != string::npos)
            {
                if (!odfs::ensureDirectory(filepath.substr(0, slash).c_str()))
                {
                    enableFileLogging = false;
                    return;
                }
            }

            // Write header to log file
            ofstream logFile(logFilePath(), ios::app);
            if (logFile.is_open())
            {
                logFile << "\n========== OpenDartboard Session Started ==========\n";
                logFile.close();
            }
        }
    }

    // Get current timestamp as string
    inline string getCurrentTimestamp()
    {
        auto now = chrono::system_clock::now();
        auto time_t = chrono::system_clock::to_time_t(now);
        auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;

        stringstream ss;
        ss << put_time(localtime(&time_t), "%H:%M:%S");
        ss << '.' << setfill('0') << setw(3) << ms.count();
        return ss.str();
    }

    // Convert log level to string
    inline string logLevelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::WARNING:
            return "WARN";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::DEBUG:
            return "DEBUG";
        default:
            return "UNKNOWN";
        }
    }

// no instance of overloaded function "std::to_string" matches the argument list
#define log_string_src(value) ("\033[36m" + value + "\033[0m")

// Helper macro to format numbers with color (blue highlight instead of purple)
// This works for any type that has to_string() support
#define log_string(value) ("\033[36m" + std::to_string(value) + "\033[0m")

    // Extract module name from function signature (smart detection!)
    inline string extractModuleName(const string &function)
    {
        // Skip logging functions - they're not useful for module detection
        if (function.find("logging::") != string::npos ||
            function.find("extractModuleName") != string::npos)
        {
            return "SYSTEM";
        }

        // Look for namespace pattern: "namespace::function" -> "NAMESPACE"
        size_t colonPos = function.find("::");
        if (colonPos != string::npos)
        {
            // Find the start of the namespace name by looking backwards from ::
            size_t startPos = 0;

            // Look for space before namespace
            size_t spacePos = function.rfind(' ', colonPos);
            if (spacePos != string::npos)
            {
                startPos = spacePos + 1;
            }

            // Extract namespace name
            string moduleName = function.substr(startPos, colonPos - startPos);

            // Convert to uppercase
            transform(moduleName.begin(), moduleName.end(), moduleName.begin(), ::toupper);

            return moduleName;
        }

        return "SYSTEM";
    }

    // Helper function to strip ANSI color codes from strings (for file logging)
    inline string stripColorCodes(const string &text)
    {
        string result = text;
        size_t pos = 0;

        // Remove all ANSI escape sequences (start with \033[ and end with 'm')
        while ((pos = result.find("\033[", pos)) != string::npos)
        {
            size_t endPos = result.find('m', pos);
            if (endPos != string::npos)
            {
                result.erase(pos, endPos - pos + 1);
            }
            else
            {
                break; // Malformed escape sequence
            }
        }

        return result;
    }

    // Main logging function
    inline void log(const string &message, LogLevel level = LogLevel::INFO, const string &moduleName = "SYSTEM")
    {
        // Only print if the log level is at or below the global threshold (lower number = higher priority)
        if (level > globalLogLevel)
            return;

        string timestamp = getCurrentTimestamp();
        string levelStr = logLevelToString(level);

        // Color definitions
        string timestampColor = "\033[32m"; // Green for timestamp
        string bracketColor = "\033[37m";   // White for brackets
        string moduleColor = "\033[90m";    // Gray for module name
        string levelColor = "";
        string resetCode = "\033[0m";

        // Set level-specific colors
        switch (level)
        {
        case LogLevel::ERROR:
            levelColor = "\033[91m"; // Bright red
            break;
        case LogLevel::WARNING:
            levelColor = "\033[33m"; // Orange (was yellow)
            break;
        case LogLevel::INFO:
            levelColor = "\033[92m"; // Lime green
            break;
        case LogLevel::DEBUG:
            levelColor = "\033[34m"; // Blue
            break;
        }

        // Build console message with proper colors
        string consoleMessage = "";

        if (showTimestamp)
        {
            consoleMessage += bracketColor + "[" + timestampColor + timestamp + bracketColor + "]" + resetCode;
        }

        consoleMessage += bracketColor + "[" + levelColor + levelStr + bracketColor + "]";
        consoleMessage += bracketColor + "[" + moduleColor + moduleName + bracketColor + "]" + resetCode;
        consoleMessage += " - " + message;

        cout << consoleMessage << endl;

        // File logging (always with timestamp, NO colors)
        if (enableFileLogging)
        {
            ofstream logFile(logFilePath(), ios::app);
            if (logFile.is_open())
            {
                // Strip color codes from message for clean file output
                string cleanMessage = stripColorCodes(message);
                logFile << "[" << timestamp << "][" << levelStr << "][" << moduleName << "] - " << cleanMessage << endl;
                logFile.close();
            }
        }
    }

    // Convenience functions for different log levels
    inline void error(const string &message, const string &module = "SYSTEM")
    {
        log(message, LogLevel::ERROR, module);
    }

    inline void warning(const string &message, const string &module = "SYSTEM")
    {
        log(message, LogLevel::WARNING, module);
    }

    inline void info(const string &message, const string &module = "SYSTEM")
    {
        log(message, LogLevel::INFO, module);
    }

    inline void debug(const string &message, const string &module = "SYSTEM")
    {
        log(message, LogLevel::DEBUG, module);
    }

// Smart macros that auto-detect the module name
// __PRETTY_FUNCTION__ is a GCC/Clang extension; MSVC spells it __FUNCSIG__. The
// module name in every log line is parsed out of it, so this is not cosmetic.
#ifdef _MSC_VER
#define OD_FUNCTION_SIGNATURE __FUNCSIG__
#else
#define OD_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

#define LOG_ERROR(message) logging::log(message, logging::LogLevel::ERROR, logging::extractModuleName(OD_FUNCTION_SIGNATURE))
#define LOG_WARNING(message) logging::log(message, logging::LogLevel::WARNING, logging::extractModuleName(OD_FUNCTION_SIGNATURE))
#define LOG_INFO(message) logging::log(message, logging::LogLevel::INFO, logging::extractModuleName(OD_FUNCTION_SIGNATURE))
#define LOG_DEBUG(message) logging::log(message, logging::LogLevel::DEBUG, logging::extractModuleName(OD_FUNCTION_SIGNATURE))

// Even simpler macros (what you wanted!)
#define log_error(message) LOG_ERROR(message)
#define log_warning(message) LOG_WARNING(message)
#define log_info(message) LOG_INFO(message)
#define log_debug(message) LOG_DEBUG(message)

}

// #1303: start something with NO CONSOLE AT ALL, and say what it returned.
//
// "Started without a console" is a condition, not a window style. A hidden window is
// still a console; a minimised one is still a console; `Start-Process -WindowStyle
// Hidden` still allocates one. The condition #1258's rule is about -- a service, a
// scheduled task, a shortcut with no window -- is DETACHED_PROCESS, where
// GetStdHandle(STD_INPUT_HANDLE) answers NULL and there is no console input buffer to
// have a mode. This is the smallest thing that can put the launcher in it.
//
//   i1303_detached.exe <exe> [args...]
//
// It prints DETACHED_EXIT_CODE=<n> and returns that code.

#include <windows.h>

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <exe> [args...]\n", argv[0]);
        return 2;
    }

    std::string line = "\"";
    line += argv[1];
    line += "\"";
    for (int i = 2; i < argc; i++)
    {
        line += " ";
        line += argv[i];
    }
    std::string writable = line;

    STARTUPINFOA startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof(process));

    if (!CreateProcessA(argv[1], &writable[0], NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &startup, &process))
    {
        std::printf("DETACHED_START_FAILED=%lu\n", (unsigned long)GetLastError());
        return 3;
    }
    CloseHandle(process.hThread);
    // Bounded on purpose: this harness must report a hang rather than join it.
    const DWORD waited = WaitForSingleObject(process.hProcess, 30000);
    if (waited != WAIT_OBJECT_0)
    {
        std::printf("DETACHED_DID_NOT_EXIT\n");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return 4;
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    std::printf("DETACHED_EXIT_CODE=%lu\n", (unsigned long)code);
    return (int)code;
}

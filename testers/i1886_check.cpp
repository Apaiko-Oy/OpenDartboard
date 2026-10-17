// turnaus#1886: the keep-awake request's decision, its pin and its sentences, pure; and the
// Windows branch itself, compiled with _WIN32 against testers/fixtures1886/windows.h, which
// records each SetThreadExecutionState call.
//
// Built twice by testers/i1886_check.sh: once as Linux sees it (-DI1886_POSIX) and once as
// Windows does (-D_WIN32 -I testers/fixtures1886). One OK or FAIL line per assertion; the
// exit status is the count of FAILs.
#include "utils/keep_awake.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static bool has(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

// Everything the logger writes while f runs.
template <typename F>
static std::string said(F f)
{
    std::ostringstream out;
    std::streambuf *was_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf *was_err = std::cerr.rdbuf(out.rdbuf());
    f();
    std::cout.rdbuf(was_out);
    std::cerr.rdbuf(was_err);
    return out.str();
}

static void pin(const char *value)
{
    if (value == nullptr)
    {
        unsetenv("OD_KEEP_AWAKE");
    }
    else
    {
        setenv("OD_KEEP_AWAKE", value, 1);
    }
}

int main()
{
    using keep_awake::Decision;
    using keep_awake::decide;

    // ---- the decision, pure, both platforms ------------------------------------------
    check(decide(nullptr, true) == Decision::Ask, "Windows, pin unset: asks not to sleep");
    check(decide("off", true) == Decision::PinnedOff, "Windows, OD_KEEP_AWAKE=off: leaves it to the power plan");
    check(decide("on", true) == Decision::Ask, "Windows, OD_KEEP_AWAKE=on: asks (only \"off\" turns it off)");
    check(decide("OFF", true) == Decision::Ask, "Windows, OD_KEEP_AWAKE=OFF: asks (a near-miss keeps the board awake)");
    check(decide("", true) == Decision::Ask, "Windows, OD_KEEP_AWAKE empty: asks");
    check(decide("off ", true) == Decision::Ask, "Windows, OD_KEEP_AWAKE='off ': asks");
    check(decide(nullptr, false) == Decision::NotThisPlatform, "POSIX, pin unset: nothing to ask");
    check(decide("off", false) == Decision::NotThisPlatform, "POSIX, pin off: still nothing to ask");
    check(std::string(keep_awake::pinName()) == "OD_KEEP_AWAKE", "the pin is called OD_KEEP_AWAKE");

    // ---- the sentences ------------------------------------------------------------------
    const std::string ask = keep_awake::sentence(Decision::Ask);
    const std::string off = keep_awake::sentence(Decision::PinnedOff);
    check(has(ask, "I1886 KEEP AWAKE") && has(ask, "not to sleep") && has(ask, "display may still turn off"),
          "the start line says the request and that the display may still turn off: " + ask);
    check(has(ask, "OD_KEEP_AWAKE=off"), "the start line names the pin that turns it off");
    check(has(off, "OD_KEEP_AWAKE=off is set") && has(off, "#1886"), "the pinned line says the pin is set: " + off);
    check(keep_awake::sentence(Decision::NotThisPlatform).empty(), "POSIX says nothing");
    check(has(keep_awake::refusedSentence(), "refused"), "a refused request is said as refused");

#ifdef _WIN32
    std::cout << "---- the Windows branch, against a recording SetThreadExecutionState ----" << std::endl;
    check(keep_awake::thisIsWindows(), "this build is the Windows one");

    // 1. Pin unset: one request, the right flags, said once, released on destruction.
    pin(nullptr);
    fake_windows::calls().clear();
    fake_windows::answer() = ES_CONTINUOUS;
    bool held = false;
    size_t while_held = 0;
    const std::string log1 = said([&] {
        keep_awake::Request r;
        held = r.held();
        while_held = fake_windows::calls().size();
    });
    check(held, "the request is held");
    check(while_held == 1, "one call while the detector runs (got " + std::to_string(while_held) + ")");
    check(fake_windows::calls().size() == 2, "a second call when the request is let go (got " +
                                                 std::to_string(fake_windows::calls().size()) + ")");
    if (fake_windows::calls().size() == 2)
    {
        const EXECUTION_STATE first = fake_windows::calls()[0];
        const EXECUTION_STATE last = fake_windows::calls()[1];
        check(first == (ES_CONTINUOUS | ES_SYSTEM_REQUIRED),
              "it asks ES_CONTINUOUS | ES_SYSTEM_REQUIRED (got 0x" +
                  [&] { std::ostringstream o; o << std::hex << first; return o.str(); }() + ")");
        check((first & ES_DISPLAY_REQUIRED) == 0, "it does not hold the display on");
        check((first & ES_AWAYMODE_REQUIRED) == 0, "it does not ask for away mode");
        check(last == ES_CONTINUOUS, "it lets go with ES_CONTINUOUS alone");
    }
    check(has(log1, "I1886 KEEP AWAKE: this board asks Windows not to sleep"), "the start line is said");
    {
        size_t n = 0, at = 0;
        while ((at = log1.find("I1886 KEEP AWAKE", at)) != std::string::npos)
        {
            n++;
            at++;
        }
        check(n == 1, "and said once (got " + std::to_string(n) + ")");
    }

    // 2. The pin: no call at all, and the pin says itself.
    pin("off");
    fake_windows::calls().clear();
    const std::string log2 = said([&] {
        keep_awake::Request r;
        held = r.held();
    });
    check(!held, "OD_KEEP_AWAKE=off: nothing is held");
    check(fake_windows::calls().empty(), "OD_KEEP_AWAKE=off: SetThreadExecutionState is never called (got " +
                                             std::to_string(fake_windows::calls().size()) + ")");
    check(has(log2, "OD_KEEP_AWAKE=off is set"), "OD_KEEP_AWAKE=off is said in the log");
    check(!has(log2, "I1886 KEEP AWAKE"), "and the request is not claimed");

    // 3. Windows refuses: said as refused, nothing to let go.
    pin(nullptr);
    fake_windows::calls().clear();
    fake_windows::answer() = 0;
    const std::string log3 = said([&] {
        keep_awake::Request r;
        held = r.held();
    });
    check(!held, "a refused request is not held");
    check(fake_windows::calls().size() == 1, "and is not released (got " + std::to_string(fake_windows::calls().size()) +
                                                 " calls)");
    check(has(log3, "refused") && !has(log3, "this board asks Windows"), "the log says refused, not held");
#else
    std::cout << "---- the POSIX build: the Request does nothing and says nothing ----" << std::endl;
    check(!keep_awake::thisIsWindows(), "this build is not the Windows one");
    pin(nullptr);
    bool held = true;
    const std::string log = said([&] {
        keep_awake::Request r;
        held = r.held();
    });
    check(!held, "nothing is held");
    check(!has(log, "KEEP AWAKE") && !has(log, "OD_KEEP_AWAKE"), "nothing is said");
#endif

    std::cout << (failures == 0 ? "i1886_check: every check held" : "i1886_check: FAILED") << " (" << failures
              << " failed)" << std::endl;
    return failures;
}

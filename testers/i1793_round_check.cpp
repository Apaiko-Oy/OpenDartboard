// turnaus#1793: after "there was no dart there", the visit's real third dart is pushed.
//
// A board counts arrivals itself, CLEAN -> DART_1 -> DART_2 -> DART_3, and before #1793 a
// board at DART_3 stayed there whatever arrived. When it had counted a dart nobody threw
// and the player removed it in Turnaus (turnaus#1723 withdraws it, so the round has room
// again), the real third dart landed on a board already at DART_3 and nothing was pushed:
// the turn was written one dart short.
//
// This check REPLAYS SUCH A ROUND through the vote's own pure decisions in
// dart_processing.hpp -- the ones dart_processing.cpp and score_processing.cpp call --
// window by window on a three-camera board, and plays Turnaus's side of it with #1281's
// rule: a dart is counted while the round, less what was withdrawn, holds fewer than
// three, and answered DROPPED otherwise. The per-camera candidate is the cpp's: a fresh
// figure over the floor moves a camera one state up and leaves DART_3 at DART_3, a CLEAN
// reading is CLEAN, anything else stays.
//
// Every assertion is labelled by what can turn it red, and testers/i1793_check.sh mutates
// each decision and predicts exactly which labels go red:
//   off:     the switch off -- the board as it was before #1793
//   on:      OD_PAST_THREE=on
//   quorum:  one camera alone past three, held by the quorum as any lone arrival is
//   takeout: the END after a dart past three, and the next round
//   dropped: turnaus_client's DROPPED answer (push_answer.hpp), said once per round under the switch
//   dropped-off: and every time with it off, as before
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1793_round_check
//       testers/i1793_round_check.cpp $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_processing.hpp"
#include "communication/push_answer.hpp"

using namespace dart_processing;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

// What each camera read in one window.
enum class Read
{
    Fresh, // a fresh figure over the floor: something arrived
    Still, // occupied, nothing new
    Clean  // under the CLEAN ceiling, or a reversion: the takeout
};

struct Window
{
    std::string name; // the truth: which dart (or none) this window is
    std::vector<Read> cams;
};

// One window, as dart_processing.cpp decides it. Returns what score_processing publishes:
// "" (nothing), "END", or the window's name (a dart).
static std::string decide(DartBoardState &board, const Window &w, bool switch_on)
{
    int goes_clean = 0, moves_up = 0;
    for (Read r : w.cams)
    {
        DartBoardState candidate = board;
        bool past_three = false;
        if (r == Read::Clean)
        {
            candidate = DartBoardState::CLEAN;
        }
        else if (r == Read::Fresh)
        {
            candidate = board == DartBoardState::DART_3 ? DartBoardState::DART_3
                                                        : static_cast<DartBoardState>(static_cast<int>(board) + 1);
            past_three = votesArrivalPastThree(board, true, switch_on);
        }
        if (votesCleanThisWindow(candidate, 0))
        {
            goes_clean++;
        }
        else if (votesUp(candidate, board, past_three))
        {
            moves_up++;
        }
    }
    const int quorum = stateVoteQuorum((int)w.cams.size());
    bool past_three = false;
    const DartBoardState previous = board;
    board = reconcileVote(board, goes_clean, moves_up, quorum, past_three);
    if (!windowPublishes(previous, board, past_three))
    {
        return "";
    }
    return board == DartBoardState::CLEAN ? "END" : w.name;
}

// Turnaus's round in hand (turnaus#1281, #1723): what a pushed dart is answered.
struct TurnausRound
{
    std::vector<std::string> darts;     // counted into the round
    std::vector<std::string> withdrawn; // "there was no dart there"
    std::vector<std::string> dropped;   // answered DROPPED

    void push(const std::string &dart)
    {
        if (darts.size() - withdrawn.size() < 3)
        {
            darts.push_back(dart);
        }
        else
        {
            dropped.push_back(dart);
        }
    }
    void withdraw(const std::string &dart) { withdrawn.push_back(dart); }
    std::string turn() const
    {
        std::string t;
        for (const std::string &d : darts)
        {
            bool gone = false;
            for (const std::string &w : withdrawn)
            {
                gone = gone || w == d;
            }
            if (!gone)
            {
                t += (t.empty() ? "" : " ") + d;
            }
        }
        return t;
    }
};

struct Replay
{
    std::vector<std::string> pushed;
    TurnausRound round;
    DartBoardState board_after = DartBoardState::CLEAN;
    bool ended = false;
};

// Replays the windows; `withdraw` is the dart the player removes as soon as it is pushed.
static Replay replay(const std::vector<Window> &windows, bool switch_on, const std::string &withdraw = "")
{
    Replay r;
    DartBoardState board = DartBoardState::CLEAN;
    for (const Window &w : windows)
    {
        const std::string out = decide(board, w, switch_on);
        if (out == "END")
        {
            r.ended = true;
        }
        else if (!out.empty())
        {
            r.pushed.push_back(out);
            r.round.push(out);
            if (out == withdraw)
            {
                r.round.withdraw(out);
            }
        }
    }
    r.board_after = board;
    return r;
}

static std::string listOf(const std::vector<std::string> &v)
{
    std::string s;
    for (const std::string &x : v)
    {
        s += (s.empty() ? "" : " ") + x;
    }
    return "[" + s + "]";
}

int main()
{
    const std::vector<Read> all = {Read::Fresh, Read::Fresh, Read::Fresh};
    const std::vector<Read> two = {Read::Fresh, Read::Still, Read::Fresh};
    const std::vector<Read> one = {Read::Still, Read::Fresh, Read::Still};
    const std::vector<Read> clean = {Read::Clean, Read::Clean, Read::Clean};

    // The round #1793 is about: a knock the board counts as a dart, then three real darts.
    // The player removes the phantom in Turnaus as soon as it shows.
    const std::vector<Window> phantom_round = {
        {"phantom", all}, {"T20", all}, {"S5", two}, {"S1", all}, {"takeout", clean}};

    {
        const Replay r = replay(phantom_round, false, "phantom");
        say(r.pushed == std::vector<std::string>{"phantom", "T20", "S5"} && r.round.turn() == "T20 S5",
            "off: the board stops counting at three, so the real third dart S1 is never pushed and the turn is "
            "written short -- pushed " + listOf(r.pushed) + ", turn [" + r.round.turn() + "]");
    }
    {
        const Replay r = replay(phantom_round, true, "phantom");
        say(r.pushed == std::vector<std::string>{"phantom", "T20", "S5", "S1"} && r.round.turn() == "T20 S5 S1" &&
                r.round.dropped.empty(),
            "on: the real third dart S1 is pushed past three and Turnaus counts it into the room the withdrawal "
            "left -- pushed " + listOf(r.pushed) + ", turn [" + r.round.turn() + "], dropped " +
                listOf(r.round.dropped));
    }

    // A full round and then a fourth arrival nobody threw (a bounce-out's neighbour, a hand
    // reaching in): pushed, and Turnaus drops it -- the written turn is the same as off.
    {
        const std::vector<Window> full = {{"T20", all}, {"T20b", all}, {"T20c", two}, {"fourth", all},
                                          {"takeout", clean}};
        const Replay off = replay(full, false);
        const Replay on = replay(full, true);
        say(on.pushed.size() == 4 && on.round.dropped == std::vector<std::string>{"fourth"} &&
                on.round.turn() == off.round.turn() && off.pushed.size() == 3,
            "on: a fourth arrival into a full round is pushed and answered DROPPED, and the written turn is "
            "the switch-off turn [" + off.round.turn() + "] -- on turn [" + on.round.turn() + "], dropped " +
                listOf(on.round.dropped));
    }

    // One camera alone past three: the quorum holds it, as it holds a lone arrival at any state.
    {
        const std::vector<Window> lone = {{"T20", all}, {"S5", all}, {"S1", all}, {"lone", one}};
        const Replay r = replay(lone, true);
        say(r.pushed == std::vector<std::string>{"T20", "S5", "S1"},
            "quorum: one camera alone voting an arrival past three is held -- pushed " + listOf(r.pushed));
    }

    // The takeout after a dart past three still reconciles CLEAN, publishes an END, and the
    // next round counts from DART_1 again.
    {
        const std::vector<Window> two_rounds = {
            {"phantom", all}, {"T20", all}, {"S5", all}, {"S1", all}, {"takeout", clean}, {"D16", all}};
        const Replay r = replay(two_rounds, true);
        say(r.ended && r.board_after == DartBoardState::DART_1 && r.pushed.back() == "D16",
            "takeout: after a dart past three the takeout is an END and the next round starts at DART_1 -- "
            "board after " + getDartBoardStateName(r.board_after) + ", pushed " + listOf(r.pushed));
    }

    // turnaus_client: a DROPPED answer is ordinary -- said once per round under the switch.
    {
        bool said = false;
        int said_count = 0;
        for (int i = 0; i < 3; i++)
        {
            if (droppedAnswerIsSaid("DROPPED", said, true))
            {
                said_count++;
                said = true;
            }
        }
        const bool counted_always = droppedAnswerIsSaid("COUNTED", true, true);
        say(said_count == 1 && counted_always,
            "dropped: three DROPPED answers in one round are said once (" + std::to_string(said_count) +
                "), and COUNTED is always said");
        int off_count = 0;
        for (int i = 0; i < 3; i++)
        {
            off_count += droppedAnswerIsSaid("DROPPED", i > 0, false) ? 1 : 0;
        }
        say(off_count == 3, "dropped-off: with the switch off every DROPPED answer is said, as before (" +
                                std::to_string(off_count) + " of 3)");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

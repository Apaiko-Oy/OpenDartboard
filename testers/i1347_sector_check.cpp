// #1347: the grammar seam, held to the server's own pattern.
//
// `TurnausClient::postableSector` is the one place the detector's score vocabulary
// becomes #821's sector grammar. The defect it closes was found by reading both sides:
// the detector publishes BULL and OUTER (docs/api.md), the door validates
// Sector::PATTERN, which spells them Bull and 25, and offer() dropped everything the
// pattern refuses -- so every bull the detector will ever score was dropped before the
// spool, counted as an unpostable sector.
//
// The needle is proved before its absence means anything (#708's shape): the RAW
// detector words BULL and OUTER are shown to fail the server's pattern -- that is the
// drop existing -- and then every non-empty answer the seam gives is shown to match it.
// The pattern is quoted from src/Domain/Scoring/Darts/Sector.php verbatim, so a server
// that moves its grammar makes this file's control wrong rather than silently stale.
//
//   g++ -std=c++17 -I src -I src/utils -o sector_check testers/i1347_sector_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "communication/turnaus_client.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

int main()
{
    // Sector::PATTERN, PHP's \A...\z said in std::regex: the whole string or nothing.
    const std::regex server_pattern("^(?:([SsDT])(20|1[0-9]|[1-9])|25|Bull|None)$");

    // ---- the needle: the detector's own bull words fail the server's grammar -----------
    say(!std::regex_match("BULL", server_pattern) && !std::regex_match("OUTER", server_pattern),
        "the raw detector words BULL and OUTER do not match Sector::PATTERN -- the drop this "
        "seam closes was real, and if this line fails the server has learned them instead");

    // ---- the translation --------------------------------------------------------------
    say(TurnausClient::postableSector("BULL") == "Bull", "BULL becomes Bull");
    say(TurnausClient::postableSector("OUTER") == "25", "OUTER becomes 25");
    say(TurnausClient::postableSector("MISS") == "None", "MISS still becomes None");

    // ---- what was already spelled the door's way passes through -----------------------
    for (const std::string &score : {"S5", "s5", "D20", "T19", "S1", "25", "Bull", "None"})
    {
        say(TurnausClient::postableSector(score) == score, score + " passes through unchanged");
    }

    // ---- what the grammar cannot express still answers empty --------------------------
    // END never reaches the seam -- offer() takes the takeout branch first -- but if it
    // ever did, it must not leave as a sector.
    for (const std::string &score : {"", "END", "S21", "S0", "S05", "20", "X5", "D100", "bull", "BULLS"})
    {
        say(TurnausClient::postableSector(score).empty(),
            "'" + score + "' is refused, for offer() to drop and count");
    }

    // ---- every non-empty answer satisfies the server ----------------------------------
    // The whole point of the seam: nothing it lets out can meet the 422 the grammar
    // answers, so a bull is never again lost between a green detector and a green server.
    const std::vector<std::string> everything = {
        "BULL", "OUTER", "MISS", "S5", "s5", "D20", "T19", "S1", "25", "Bull", "None",
        "", "END", "S21", "S0", "S05", "20", "X5", "D100", "bull", "BULLS"};
    bool all_postable = true;
    for (const std::string &score : everything)
    {
        const std::string sector = TurnausClient::postableSector(score);
        if (!sector.empty() && !std::regex_match(sector, server_pattern))
        {
            all_postable = false;
            std::cout << "     '" << score << "' came out as '" << sector
                      << "', which Sector::PATTERN refuses" << std::endl;
        }
    }
    say(all_postable, "every non-empty answer the seam gives matches Sector::PATTERN");

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}

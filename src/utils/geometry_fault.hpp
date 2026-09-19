#pragma once
// #1388 / ADR-0080 §4: the `Moved` verdict that survives the restart, because today it
// does not.
//
// THE HOLE THIS FILLS. #899's stated remedy for a board whose geometry has been
// contradicted is "a human restarting the detector". On a deployed board that remedy is
// already performed, automatically, by nobody:
//
//     Moved -> running = false -> the process exits
//           -> the unit's Restart=always -> systemd starts it again
//           -> the cached calibration is not read by default (#1330)
//           -> a fresh calibration is taken and adopted
//
// So a rig that has genuinely shifted on its bolts faults, comes straight back up scoring
// on whatever it now sees, and nothing anywhere records that the geometry changed. The
// adoption #899 refused happens anyway -- not as a decision with a logged delta, but
// through the unit file. ADR-0080 §1 closes that for the BUMP case by construction,
// because after #1388 a bump no longer exits. This file closes it for the persistent
// case, which has to be closed deliberately: the verdict outlives the process, and a
// board that starts holding one refuses to adopt fresh geometry until an operator clears
// it.
//
// WHY A FILE AND NOT THE CALIBRATION CACHE. The cache holds a measurement and is read
// only when an operator asks for it (#1330). This holds a refusal and must be read on
// every start whether anybody asked or not, which is the opposite default; putting the
// two in one file would mean one of them got the wrong one.
//
// WHERE IT LIVES, AND THE ONE THING THAT IS WRONG WITH THAT. `cache/geometry_moved.txt`,
// beside the calibration cache, named by nothing but the working directory -- which is
// #1330's complaint about the cache file, and it is the same complaint here. It does not
// say whose geometry it is, so a second board started in the same directory inherits this
// one's refusal.
//
// It is accepted here and was not there, for one reason: the two fail in opposite
// directions. A calibration inherited by the wrong board is a board scoring confidently
// through another camera's perspective, which is #1318 exactly. A refusal inherited by
// the wrong board is a board that will not score until somebody looks at it -- an
// operator's afternoon, and ADR-0055's rule kept rather than broken. A guard whose
// failure mode is refusing too much may be named by the working directory; a measurement
// whose failure mode is scoring wrongly may not.
//
// WHAT CLEARS IT. An operator, with `--clear-geometry-fault`, and nothing else. Not a
// restart, not a successful calibration, not time. ADR-0080 §3: a frame that has shifted
// on its bolts is a physical fault somebody has to look at, and a board that cleared its
// own record would be a board deciding it had been fixed.

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "utils.hpp"

namespace geometry_fault
{
    /** The file a restart reads. Relative to the working directory, deliberately. */
    inline std::string path()
    {
        (void)odfs::ensureDirectory("cache");
        return "cache/geometry_moved.txt";
    }

    /**
     * What a previous run recorded, or an empty string.
     *
     * The whole file is the account, so a reader that cannot parse it still has the
     * sentence. Anything unreadable-but-present is deliberately treated as a fault held
     * rather than as no fault: a file that exists and cannot be read is not evidence that
     * the board is fine.
     */
    inline std::string held()
    {
        std::ifstream file(path());
        if (!file)
        {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string detail = buffer.str();
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
        {
            detail.pop_back();
        }
        return detail.empty() ? std::string("a geometry fault was recorded with no detail") : detail;
    }

    /**
     * Record a persistent disagreement, if nothing is recorded yet.
     *
     * First fault wins, for `board_sight::recordFault`'s reason: the first thing that
     * could not be confirmed is the thing to go and look at. Returns whether anything was
     * written, so the caller can say so in the log rather than guess.
     */
    inline bool record(const std::string &account)
    {
        if (!held().empty())
        {
            return false;
        }
        std::ofstream file(path(), std::ios::trunc);
        if (!file)
        {
            log_error("Could not record the geometry fault at " + path() +
                      "; this board will come back up and calibrate on whatever it can see, "
                      "which is what ADR-0080 exists to stop. The disagreement was: " + account);
            return false;
        }
        const std::time_t now = std::time(nullptr);
        char when[32] = {0};
        std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
        file << account << "\n"
             << "recorded " << when << "\n";
        return true;
    }

    /** An operator has looked at the rig. Returns whether there was anything to clear. */
    inline bool clear()
    {
        if (held().empty())
        {
            return false;
        }
        return std::remove(path().c_str()) == 0;
    }
}

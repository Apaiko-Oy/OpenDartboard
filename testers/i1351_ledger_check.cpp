// #1351: the cursor's arithmetic, on its own, and the lost club dart replayed against it.
//
// The cursor on disk is a count of settled leading records, and the count model moved on
// EVERY settlement, in whatever order it came -- so a Contest ending, which erases
// spooled records from the middle of the queue, advanced the cursor over leading club
// records that were never delivered, and the next start skipped them. `SpoolLedger` is
// that arithmetic extracted pure (#1338's shape): records settle by index in any order,
// and the prefix -- the only thing the cursor file ever says -- advances only over
// records actually settled, stopping at the first still owed.
//
//   g++ -std=c++17 -I src -I src/utils -o ledger_check testers/i1351_ledger_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

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
    // ---- the lost club dart, replayed ---------------------------------------------------
    // The spool holds [club, contest, contest, club]; the Contest ends and its two
    // records are dropped from the middle. On the count model the cursor became 2 and
    // covered the leading club record -- undelivered -- so a restart skipped it.
    {
        SpoolLedger ledger;
        const long long club_a = ledger.recordAppended();   // 0
        const long long contest_b = ledger.recordAppended(); // 1
        const long long contest_c = ledger.recordAppended(); // 2
        const long long club_d = ledger.recordAppended();   // 3

        say(!ledger.settle(contest_b) && !ledger.settle(contest_c),
            "the Contest's two mid-file records settle without advancing the prefix");
        say(ledger.settledPrefix() == 0,
            "the cursor still says 0, so a restart rescans the club record at the front -- "
            "the count model said 2 here, and that is the lost dart");

        say(ledger.settle(club_a) && ledger.settledPrefix() == 3,
            "the club record delivering settles it AND releases the two waiting behind it: "
            "the prefix jumps to 3");
        say(ledger.settle(club_d) && ledger.settledPrefix() == 4 && ledger.allSettled(),
            "the last record delivers and the file is settled whole");
    }

    // ---- loadSpool's orphan, mid-scan ---------------------------------------------------
    // [club resumed, contest orphaned, club resumed]: the old assignment declared
    // everything before the orphan settled, including the club record the scan had just
    // resumed into the queue.
    {
        SpoolLedger ledger;
        ledger.recordAppended(); // 0: club, resumed
        const long long orphan = ledger.recordAppended(); // 1: contest, orphaned
        ledger.recordAppended(); // 2: club, resumed

        ledger.settle(orphan);
        say(ledger.settledPrefix() == 0,
            "an orphaned mid-file record settles alone -- the resumed club record before it "
            "stays under the cursor's protection");
    }

    // ---- what the ledger refuses --------------------------------------------------------
    {
        SpoolLedger ledger;
        const long long only = ledger.recordAppended();

        say(!ledger.settle(-1),
            "-1 -- the record a full disk never took -- is refused, so an unwritten record "
            "can never move the cursor past a written one");

        say(ledger.settle(only) && !ledger.settle(only) && ledger.settledPrefix() == 1,
            "settling the same record twice moves the prefix once -- a rescan after a "
            "restart re-settles into the same verdict for free");
    }

    // ---- the cursor file's meaning is unchanged -----------------------------------------
    // A file an earlier build wrote says "N leading records are settled"; startFrom is
    // that N, and the scan settles from there.
    {
        SpoolLedger ledger;
        ledger.startFrom(2);
        ledger.recordAppended(); // 0, under the prefix: loadSpool skips it
        ledger.recordAppended(); // 1, under the prefix
        const long long fresh = ledger.recordAppended(); // 2, owed

        say(!ledger.settle(0),
            "a record under the prefix is already covered; settling it again is a no-op");
        say(ledger.settle(fresh) && ledger.settledPrefix() == 3 && ledger.allSettled(),
            "the record above the prefix settles and the count reads exactly as every "
            "existing cursor file does");
    }

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}

// turnaus#1875: a score one camera read alone publishes FLAGGED, even where that camera's own
// LONE-WIRE / RING-WIRE account says it is clear of the 5 mm sigma -- by default.
//
// The rule is `score_processing::flagAnUncheckedLoneReading`, pure, in score_processing.hpp.
// This is a TRUTH-LINE REPLAY, not a detector replay: the kept frames are three settled stills
// per corrected dart, not footage, so what is held is the rule at the seam that takes what the
// board published. Every lone row below is a dart from device/2's truth line (path `vote`,
// `agreeing` 1, a scoring sector) inside the maintainer's corrected span of each day --
// 2026-10-15 darts 2519-3004 (486), 2026-10-16 darts 8-471 (464) -- with what it published,
// whether it was flagged and with what, the first of its posted candidates, what the thrower
// corrected it to ("" where it stood), and whether v0.1.16's #1867 RIM CONTRADICTS would flag
// it (read from the day's log: I1707 RIM CARRIED, 0 usable constraints, a rim-only camera, a
// single/triple/bull/outer inside 134.5 mm). The sessions are kept off GitHub; the rows were
// cut from them by hand-run script and are the only part of them here.
//
// An uncorrected dart inside a corrected span is PROBABLY right, not certainly, so a flag on
// one is counted as a flag added, and any "lost" figure is an upper estimate.
//
//   g++ -std=c++17 -I src -I src/utils -o lone testers/i1875_lone_flag_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// PREFIXES, which i1875_check.sh counts:
//   lone:      a lone hit the truth line shows UNFLAGGED; after the rule it is flagged, with
//              its first candidate as the alternative. 53 rows (34 + 19). Mutation A (the rule
//              never fires) turns exactly these red, plus the named and count lines below.
//   named:     the two 2026-10-16 darts the issue names, and #1822's unflagged S3 for a T3.
//   count:     the per-day tallies this replay reports, as numbers.
//   kept:      a lone hit something already flagged keeps its flag and its alternative --
//              RING-WIRE's, and on a v0.1.16 board #1867's MISS. No mutation of the flag
//              reaches these.
//   pure:      a consensus, a wedge-by-default, a MISS, an empty candidate list (not flagged).

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

struct Row
{
    const char *day; // "15" or "16" (2026-10-)
    int dart;        // the dart's number in the day's truth line
    const char *reference;
    const char *published;
    bool flagged;           // as the truth line has it
    const char *alternative; // as the truth line has it; "" where none
    const char *first;       // the first posted candidate; "" where none
    const char *corrected;   // what the thrower corrected it to; "" where it stood
    bool rim_contradicts;    // v0.1.16's #1867 rule flags it with MISS
};

static const Row kRows[] = {
    {"15", 2519, "01M50ETFWWRPESVNDFN5BH3QJW", "D5", true, "S5", "S5", "S5", false},
    {"15", 2521, "01M50ETYTXBT2909KV47A46ADY", "S7", false, "", "S19", "", false},
    {"15", 2530, "01M50EW79VJ0W70YPC704CK7V3", "S17", false, "", "S3", "", false},
    {"15", 2546, "01M50EZ9Y6EP10T9HBWD079WNV", "D9", true, "S9", "S9", "S9", false},
    {"15", 2548, "01M50EZY981QMNNB9FQ13736VZ", "D14", true, "S14", "S14", "", false},
    {"15", 2549, "01M50F0TFW5XRWM2Z8Y5Q9WA4V", "S14", false, "", "S9", "", false},
    {"15", 2554, "01M50F1NG5Q17WDNGPP6QZMD97", "S2", false, "", "D2", "", false},
    {"15", 2556, "01M50F25K50YF71T6NNB7F9WRK", "S2", false, "", "S17", "", false},
    {"15", 2563, "01M50F306M4AD74H6RA8JSDYAE", "D15", true, "None", "None", "", false},
    {"15", 2566, "01M50F3CYS1SVEK4Q46HVBDSFC", "D2", true, "None", "None", "", false},
    {"15", 2568, "01M50F3PM1PAN2BAFKHG4172QJ", "D15", true, "None", "None", "", false},
    {"15", 2569, "01M50F3RWDH7CDDHBW65RWBRA8", "S3", false, "", "S19", "", false},
    {"15", 2578, "01M50F7D0K9D84TY20K1ES2M1R", "S1", true, "D1", "D1", "", false},
    {"15", 2589, "01M50F9FCQFNX8XV7CY3SX1N8H", "D2", true, "None", "None", "", false},
    {"15", 2602, "01M50FCTZ3JHEN3THQPAFQEVDH", "S20", false, "", "D20", "", false},
    {"15", 2610, "01M50FDTB12EAZP5QWNMAG4G68", "S20", false, "", "T20", "", false},
    {"15", 2611, "01M50FE5A4NYXHAVKFXXJC138V", "D5", true, "None", "None", "", false},
    {"15", 2619, "01M50FF33N2AJR6F5VQ076FZ48", "S20", false, "", "S5", "", false},
    {"15", 2629, "01M50FH92K4QCZ5CCGEFDVB9FX", "D3", true, "S3", "S3", "S19", false},
    {"15", 2630, "01M50FHBTRMBQR9FWGEYW67J4G", "D19", true, "None", "None", "None", false},
    {"15", 2631, "01M50FHV875RHVCCZRWSR4X5SM", "S3", false, "", "D10", "", false},
    {"15", 2637, "01M50FKR54CQ2KRSFT4TAZ1H0S", "S18", false, "", "S4", "", false},
    {"15", 2640, "01M50FNYXHSTGB8GEDBFD5CKW9", "D11", true, "None", "None", "", false},
    {"15", 2642, "01M50FPEVCVNA2YHQQ4KS43K7P", "D11", true, "S11", "S11", "None", false},
    {"15", 2644, "01M50FQ3C6EZYD8Z2CS8YEXXKG", "S4", false, "", "D4", "", false},
    {"15", 2663, "01M50FZ1S7N4BMCFYHWQ1T36K6", "T20", true, "S20", "S20", "", false},
    {"15", 2665, "01M50FZZ9G25DAWRQEG0E8PG8X", "D18", true, "S18", "S18", "S18", false},
    {"15", 2682, "01M50G3V6NS2AHRRXB98F38MEY", "S18", false, "", "D18", "", false},
    {"15", 2683, "01M50G43ACBH536WT7ZGA1TPP9", "D1", true, "None", "None", "", false},
    {"15", 2688, "01M50G5RET5817D10QHWEAJC4B", "S20", false, "", "S5", "", false},
    {"15", 2689, "01M50G5TY61JR17Z8Y1F8Y5C5W", "S20", false, "", "D20", "", false},
    {"15", 2690, "01M50G62PC3KATHEGVZZ82DMZT", "S20", false, "", "D20", "", false},
    {"15", 2691, "01M50G64Y12D8JE6GXMHRC0PFZ", "S20", false, "", "D20", "", false},
    {"15", 2692, "01M50G66ZA5PBXCS7WZXCHBSMB", "D1", true, "S1", "S1", "", false},
    {"15", 2695, "01M50G6P3H64F6V1P3VPR6F4SZ", "S20", false, "", "S1", "", false},
    {"15", 2700, "01M50G7FSHB4GP7A5BAZAP80YP", "S20", false, "", "S1", "", false},
    {"15", 2707, "01M50G8NQXJTHXM2Z9FXR8V2JV", "S19", false, "", "S7", "", true},
    {"15", 2708, "01M50G9CJPWE68ZVXSY8A7XGPW", "D1", true, "None", "None", "", false},
    {"15", 2724, "01M50GEXHEYFVF6GAF823M8NR7", "D13", true, "S13", "S13", "S13", false},
    {"15", 2726, "01M50GFHEB5JBSGQ7509F3X7DE", "D15", true, "None", "None", "", false},
    {"15", 2728, "01M50GFXQ04G06ZQ8CH62GEH6X", "D18", true, "S18", "S18", "None", false},
    {"15", 2737, "01M50GHMM8NSWC2KTCBRMN9J5Q", "D1", true, "None", "None", "", false},
    {"15", 2753, "01M50GN9ADXR99YCX8TPA5S8A5", "S17", false, "", "S2", "", false},
    {"15", 2768, "01M50GQQC35K40ZFAH6521ANAX", "S20", true, "T20", "T20", "", false},
    {"15", 2769, "01M50GQSAGZBS6QP3E0ZX239CA", "S1", false, "", "S20", "", false},
    {"15", 2771, "01M50GRGS66K3MTA0JT6JY2DH6", "D10", true, "None", "None", "", false},
    {"15", 2787, "01M50GWGE7Z19YSKYCKM30FA88", "T6", true, "S6", "S6", "None", true},
    {"15", 2788, "01M50GWK2YKGBZ45FWKV8STC70", "S18", false, "", "S4", "None", true},
    {"15", 2791, "01M50GY6B04WCWCEWPD0P42AM3", "S18", false, "", "S4", "", false},
    {"15", 2792, "01M50GYKYHN3SPPC593NA9DZ5V", "S18", false, "", "D18", "", false},
    {"15", 2793, "01M50GYYEBQQGX1HBEQ58GEB7H", "D1", true, "None", "None", "", false},
    {"15", 2798, "01M50J698V34KDCEHK2QZ63K2K", "S20", false, "", "D20", "", false},
    {"15", 2805, "01M50J7ME4KYJFYASRWCH6S3B2", "S19", false, "", "S7", "S7", false},
    {"15", 2814, "01M50J9ZCT1TN370F75HHM2BMD", "S11", false, "", "S8", "", false},
    {"15", 2819, "01M50JCEQWCTE1WWWA102WYGZA", "D16", true, "None", "None", "", false},
    {"15", 2828, "01M50JES3ZWS2EHYHA1DTR8PXP", "S16", false, "", "S8", "", false},
    {"15", 2851, "01M50JRT4BSVMPR8C6RZYB8H96", "D15", true, "None", "None", "", false},
    {"15", 2862, "01M50JW4CR0M0VQMHF52B28THX", "S19", false, "", "D19", "", false},
    {"15", 2881, "01M50JZZN5TJWBFKEMKBTFJQEA", "S3", false, "", "S17", "", true},
    {"15", 2885, "01M50K1JVWDCJ1NR0B806C3RDS", "D20", true, "None", "None", "", false},
    {"15", 2899, "01M50K52NMHEN9JX0PV0MVG6DZ", "D20", true, "None", "None", "", false},
    {"15", 2902, "01M50K64QY9KETY7GCRTYG5K31", "S18", false, "", "S13", "", false},
    {"15", 2906, "01M50K7NRSZ9Y3RRRCBKXNQQBE", "S19", false, "", "S7", "S7", false},
    {"15", 2920, "01M50KADKQC6PX0GR4HAZF37WX", "T5", true, "S5", "S5", "S20", false},
    {"15", 2925, "01M50KBT7TXD9RKZQMNE28DYXC", "T20", true, "S20", "S20", "", false},
    {"15", 2939, "01M50KETMGH2328JDFT5C9AXWP", "D20", true, "S20", "S20", "S20", false},
    {"15", 2940, "01M50KFKGQ5JWMNHF3K8H83V6Z", "D1", true, "None", "None", "", false},
    {"15", 2946, "01M50KH0Y6JTSHYTKADH4A6F68", "S2", true, "T2", "T2", "", true},
    {"15", 2965, "01M50KNGFM12W3F7H411AR38MC", "S12", false, "", "S9", "", false},
    {"15", 3002, "01M50KY05BBER4JBDDK66XE5K7", "S6", true, "25", "25", "None", true},
    {"15", 3003, "01M50KY2B9V57R34W0Z3W14P61", "D15", true, "S15", "S15", "S10", false},
    {"16", 14, "01M50PK7WERZ5992HV2JY1CTRZ", "S19", true, "T19", "T19", "", false},
    {"16", 38, "01M50PS5CZ731AFTTD0JF9FAGC", "S7", false, "", "S19", "", false},
    {"16", 53, "01M50PV3QDB1HEEEJDH6YJSG5V", "D20", true, "None", "None", "", false},
    {"16", 62, "01M50PX8NNJG3GTKWF9Q31YYTS", "S20", false, "", "S1", "", false},
    {"16", 81, "01M50Q0F8Q3JHDTGTY9TE0BTTK", "S9", true, "T9", "T9", "", false},
    {"16", 93, "01M50Q2FSP50X5W73ZY6RXGVF6", "S4", false, "", "S18", "", false},
    {"16", 102, "01M50Q3WBRDZFMSDYW32SDJK6N", "D16", true, "S16", "S16", "", false},
    {"16", 106, "01M50Q4DCK18X0FX7830SW5QV5", "D8", true, "None", "None", "", false},
    {"16", 108, "01M50Q4JJZN42S2QAKNHRY1Q3N", "D16", true, "S16", "S16", "", false},
    {"16", 112, "01M50Q5A39JJRAMV9CWM1565SK", "D8", true, "None", "None", "", false},
    {"16", 113, "01M50Q5CCWASH30ZR9HEPXERH8", "D5", true, "S5", "S5", "D8", false},
    {"16", 122, "01M50Q80Z2T9QARABYRPZ0MZDM", "S19", false, "", "T8", "", false},
    {"16", 133, "01M50Q9DHJ89DZKGV9ZFDYY0D7", "S20", true, "D20", "D20", "", false},
    {"16", 187, "01M50QXXAGDACARAT833DJ8KKB", "S14", false, "", "S9", "", false},
    {"16", 188, "01M50QY0A76M8EH9MFF510HGAH", "25", true, "Bull", "Bull", "", false},
    {"16", 206, "01M50R1M4W90JBWV48BYD76E92", "S2", false, "", "S17", "", false},
    {"16", 208, "01M50R23Z3TDJHC1YY8YPY5ZE5", "S3", false, "", "T7", "", false},
    {"16", 209, "01M50R266WBNP4RN0Q2JC98FRW", "S19", false, "", "S3", "", false},
    {"16", 240, "01M50R6B49XXFQMX9V366BHTRR", "S5", false, "", "S12", "", false},
    {"16", 243, "01M50R6R2RTPCH9AM3S8QWKS24", "S20", false, "", "S1", "", true},
    {"16", 248, "01M50R7J0HJHB0RZEWQXYPP97V", "D10", true, "None", "None", "", false},
    {"16", 273, "01M52FZFT9Y75WTKHGXMH2QYPP", "S8", false, "", "T8", "", false},
    {"16", 278, "01M52G0B6A85N0D002M31CJKQ1", "S3", false, "", "S20", "", false},
    {"16", 302, "01M52G5H9EQNEPYYKKEGZY0QQQ", "S2", false, "", "S6", "", false},
    {"16", 306, "01M52G8W0MR9AYQGVH25VG8BP4", "D20", true, "None", "None", "D1", false},
    {"16", 309, "01M52G9T956RF9G4HVGTT2PF8Q", "S18", false, "", "S1", "", true},
    {"16", 315, "01M52GBBJ7JGZ4SPPYXJCTWF8T", "D5", true, "S5", "S5", "S5", false},
    {"16", 330, "01M52GE3GMT0S1MJHSHJFP9TDE", "T20", true, "S20", "S20", "", true},
    {"16", 332, "01M52GFT46TYHWJMBQPTH3YWXD", "D20", true, "None", "None", "", false},
    {"16", 335, "01M52GGA8RE8R1RGHTW2F6FP0Z", "S16", false, "", "S8", "S20", true},
    {"16", 340, "01M52GHVEGDA5N61BD51PW25T9", "T20", true, "S20", "S20", "S20", false},
    {"16", 345, "01M52GJWX73TNVZSEW9GPZG6R9", "D19", true, "None", "None", "", false},
    {"16", 373, "01M52GSVP6H27T66CAQ8JS3HK9", "D20", true, "None", "None", "", false},
    {"16", 375, "01M52GT2AJY6SHFRG3T3A0BA7C", "S6", false, "", "S13", "", true},
    {"16", 397, "01M52GZE7MV5A7NR3QW5V6DP96", "D20", true, "None", "None", "", false},
    {"16", 419, "01M52H3K8MMW7DT34NM872V4WM", "D19", true, "S19", "S19", "S19", false},
    {"16", 466, "01M52HC72118S80MSF5QNYRWH2", "S1", false, "", "S18", "", false},
    {"16", 469, "01M52HD12435C3AXRY749JJGPW", "S15", false, "", "S9", "", false},
    {"16", 471, "01M52HDF91B8AK0AP5QVG4PBH6", "S1", false, "", "S20", "D20", true},
};

struct Published
{
    bool flagged = false;
    std::string alternative;
};

// The vote branch's end as score_processing.cpp writes it: whatever flagged the dart before
// stands (`v0116` adds #1867's MISS on top of what the truth line's older build published),
// then the lone rule is asked with the dart's ranked candidates -- which, as dart_candidates
// ranks them, start with the flagged alternative where there is one.
static Published published(const Row &r, bool v0116)
{
    Published p;
    p.flagged = r.flagged;
    p.alternative = r.alternative;
    if (v0116 && r.rim_contradicts)
    {
        p.flagged = true;
        p.alternative = "MISS";
    }
    std::vector<std::string> candidates;
    if (p.flagged && !p.alternative.empty())
    {
        candidates.push_back(p.alternative);
    }
    if (r.first[0] != '\0')
    {
        candidates.push_back(r.first);
    }
    const LoneUnchecked u = flagAnUncheckedLoneReading(true, p.flagged, 1, r.published, candidates, 0.7f);
    if (u.flag)
    {
        p.flagged = true;
        p.alternative = u.alternative;
    }
    return p;
}

static std::string row(const Row &r)
{
    return std::string("2026-10-") + r.day + " " + std::to_string(r.dart) + " " + r.reference + " " +
           r.published + (r.corrected[0] ? std::string(" (corrected ") + r.corrected + ")" : std::string(" (stood)"));
}

struct Tally
{
    int lone = 0, before = 0, added = 0, caught = 0, oneTap = 0;
};

int main()
{
    Tally t[2][2]; // [day 15/16][truth-line build / v0.1.16]
    for (const Row &r : kRows)
    {
        const int d = std::string(r.day) == "15" ? 0 : 1;
        for (int b = 0; b < 2; b++)
        {
            const bool beforeFlagged = r.flagged || (b == 1 && r.rim_contradicts);
            const Published p = published(r, b == 1);
            Tally &x = t[d][b];
            x.lone++;
            x.before += beforeFlagged ? 1 : 0;
            if (p.flagged && !beforeFlagged)
            {
                x.added++;
                if (r.corrected[0])
                {
                    x.caught++;
                    x.oneTap += p.alternative == r.corrected ? 1 : 0;
                }
            }
        }
    }
    for (int d = 0; d < 2; d++)
    {
        for (int b = 0; b < 2; b++)
        {
            const Tally &x = t[d][b];
            std::cout << "I1875 REPLAY 2026-10-" << (d == 0 ? "15" : "16") << " basis="
                      << (b == 0 ? "truth-line" : "v0.1.16") << " lone-hits=" << x.lone
                      << " flagged-before=" << x.before << " flags-added=" << x.added
                      << " corrected-newly-caught=" << x.caught << " of-which-alternative-is-the-fix=" << x.oneTap
                      << std::endl;
        }
    }
    // The vote path's two-camera consensus in the same spans, which this rule never asks:
    // 37 darts on 2026-10-15, 28 on 2026-10-16 (agreeing >= 2, counted from the truth lines).
    std::cout << "I1875 CONSENSUS 2026-10-15=37 2026-10-16=28 untouched" << std::endl;

    // lone: every lone hit the truth line shows unflagged is flagged after, with its first candidate.
    for (const Row &r : kRows)
    {
        if (r.flagged)
        {
            continue;
        }
        const Published p = published(r, false);
        say(p.flagged && p.alternative == r.first,
            "lone: " + row(r) + " publishes flagged with " + (r.first[0] ? r.first : "nothing"));
    }
    // kept: what was flagged stays flagged with what it had; on v0.1.16, #1867's MISS stays.
    for (const Row &r : kRows)
    {
        if (r.flagged)
        {
            const Published p = published(r, false);
            say(p.flagged && p.alternative == r.alternative,
                "kept: " + row(r) + " keeps its flag with " + r.alternative);
        }
        if (r.rim_contradicts)
        {
            const Published p = published(r, true);
            say(p.flagged && p.alternative == "MISS", "kept: " + row(r) + " keeps #1867's MISS on v0.1.16");
        }
    }

    // named: the issue's two, and #1822's.
    for (const Row &r : kRows)
    {
        const std::string ref = r.reference;
        if (ref == "01M52GGA8RE8R1RGHTW2F6FP0Z" || ref == "01M52HDF91B8AK0AP5QVG4PBH6")
        {
            const Published a = published(r, false), b = published(r, true);
            const std::string want = ref == "01M52GGA8RE8R1RGHTW2F6FP0Z" ? "S8" : "S20";
            say(a.flagged && a.alternative == want && b.flagged && b.alternative == "MISS",
                "named: " + row(r) + " comes out flagged (with " + want + " on the truth line's build; #1867's MISS "
                "stands on v0.1.16)");
        }
    }
    {
        // #1822 (casual board 17, 2026-10-10): S3 for an S19, flagged T3 (ring) -- and S3 for a T3,
        // unflagged; both candidates T3,S19,S17.
        const Row a = {"10", 0, "01M4KJF3DRWCV51S05RR7K35FD", "S3", true, "T3", "T3", "S19", false};
        const Row b = {"10", 0, "01M4KN6RGE535FXAG290F9THMQ", "S3", false, "", "T3", "T3", false};
        const Published pa = published(a, false), pb = published(b, false);
        say(pa.flagged && pa.alternative == "T3",
            "named: #1822's 01M4KJF3DRWCV51S05RR7K35FD S3 (thrown S19) keeps RING-WIRE's T3 -- not caught better");
        say(pb.flagged && pb.alternative == "T3",
            "named: #1822's 01M4KN6RGE535FXAG290F9THMQ S3 (thrown T3) publishes flagged with T3, the fix");
    }

    // count: the replay's figures, as the header comment states them.
    say(t[0][0].added == 34 && t[0][0].caught == 3, "count: 2026-10-15 truth-line basis adds 34 flags, 3 corrected");
    say(t[1][0].added == 19 && t[1][0].caught == 2, "count: 2026-10-16 truth-line basis adds 19 flags, 2 corrected");
    say(t[0][1].added == 31 && t[0][1].caught == 2, "count: 2026-10-15 v0.1.16 basis adds 31 flags, 2 corrected");
    say(t[1][1].added == 14 && t[1][1].caught == 0, "count: 2026-10-16 v0.1.16 basis adds 14 flags, 0 corrected");

    // pure: what the rule must not reach, and its sentence.
    say(!flagAnUncheckedLoneReading(false, false, 0, "S20", {"S1"}, 0.9f).flag,
        "pure: a consensus or a wedge-by-default (lone=false) is not asked");
    say(!flagAnUncheckedLoneReading(true, false, 0, "MISS", {"D20"}, 0.7f).flag,
        "pure: a lone MISS is not flagged here");
    say(!flagAnUncheckedLoneReading(true, true, 0, "S20", {"T20"}, 0.7f).flag,
        "pure: an already-flagged reading is left to the rule that flagged it");
    {
        const LoneUnchecked u = flagAnUncheckedLoneReading(true, false, 2, "BULL", {}, 0.9f);
        say(!u.flag && u.alternative.empty() && u.account.find("nothing is flagged (#1556 rule 2)") != std::string::npos,
            "pure: a lone reading with no candidate is not flagged (#1556 rule 2: a flag is its alternative), and says so");
    }
    {
        const LoneUnchecked u = flagAnUncheckedLoneReading(true, false, 1, "S16", {"S16", "S8", "S7"}, 0.7f);
        say(u.alternative == "S8", "pure: the alternative is never the published score");
        std::cout << "I1875 SENTENCE " << u.account << std::endl;
        say(u.account.rfind("LONE-UNCHECKED: camera 1's S16 ", 0) == 0 &&
                u.account.find("at 0.7 (turnaus#1875)") != std::string::npos,
            "pure: the account names the camera, the reading, the alternative and the confidence");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

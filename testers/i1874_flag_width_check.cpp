// turnaus#1874: the geometric flag's width, replayed over device/2's truth lines.
//
// On 2026-10-13, -15 and -16 every geometric dart within ONE sigma of its nearest wire was
// flagged, because those boards ran v0.1.15, whose unset OD_FLAG_SIGMAS is #1556's 1.0 (the
// 0.7 default is v0.1.16's; docs/rig.md records the evidence). This replays the flagged
// geometric darts of each day's corrected span through the rule as it now stands --
// `tightenBoundaryCall(decideBoundaryCall(...), z, flagSigmasFrom(nullptr))`, z the truth
// line's margin_mm / sigma_mm, which is the solve's crossingSigmas to the truth line's two
// decimals -- and reports flags at 1.0 (the boards), at 0.7 (v0.1.16) and at the tree's
// default, and every corrected dart that would lose its flag.
//
// THE SPANS: 2026-10-13 darts 6-90, 2026-10-15 darts 2519-3004, 2026-10-16 darts 8-471 (first
// to last dart a marker corrected to a cell or typed; i1875's spans for the last two). An
// uncorrected dart inside a span is PROBABLY right, not certainly, so a flag cleared on one
// counts as a flag removed, and the "lost" figure is an upper estimate. The sessions are kept
// off GitHub; the 409 rows were cut from them by a hand-run script and are the only part of
// them here. `turn` and `removed` corrections name no dart (turnaus#1819): counted apart.
//
//   g++ -std=c++17 -I src -I src/utils -o width testers/i1874_flag_width_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// PREFIXES, which i1874_check.sh counts:
//   count:    the per-day flags at the tree's default and the corrected darts that lose
//             their flag, as numbers.
//   lost:     the one corrected dart the default clears: 2026-10-13 dart 43, S12 typed `None`
//             at z 0.68 -- its offered S5 could not have fixed it.
//   kept:     every other corrected dart keeps its flag and its alternative at the default.
//   edge:     #1815's two true flags between 0.5 and 0.6 -- board 20's S20 typed `None` at
//             0.524 and rig-20260929 dev v9.3's S19 for a thrown T19 at 0.569, T19 offered --
//             kept at the default. They are why the default is not the issue's 0.5.
//   cleared:  each dart that stood, between the default and 0.7, published clear.
//   account:  a cleared dart's UNCERTAINTY sentence names the threshold that cleared it.
//   fixed:    figures at a stated K (1.0, 0.7), which no default reaches.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

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
    const char *day; // 2026-10-<day>
    int dart;        // the dart's number in the day's truth line
    const char *reference;
    const char *published;
    const char *alternative;
    const char *kind;
    double marginMm;
    double sigmaMm;
    const char *corrected; // "" where it stood
    const char *picked;    // alternative / candidate / typed / turn / removed; "" where it stood
};

// Every geometric dart inside the span the truth line shows flagged (all z <= 1.0).
static const Row kRows[] = {
    {"13", 9, "01M4TRJ21VHXYYC9KNYPRFH4YG", "S7", "S16", "wedge", 2.46, 5, "", ""},
    {"13", 11, "01M4TRJEY7KY0FV1T70SMYQKT8", "S3", "T3", "ring", 3.8, 5.08, "", ""},
    {"13", 15, "01M4TRKAAW04SWBK9K0TF5X138", "S17", "S3", "wedge", 4.19, 5, "", ""},
    {"13", 17, "01M4TRKE341TD1W84DSB71SG25", "S6", "S10", "wedge", 1.81, 5, "", ""},
    {"13", 18, "01M4TRKQQZHWF70TASW72M5NY3", "S11", "S8", "wedge", 5.29, 5.34, "", ""},
    {"13", 20, "01M4TRKV6W36ACAFCS8JR9MQ7Y", "T16", "S16", "ring", 0.3, 5, "S16", "alternative"},
    {"13", 22, "01M4TRMDG31H6QZCDZDHX5RCRR", "S6", "S13", "wedge", 1.01, 5.9, "S13", "alternative"},
    {"13", 25, "01M4TRN7C9YK0M0RS299WSRBM1", "S10", "S15", "wedge", 7.41, 11.62, "", ""},
    {"13", 27, "01M4TRNJ2D25KKPFJG19P2YHV4", "T7", "T19", "wedge", 3.31, 5, "", ""},
    {"13", 30, "01M4TRNZ0YRA11S7YVFMWJ75FP", "S19", "T19", "ring", 2.96, 5, "", ""},
    {"13", 31, "01M4TRP0NNEQNX4H9DAX2XZ9V0", "T19", "S19", "ring", 4.43, 8.7, "", ""},
    {"13", 34, "01M4TRPDXJFZDNTBDPGYE7JS1C", "S17", "T17", "ring", 5.86, 7.07, "", ""},
    {"13", 35, "01M4TRPTVFZPBSCB3CFZY8AB6X", "S9", "S12", "wedge", 2.64, 5, "", ""},
    {"13", 37, "01M4TRPZDXE61TAJHV38SSHKQP", "S2", "T2", "ring", 4.08, 9.76, "", ""},
    {"13", 38, "01M4TRQ84ETKKTTT937XXP4C9X", "S2", "T2", "ring", 7.12, 9.26, "", ""},
    {"13", 39, "01M4TRQAFT7MHHM01F52S1Y5P1", "S16", "S8", "wedge", 6.92, 8.31, "", ""},
    {"13", 40, "01M4TRQFC2B52A1E0YV04R6N73", "S3", "S19", "wedge", 0.71, 5, "", ""},
    {"13", 43, "01M4TRRGGTCXEHZG0C9HKDM52F", "S12", "S5", "wedge", 4.46, 6.55, "None", "typed"},
    {"13", 46, "01M4TRVPQ0HP2P9FBZ7K1DAEFD", "S20", "S1", "wedge", 2.24, 5, "", ""},
    {"13", 48, "01M4TRWQF99WAFPW0ATW76CJ9J", "S11", "S14", "wedge", 0.83, 5.56, "", ""},
    {"13", 49, "01M4TRWT41HKME9228PH5RRK9F", "25", "S1", "ring", 4.36, 5.17, "", ""},
    {"13", 50, "01M4TRWWRHE27KPY17QX4XZAT5", "S3", "S17", "wedge", 0.3, 5, "", ""},
    {"13", 51, "01M4TRX7JW7EXM427G56SBBGBR", "S14", "S9", "wedge", 1.61, 5.68, "", ""},
    {"13", 52, "01M4TRXA4NC3BVEK8TQ14GN4WN", "25", "Bull", "ring", 2.38, 5.47, "", ""},
    {"13", 53, "01M4TRXCW9P4F071XJN6SZH6HG", "25", "Bull", "ring", 3.89, 6.11, "", ""},
    {"13", 60, "01M4TRYWJHDYQGPN7CF8Q91FX4", "S5", "S12", "wedge", 3.57, 5, "", ""},
    {"13", 62, "01M4TRZ2GJEY2RS8P698GQ8J9K", "T20", "S20", "ring", 1.79, 5.5, "", ""},
    {"13", 64, "01M4TRZGVY37FYNGM075ETKRVQ", "S5", "T5", "ring", 1.04, 5.31, "", ""},
    {"13", 66, "01M4TRZXXZ4SW32M3ZWJ158XMK", "S3", "S19", "wedge", 4.49, 5, "", ""},
    {"13", 69, "01M4TS0DRBVMS9RZDJT1GDWP0Z", "S7", "S19", "wedge", 1.07, 5, "", ""},
    {"13", 70, "01M4TS0GGDNPJX0MEEFTACTR81", "T7", "S7", "ring", 1.46, 5.27, "", ""},
    {"13", 71, "01M4TS0JYHAK2QJNHGD3YZRMFE", "S7", "T7", "ring", 4.94, 5.85, "", ""},
    {"13", 74, "01M4TS15YJYMND46EH8GZ3CRR7", "S20", "S1", "wedge", 1.41, 5, "", ""},
    {"13", 75, "01M4TS1XWSCTCCDET99P6H9382", "S5", "S20", "wedge", 1.83, 5, "", ""},
    {"13", 79, "01M4TS2EE9N09W9W467P6ECAN2", "S5", "T5", "ring", 1.35, 5.98, "", ""},
    {"13", 89, "01M4TS5DW76BFHEPXEA32HNHHV", "S19", "S7", "wedge", 4.79, 5, "", ""},
    {"13", 90, "01M4TS5SJK14HA3CQHQ7BQQ1M7", "T1", "S1", "ring", 0.67, 5.11, "S1", "alternative"},
    {"15", 2523, "01M50EVAQNSNC36DQTE4KCN15V", "S20", "S1", "wedge", 4.64, 5, "", ""},
    {"15", 2524, "01M50EVCTW0HRX24EH6BGZG6EJ", "S4", "S13", "wedge", 3.07, 5.22, "", ""},
    {"15", 2525, "01M50EVEJYVZDP223V91DK199K", "T18", "S18", "ring", 4.52, 5, "", ""},
    {"15", 2527, "01M50EVT2KQRCKBW9AV9FX068M", "S1", "T1", "ring", 3.26, 5, "", ""},
    {"15", 2528, "01M50EVW0695JJ35TB4EKY4FME", "S14", "S9", "wedge", 0.77, 6.15, "", ""},
    {"15", 2529, "01M50EW59KB34BSTG92G1J4450", "S17", "S3", "wedge", 3.13, 5, "", ""},
    {"15", 2532, "01M50EWH923EE06S950BYTCC22", "S19", "T19", "ring", 1.55, 5.39, "", ""},
    {"15", 2533, "01M50EWKCC97RNVH7DG6VYFG4H", "S19", "S3", "wedge", 3.51, 5, "", ""},
    {"15", 2535, "01M50EWXT3KAZPJEQ474Y2QKDA", "T19", "S19", "ring", 3.71, 5.16, "", ""},
    {"15", 2536, "01M50EWZQ6BJA8E46BD1ES1EZD", "S17", "S3", "wedge", 3.13, 5, "", ""},
    {"15", 2537, "01M50EX1FES0AWVPN374PDQ652", "S17", "S3", "wedge", 2.56, 5, "", ""},
    {"15", 2539, "01M50EXCD563T2HJ83FJK6K17M", "D19", "S19", "ring", 2.86, 6.29, "", ""},
    {"15", 2541, "01M50EXR1NXYB6SMNMY3ERT8GW", "S20", "S1", "wedge", 1.82, 6.27, "", ""},
    {"15", 2542, "01M50EXTKDS2XZWH3T5GFMQ8VD", "T3", "T19", "wedge", 6.94, 8.32, "", ""},
    {"15", 2543, "01M50EYCRJKZCNP16R5SXV2X24", "S8", "S11", "wedge", 3.2, 5, "", ""},
    {"15", 2557, "01M50F27D4V8CZ2WB2ESMYEABM", "S1", "T1", "ring", 4.27, 5.75, "", ""},
    {"15", 2576, "01M50F6Z29KGX2XWY86YMD9GE1", "S15", "S2", "wedge", 0.88, 5, "", ""},
    {"15", 2583, "01M50F8A1KT1VVV2B4Z4219CS4", "D15", "S15", "ring", 2.2, 7.54, "", ""},
    {"15", 2588, "01M50F920B1K6YDXM3CXS84JA9", "T18", "T4", "wedge", 1.99, 5, "", ""},
    {"15", 2590, "01M50FANZ6Q1DFRHZS1RTCD7XH", "S3", "S19", "wedge", 1.42, 5, "", ""},
    {"15", 2592, "01M50FASRECJTBPKA4MV9093TW", "S7", "S19", "wedge", 0.45, 6.5, "S19", "alternative"},
    {"15", 2593, "01M50FBA6WBFR8ZJMYCQ8Z34F0", "S7", "T7", "ring", 2.73, 8.9, "", ""},
    {"15", 2594, "01M50FBBZT14B0ZJZZ2CKNXWAZ", "S17", "S2", "wedge", 4.61, 5, "", ""},
    {"15", 2595, "01M50FBE2FNSMTEQ8RTTFD47TW", "S7", "T7", "ring", 5.31, 5.56, "", ""},
    {"15", 2596, "01M50FBQT8TZY6DP1W84EDNA5Z", "S1", "D20", "wedge", 5.72, 11.58, "", ""},
    {"15", 2597, "01M50FBT1SMV1Z0VBS57ATRQCS", "S9", "S12", "wedge", 1.53, 5.14, "", ""},
    {"15", 2599, "01M50FC5AJMHQ6Y2VDKN1Y4BFV", "S1", "T1", "ring", 2.48, 5, "", ""},
    {"15", 2600, "01M50FC7CP6VXPJRJE1JXARRKJ", "S1", "S20", "wedge", 3.93, 5, "", ""},
    {"15", 2601, "01M50FC93VWGH6BC3S2SY3SBAB", "T20", "S20", "ring", 0.01, 5.54, "S20", "alternative"},
    {"15", 2604, "01M50FCZ3Z9124442VCAVMET6A", "S5", "S20", "wedge", 1.54, 5, "", ""},
    {"15", 2605, "01M50FD9AMS0VK7E060H5GPK6B", "S18", "T18", "ring", 4.15, 5.24, "", ""},
    {"15", 2606, "01M50FDBCMPQ41Y9FGQVPZ5T0X", "S20", "T20", "ring", 1.6, 5.9, "", ""},
    {"15", 2608, "01M50FDPC6JRT7W8M1H88CMEFV", "T20", "S20", "ring", 4.52, 5.7, "", ""},
    {"15", 2609, "01M50FDRGG875EM2ZF3EN13VBY", "S5", "S12", "wedge", 4.99, 6.99, "", ""},
    {"15", 2617, "01M50FEYZ03FPM1YZT1Z615DN8", "S20", "T20", "ring", 4.01, 6.4, "", ""},
    {"15", 2618, "01M50FF0VT3ZDM6MH04GM7DF4A", "S18", "T18", "ring", 1.77, 5, "", ""},
    {"15", 2620, "01M50FFCD4ZEG0AY7BNF95E05B", "T20", "S20", "ring", 5.77, 6.01, "", ""},
    {"15", 2635, "01M50FKCC6S8PM22A7Y84FMAXY", "S13", "S4", "wedge", 4.13, 6.92, "", ""},
    {"15", 2638, "01M50FM2WFJT2D5Z8R825RGAPQ", "S8", "S16", "wedge", 0.83, 5, "", "turn"},
    {"15", 2643, "01M50FPYRQCBP38BT3YMMHZ2BA", "S8", "T8", "ring", 2.98, 5, "", ""},
    {"15", 2649, "01M50FWP6SXT3TCR20GAYMCSP8", "S3", "T3", "ring", 4.89, 5.65, "", ""},
    {"15", 2651, "01M50FWT4AGS08ZF5K2NWQ0NQA", "S7", "S16", "wedge", 0.36, 6.25, "", ""},
    {"15", 2653, "01M50FX5VA4BRJD7J29HWZD2YA", "S18", "S4", "wedge", 0.42, 5.82, "", ""},
    {"15", 2655, "01M50FXG81JSZFM8EZ5NW7CSSA", "S1", "T1", "ring", 1.92, 5.18, "", ""},
    {"15", 2656, "01M50FXJDESPAFSMV3M39J2HXF", "T1", "T20", "wedge", 1.51, 5, "T20", "alternative"},
    {"15", 2657, "01M50FXM7HMV4DVK0SGZ5MSXM9", "S18", "S1", "wedge", 1.86, 5.53, "S1", "alternative"},
    {"15", 2658, "01M50FYC1MBC44YZGVHM4BZ6R4", "S1", "S20", "wedge", 0.99, 5, "S20", "alternative"},
    {"15", 2661, "01M50FYX3BS8AGGASK5Z4Z9KKR", "T18", "S18", "ring", 1.12, 5.14, "", "turn"},
    {"15", 2662, "01M50FYZERN6GAERSK05H6CZJP", "S20", "T20", "ring", 0.56, 5.78, "", "turn"},
    {"15", 2664, "01M50FZX148ZB2BHER4JZA9RC5", "S20", "S1", "wedge", 0.17, 5, "", ""},
    {"15", 2666, "01M50G01E9WDYB7C2HWWZR52WZ", "S5", "S20", "wedge", 2.28, 5, "", ""},
    {"15", 2667, "01M50G0M5PS33HAD4C3J779152", "T20", "T1", "wedge", 0.86, 5, "", ""},
    {"15", 2669, "01M50G0RBEBHG8M8XYDSRN9D5D", "T5", "S5", "ring", 5.39, 5.74, "", ""},
    {"15", 2671, "01M50G1652KZEFK2HMZ3TFYKM6", "S1", "S20", "wedge", 3.82, 5, "", "turn"},
    {"15", 2672, "01M50G187XRYD40WS23E08MAPD", "T20", "S20", "ring", 0.09, 5.73, "", "turn"},
    {"15", 2673, "01M50G2JHAVNCJAN7YSQECHPTW", "S20", "T20", "ring", 3.86, 6.23, "", ""},
    {"15", 2678, "01M50G37GMEKKCDBE6YQFSNSVJ", "S2", "S17", "wedge", 4.76, 5, "", ""},
    {"15", 2685, "01M50G5A3PEPM675Y245C0FD07", "S1", "S18", "wedge", 2.24, 5, "", ""},
    {"15", 2698, "01M50G747Q0RNJEAYC1R1NFC0P", "S1", "S20", "wedge", 3.67, 5, "", ""},
    {"15", 2701, "01M50G7JD0RJ3TKKXBV9RTGEST", "S1", "S18", "wedge", 2.59, 21.59, "", "turn"},
    {"15", 2702, "01M50G83K9E5SWRNACQ3XC3KGP", "T20", "S20", "ring", 3.27, 5.9, "", ""},
    {"15", 2704, "01M50G87KZC3V6YGZ40TSA1PE7", "S1", "T1", "ring", 0.99, 5.52, "", ""},
    {"15", 2706, "01M50G8KE298MMVZE78CBFK8AV", "S20", "T20", "ring", 4.39, 5.69, "", "turn"},
    {"15", 2710, "01M50G9GYRKM0M77GFFNHVG0FH", "S20", "S1", "wedge", 4.25, 5, "", ""},
    {"15", 2712, "01M50GC9S4JN2GG37RPZAZHGVC", "S20", "S1", "wedge", 2.88, 5, "", ""},
    {"15", 2715, "01M50GCXPM8Z81Q2XQH3F0JCY2", "S5", "T5", "ring", 2.48, 5.08, "", ""},
    {"15", 2717, "01M50GD8JZEFK6PY20WF12KXEG", "S1", "S18", "wedge", 0.82, 5, "", ""},
    {"15", 2719, "01M50GDCHFTWRKQ57W3Q6W0EV6", "T20", "S20", "ring", 3.6, 5.34, "", ""},
    {"15", 2720, "01M50GDPBG45KY3AEPQJ2Q21QQ", "T10", "T15", "wedge", 0.71, 5, "", ""},
    {"15", 2721, "01M50GDTMJ617HXQN191A31GM8", "S3", "S19", "wedge", 0.08, 5, "", ""},
    {"15", 2734, "01M50GH1XD1438ESD9SAXV0RQF", "S18", "S4", "wedge", 11.42, 25.31, "", ""},
    {"15", 2736, "01M50GHJE4JDN7N0RDCYF1MXDJ", "S1", "S20", "wedge", 4.03, 5, "", ""},
    {"15", 2739, "01M50GJJZ1HWDMNXXZBQZ8082K", "S20", "S1", "wedge", 4.61, 5, "", ""},
    {"15", 2741, "01M50GJXZM95ZCWX86XT4Q64HR", "S9", "T9", "ring", 2.46, 5, "", ""},
    {"15", 2742, "01M50GK1KNSP8GJGFY914ZP5MP", "T20", "S20", "ring", 1.54, 5.73, "S20", "alternative"},
    {"15", 2743, "01M50GK3HCQJYH829Y5BE1P9W4", "S20", "T20", "ring", 0.16, 5.76, "", ""},
    {"15", 2744, "01M50GKJM0JDDCCB2JE7QASFFD", "S1", "S18", "wedge", 0.89, 5, "", ""},
    {"15", 2745, "01M50GKMRAZVNP7JMCTP9XJGX9", "S1", "T1", "ring", 0.72, 5, "", ""},
    {"15", 2747, "01M50GKZPNKX6CEJ9QDTBCANQK", "T1", "S1", "ring", 1, 5.37, "S1", "alternative"},
    {"15", 2751, "01M50GMW19N6Z3TR1HJ78494TE", "S20", "S1", "wedge", 0.92, 5, "", ""},
    {"15", 2756, "01M50GNPVGV1MFVVPYY4SN7ZFD", "T3", "S3", "ring", 0.51, 5.23, "S3", "alternative"},
    {"15", 2758, "01M50GP54CH2171EZV9EVN2BE7", "T4", "T18", "wedge", 1.14, 6.06, "T18", "alternative"},
    {"15", 2763, "01M50GPV0Q32KZPSQFH1Z3FZRF", "T5", "S5", "ring", 1.26, 6.14, "S5", "alternative"},
    {"15", 2765, "01M50GQAEBW8ZJPY5SRZYETPFR", "S20", "D20", "ring", 3.3, 10.58, "", ""},
    {"15", 2766, "01M50GQC9VP7AW5F5HXTJXYMV5", "S12", "S5", "wedge", 4.36, 5.32, "", ""},
    {"15", 2767, "01M50GQN5GBABEKR4XC13H3CTM", "S20", "T20", "ring", 2.88, 5.78, "", ""},
    {"15", 2770, "01M50GR575SASNG6S4BCPNZCWT", "S19", "S3", "wedge", 1.8, 5.11, "", ""},
    {"15", 2784, "01M50GV7E01GDG3W8H7GZNK9JP", "T18", "T1", "wedge", 1.97, 5.14, "T1", "alternative"},
    {"15", 2786, "01M50GWDVHM9S954JKS00X274B", "T1", "S1", "ring", 0.55, 5, "S1", "alternative"},
    {"15", 2794, "01M50J5ATN4DCBCTWSW4N8T9ED", "S14", "T14", "ring", 1.55, 5, "", ""},
    {"15", 2795, "01M50J5CD3FRMWWJZ63TW38RNK", "S3", "S19", "wedge", 2.79, 5.11, "", ""},
    {"15", 2796, "01M50J5E9EKZR01GN5FJDY9VE2", "S1", "S18", "wedge", 0.42, 5.63, "", ""},
    {"15", 2800, "01M50J6P3YE96MBKECV53RN438", "S4", "S13", "wedge", 4.75, 5, "", ""},
    {"15", 2801, "01M50J6QYRS49J1ABCHWKR9DBQ", "S9", "T9", "ring", 1.5, 5, "", ""},
    {"15", 2802, "01M50J6SWAD5F7VMY2VAV434DG", "T1", "T20", "wedge", 2.72, 5.68, "T20", "alternative"},
    {"15", 2804, "01M50J7JKQ1DF8AXEMXWMYT9SC", "S19", "T19", "ring", 0.82, 8.62, "T19", "alternative"},
    {"15", 2807, "01M50J8EVT2ZVEM0592PB5PPMB", "S12", "S5", "wedge", 3.23, 5.72, "", ""},
    {"15", 2809, "01M50J95H7N99K5JWSC5JED6A0", "T18", "S18", "ring", 5.56, 6.05, "", ""},
    {"15", 2812, "01M50J9TP6FKZ8MM4MSKMKK0HJ", "S6", "S13", "wedge", 5.15, 5.82, "", ""},
    {"15", 2818, "01M50JC928KKW6G2NXMVZX80SK", "S7", "S16", "wedge", 0.71, 5.21, "", ""},
    {"15", 2825, "01M50JE1F1JW533QNNNHYFJT31", "25", "S4", "ring", 1.93, 5, "", ""},
    {"15", 2829, "01M50JF3J3FV5939H7ZVECECP1", "S5", "S20", "wedge", 2.34, 5, "", "turn"},
    {"15", 2834, "01M50JJDMBNAYMJD24QPXGAW5C", "S3", "S17", "wedge", 1.58, 16.13, "", ""},
    {"15", 2835, "01M50JJWYK5436228PRHPD5PDB", "T9", "S9", "ring", 3.13, 5, "", ""},
    {"15", 2849, "01M50JQPSR70TXYT5BWKBP6AJE", "S8", "D8", "ring", 4.48, 8.52, "", ""},
    {"15", 2853, "01M50JSEWH6NBH3JSK550JXKS8", "T9", "S9", "ring", 5.47, 5.7, "", ""},
    {"15", 2854, "01M50JSH3DB7ST91BDGTV7J3CW", "S12", "T12", "ring", 3.1, 5.83, "", ""},
    {"15", 2857, "01M50JT3YE90KGMDC1AECXV65Q", "S4", "S13", "wedge", 1.37, 6.43, "", ""},
    {"15", 2859, "01M50JVFN8MA2KMZTZJY7BDZAK", "S12", "T12", "ring", 0.79, 5, "", ""},
    {"15", 2860, "01M50JVHJPFEZKRR0C9XP385R5", "S1", "S20", "wedge", 3.63, 5, "", ""},
    {"15", 2861, "01M50JW2CQ76JXPQAQ1VKTXJJ4", "S3", "S19", "wedge", 3.09, 5, "", ""},
    {"15", 2866, "01M50JWKCX79Y91KMYFHKD2VG5", "T5", "S5", "ring", 2.98, 5.24, "", ""},
    {"15", 2867, "01M50JX0NEDJK9A3BHYHAF6TVB", "T18", "S18", "ring", 3.33, 5, "", ""},
    {"15", 2868, "01M50JX2XWEJVFKEJK3MW0Q02E", "S20", "S5", "wedge", 2.3, 5, "", ""},
    {"15", 2869, "01M50JX50JYP95VA7SBN9214Z3", "S4", "T4", "ring", 0.24, 5.19, "", ""},
    {"15", 2870, "01M50JXXWNM72YQNTMERC86J87", "S5", "S20", "wedge", 3.93, 5, "", ""},
    {"15", 2871, "01M50JXZYT70CNEE360VYWP9FK", "S20", "S5", "wedge", 5.7, 5.73, "", ""},
    {"15", 2872, "01M50JY20FP11PSVVPTTBSQRM7", "T20", "S20", "ring", 1.73, 5.81, "S20", "alternative"},
    {"15", 2873, "01M50JYPYGGYEB429CNC2FWAPA", "S1", "T1", "ring", 0.12, 5.5, "", ""},
    {"15", 2875, "01M50JYTY6XAV1JNAGYYG1FSYG", "S1", "S20", "wedge", 3.41, 5, "", ""},
    {"15", 2876, "01M50JZ5H7JJ8628DJWMX9FAMR", "S5", "T5", "ring", 2.55, 5.62, "", ""},
    {"15", 2878, "01M50JZ938XBA355VES020N5A3", "25", "S2", "ring", 2.25, 5.34, "", ""},
    {"15", 2879, "01M50JZV0QTW3EF813FEZZET8T", "T20", "S20", "ring", 2.68, 5.65, "", ""},
    {"15", 2880, "01M50JZX6ZYKB5NN8RRK0JQWHD", "S20", "S1", "wedge", 0.87, 5, "", ""},
    {"15", 2882, "01M50K0HZ3M7GYR7GYSXM1H268", "S4", "S18", "wedge", 0.17, 5.36, "S18", "alternative"},
    {"15", 2884, "01M50K0NWG09T58GTSFBYF8KXB", "S5", "S20", "wedge", 2.95, 5.52, "", ""},
    {"15", 2888, "01M50K2KXEPKCR1E1S7JEJPDBB", "S11", "S8", "wedge", 0.9, 12.43, "", ""},
    {"15", 2889, "01M50K2Q1K4F3664Y63JZ047B3", "S7", "S19", "wedge", 5.65, 12.68, "", ""},
    {"15", 2893, "01M50K3QADT1R9DCTDJ079MKWP", "S2", "T2", "ring", 4.85, 5.39, "", ""},
    {"15", 2894, "01M50K3VQJHGAP6GHXHZT5GQDD", "S13", "S6", "wedge", 1.59, 5.8, "", ""},
    {"15", 2900, "01M50K60YF0RB3WMP90MC9SEKF", "S4", "S18", "wedge", 3.63, 5, "", ""},
    {"15", 2903, "01M50K6MRY7ACZB7J6E2NXVE12", "S4", "T4", "ring", 0.85, 5, "", ""},
    {"15", 2904, "01M50K6PWYFR9RYF9069MW5D7K", "S1", "S20", "wedge", 2.81, 5, "", ""},
    {"15", 2905, "01M50K6SNHVMD4N9BNA68DSZXJ", "S1", "S20", "wedge", 0.82, 6.4, "S20", "alternative"},
    {"15", 2907, "01M50K7QNVKAMBX6TSN39PDZWH", "S17", "S2", "wedge", 2.01, 5, "", ""},
    {"15", 2909, "01M50K8BS144TNP571YA6HJ80B", "S1", "S18", "wedge", 0.92, 5.17, "", ""},
    {"15", 2910, "01M50K8DJVYCJEGBM58D5ZWDBD", "S7", "S19", "wedge", 3.69, 5, "", ""},
    {"15", 2911, "01M50K8FC08XAW62M422PWEH9C", "S20", "T20", "ring", 2.2, 5.66, "", ""},
    {"15", 2915, "01M50K9KCKNNE4FJ3XE2T07JXR", "S5", "S12", "wedge", 2.45, 5, "", ""},
    {"15", 2917, "01M50K9QDA8HY2V86AVGDNW6C1", "S5", "T5", "ring", 4.46, 5, "", ""},
    {"15", 2922, "01M50KB1GZB0B84PTDJ7915768", "S4", "S18", "wedge", 3.91, 5, "", ""},
    {"15", 2924, "01M50KBR176KR9SK44W17A4HE2", "T20", "S20", "ring", 4.07, 5.86, "", ""},
    {"15", 2928, "01M50KCAH67AKTXFZYV0C24R7D", "S5", "S20", "wedge", 1.59, 5, "", ""},
    {"15", 2934, "01M50KDGBJ0PB8Q0X13NXGT9VM", "S20", "S5", "wedge", 3.48, 5, "", ""},
    {"15", 2935, "01M50KDHZN0CP5Q6QEE49JHK01", "S1", "T1", "ring", 0.48, 5.51, "", ""},
    {"15", 2942, "01M50KG4GC8T25W5V1YAP64MDD", "S2", "D2", "ring", 5.3, 12.73, "", ""},
    {"15", 2943, "01M50KG708XRKRTD5ZJPQMCMPG", "S8", "S11", "wedge", 3.52, 5, "", ""},
    {"15", 2944, "01M50KGEFRW344903NJ8CYY0K7", "S6", "S10", "wedge", 0.1, 5, "", ""},
    {"15", 2945, "01M50KGYADXCB9J1P2HQWCPWGR", "T1", "S1", "ring", 2.42, 5.03, "", "turn"},
    {"15", 2948, "01M50KJ7QBT5GQXTEBBB4WZQ5C", "T5", "S5", "ring", 3.97, 5.25, "", ""},
    {"15", 2950, "01M50KJRYB0DH3V19JX0C3B244", "S5", "S12", "wedge", 3.75, 5.17, "", ""},
    {"15", 2951, "01M50KJTQ3C6NAVK1WMH9AAWQX", "S1", "S18", "wedge", 2.56, 9.23, "S18", "alternative"},
    {"15", 2953, "01M50KKADK9N88V4RZ02T75G1T", "S10", "T10", "ring", 3.09, 5, "", ""},
    {"15", 2956, "01M50KKW25QDRHMSMQ4GGD74A3", "T20", "S20", "ring", 4.24, 6.05, "", ""},
    {"15", 2957, "01M50KKXXZR6HPHA8YMZKB5FWE", "S20", "S1", "wedge", 3.74, 5.37, "", ""},
    {"15", 2961, "01M50KMYBXJZ9C3QJZDEGQHA9T", "S1", "T1", "ring", 0.04, 5.94, "", ""},
    {"15", 2962, "01M50KN0AZPTEZJ8JDZPG7RWWB", "S18", "S4", "wedge", 3.16, 6.38, "", ""},
    {"15", 2966, "01M50KNXHT0K9J0WJ2D2GT1CTK", "T3", "T17", "wedge", 2.42, 5, "", ""},
    {"15", 2970, "01M50KPFC27EG2MV9TBTYJ1HTK", "S11", "S8", "wedge", 0.65, 5, "", ""},
    {"15", 2971, "01M50KPH25AKKPQN3R48FQFDF1", "S18", "T18", "ring", 3.84, 5.2, "", ""},
    {"15", 2975, "01M50KQVHVKDCZW3RPDC3S2FSN", "S1", "S18", "wedge", 2.14, 5, "", ""},
    {"15", 2976, "01M50KQX6PWVB9JTBANSP0NAA3", "S1", "S20", "wedge", 2.08, 6.52, "S20", "alternative"},
    {"15", 2977, "01M50KQZKPBE2BFEGMBCCVMFAC", "T12", "S12", "ring", 0.36, 5, "", ""},
    {"15", 2978, "01M50KRHJ2M4A9ABZMTS8P1KNQ", "S18", "S4", "wedge", 1.52, 5.63, "", ""},
    {"15", 2979, "01M50KRKR9P89NY5PMP2SZAJJV", "S20", "S5", "wedge", 3.05, 5, "", ""},
    {"15", 2982, "01M50KS2KEWP8DQW6RWVESZ7AK", "S14", "S9", "wedge", 3.29, 6.06, "", ""},
    {"15", 2984, "01M50KSJKCSXN2QJ2SNSHBKYG4", "S15", "S10", "wedge", 0.86, 6.74, "", ""},
    {"15", 2985, "01M50KSRTNJ7X37J44NK0D4CZ0", "S12", "S5", "wedge", 1.89, 5, "", ""},
    {"15", 2986, "01M50KSVBTWZVQWKCXVDP82ST8", "S1", "S18", "wedge", 0.79, 16.53, "S18", "alternative"},
    {"15", 2987, "01M50KT9Z3VH6GXHQHXMJ0RYBR", "T18", "S18", "ring", 2.74, 5, "", ""},
    {"15", 2988, "01M50KTBJKR5QBYQKQT9EZM2HC", "S1", "S20", "wedge", 3.39, 5, "", ""},
    {"15", 2989, "01M50KTK6CM0PXBAV26JJK874C", "S14", "S9", "wedge", 2.11, 6.96, "", ""},
    {"15", 2991, "01M50KV4B6VQ7RJ406V9PX55XN", "S20", "S1", "wedge", 2.58, 5, "", ""},
    {"15", 2994, "01M50KVQX6ZTW108ZT61HG9T7N", "S18", "S4", "wedge", 2.45, 5.44, "", ""},
    {"15", 2999, "01M50KWWCC1X3MSTBP8ZWFTJ8J", "S1", "S18", "wedge", 0.36, 5.09, "", ""},
    {"15", 3004, "01M50KYW4PTX12G78XSAVRQ153", "S7", "S19", "wedge", 1.74, 5.83, "S19", "alternative"},
    {"16", 9, "01M50PJNZSYZCKGXNMHH46BAVE", "S20", "T20", "ring", 2.82, 6.37, "", ""},
    {"16", 10, "01M50PJRSZCKSNHJF01P4CRE1F", "S20", "T20", "ring", 0.11, 5.95, "", ""},
    {"16", 11, "01M50PJVGCT7BJQYEQS64TRMV0", "T19", "S19", "ring", 5.61, 5.73, "", ""},
    {"16", 13, "01M50PK5RDJ120AY4GGXBS1AW2", "T19", "S19", "ring", 0.73, 9.75, "", "turn"},
    {"16", 15, "01M50PNS3A93EQBRBMCB8NHFBP", "S20", "S5", "wedge", 1.24, 5, "", ""},
    {"16", 16, "01M50PNVS839CRJKBS9KBS7Z8N", "S20", "S5", "wedge", 0.74, 5, "", ""},
    {"16", 17, "01M50PNYBK3867KEH4S81HFX15", "S7", "S19", "wedge", 3.92, 5.67, "", ""},
    {"16", 18, "01M50PPMGBGS8WGK7K0V4E3C28", "T20", "S20", "ring", 4.34, 6.54, "", ""},
    {"16", 19, "01M50PPR2HMJ4JV84SD6TE01NT", "T20", "S20", "ring", 4.51, 5.99, "", ""},
    {"16", 22, "01M50PQ6MXSB50K8TXT8N3SWQ9", "S20", "T20", "ring", 4.29, 6.36, "", ""},
    {"16", 24, "01M50PQG9NBXB3DXMCJ2XDFZWK", "S1", "T1", "ring", 4.36, 5.95, "", ""},
    {"16", 25, "01M50PQJKCYY95BBKE5964D50X", "S1", "T1", "ring", 4.12, 5.87, "", ""},
    {"16", 28, "01M50PQXJ4ZVJDFYTQ180QTVGS", "T1", "T20", "wedge", 0.47, 5, "", ""},
    {"16", 29, "01M50PR07SE0JN8364NZRWN22G", "T3", "T17", "wedge", 1.46, 7.21, "", ""},
    {"16", 30, "01M50PR6PBVQ2E4MEKFPVHZ2R2", "S20", "T20", "ring", 2.29, 6.12, "", ""},
    {"16", 31, "01M50PR9CA355DW9XW7NFT132B", "T5", "S5", "ring", 5.44, 5.93, "", ""},
    {"16", 34, "01M50PRQKD37403HQB4BZDXY1X", "S20", "T20", "ring", 4.28, 5.81, "", ""},
    {"16", 37, "01M50PS3AFHG6PT6WF7WSNR1GW", "S1", "T1", "ring", 2.36, 5.97, "", ""},
    {"16", 40, "01M50PSEAHWJ3513FE725B6TH0", "S19", "T19", "ring", 4.81, 6.39, "", ""},
    {"16", 41, "01M50PSGXQ0FVDJRSMWTKJAYWP", "S18", "T18", "ring", 3.24, 6.15, "", ""},
    {"16", 42, "01M50PSPPP6JN2JHQXXRZZH18J", "S5", "S20", "wedge", 0.9, 5, "", ""},
    {"16", 43, "01M50PSTEZE9N0FNVJB79YEFF6", "S15", "S10", "wedge", 3.53, 5, "", ""},
    {"16", 47, "01M50PT9MBJANCD63VZY7J6KK4", "25", "S13", "ring", 3.49, 5.92, "", ""},
    {"16", 52, "01M50PV0MPJ0MEDWEXJBB0G0DS", "S14", "S11", "wedge", 11.91, 19.73, "", ""},
    {"16", 54, "01M50PW0VD464D3Z03SQ5YGXT2", "T20", "S20", "ring", 4.79, 6.06, "", ""},
    {"16", 55, "01M50PW34X1W7YJ2K0HWDD3Z3T", "S1", "T1", "ring", 1.24, 6.48, "", ""},
    {"16", 56, "01M50PW791E55YQR59HGWME4AV", "S7", "S16", "wedge", 0.69, 5, "", ""},
    {"16", 57, "01M50PWRX3VQAACP7N0EB35S50", "S9", "S14", "wedge", 0.39, 6.63, "", ""},
    {"16", 60, "01M50PX45EMDZFS9W0HRDCRH41", "T20", "S20", "ring", 5.8, 5.97, "", ""},
    {"16", 61, "01M50PX6CQBX4N22EZ5QPCAFNS", "S1", "S18", "wedge", 3.89, 5, "", ""},
    {"16", 66, "01M50PXT2AVQPC1PGVAVPVEHCW", "S20", "S1", "wedge", 2.1, 5, "", ""},
    {"16", 67, "01M50PXW9PSCYWB8YG6170FXCM", "T20", "T1", "wedge", 2.24, 5, "", ""},
    {"16", 68, "01M50PXYGNS0276W5MTPVX3VB9", "T20", "S20", "ring", 4.9, 6.09, "", ""},
    {"16", 71, "01M50PYAGX6M93FTJ5HCQY4Z1B", "S20", "T20", "ring", 1.23, 6.17, "", ""},
    {"16", 76, "01M50PYW9JD5YSF47TNB5EXDDP", "T12", "S12", "ring", 5.52, 6.18, "", ""},
    {"16", 77, "01M50PYZ4BK479HKTH37SM5XSY", "S5", "T5", "ring", 3.14, 5.72, "", ""},
    {"16", 78, "01M50PZ4KRAWA4V4G89ASMK7HC", "S1", "T1", "ring", 5.71, 5.94, "", ""},
    {"16", 83, "01M50Q1D5CM5AK7E21NANDSE81", "T1", "S1", "ring", 3.8, 6.07, "", ""},
    {"16", 85, "01M50Q1NHFT2DEVQ596XZ0KQ75", "S1", "S18", "wedge", 2.32, 5, "", ""},
    {"16", 87, "01M50Q1TY1630EYGXVME5HJ8GB", "S1", "S20", "wedge", 0.19, 5, "", ""},
    {"16", 89, "01M50Q22HQ5E6XZQ48TBMAPWN9", "S20", "T20", "ring", 2.4, 6.12, "", ""},
    {"16", 94, "01M50Q2TP1SBF5R6RQQ033DFFC", "S6", "S10", "wedge", 2.88, 5, "", ""},
    {"16", 103, "01M50Q42Q4GR2EQ09497W8S2RP", "S1", "S20", "wedge", 2.26, 5, "", ""},
    {"16", 105, "01M50Q47Y86GC6SK0F0400SPZ3", "S19", "T19", "ring", 2.58, 5.78, "", ""},
    {"16", 114, "01M50Q754SDG77WM6YH99PT844", "T1", "S1", "ring", 4.39, 5.94, "", ""},
    {"16", 115, "01M50Q77NMPHGSP7SKFEHA4A7M", "T20", "S20", "ring", 4.97, 6.42, "", ""},
    {"16", 116, "01M50Q7A7YP83NY2C0WQS4E5H8", "S20", "T20", "ring", 1.99, 6.62, "", ""},
    {"16", 121, "01M50Q7Y99S7YPZJG9AD6GV39P", "S5", "S20", "wedge", 3.41, 5.11, "", ""},
    {"16", 123, "01M50Q86KESJV873N0BDRC5FZT", "S18", "S1", "wedge", 0.53, 5, "", ""},
    {"16", 124, "01M50Q88SYJ4Q34RRWX8MDJ9AP", "T3", "S3", "ring", 5.48, 5.8, "", ""},
    {"16", 125, "01M50Q8D4CAQZ2FBGRKG1N562P", "S7", "T7", "ring", 4.29, 16.54, "", ""},
    {"16", 126, "01M50Q8J66NDX5C6FRN82ZBYKJ", "S1", "T1", "ring", 4.05, 6.09, "", ""},
    {"16", 127, "01M50Q8MMT9VAZHF3NV8STHQSC", "S20", "T20", "ring", 6.26, 6.41, "", ""},
    {"16", 129, "01M50Q8WTSB825PPKG0PQ01F07", "S1", "S18", "wedge", 3.84, 5, "", ""},
    {"16", 130, "01M50Q8Z48ZRY95DJD4F1RCB7Q", "S20", "S1", "wedge", 1.13, 5, "", ""},
    {"16", 132, "01M50Q9AG56F05ERDKGVZXTEXC", "S1", "S20", "wedge", 1.13, 5, "", ""},
    {"16", 134, "01M50Q9FRNBMGJWHPNY9JYVHDE", "T20", "S20", "ring", 4.68, 6.58, "", ""},
    {"16", 137, "01M50Q9SK39Z618VW68WK3N2QA", "S3", "S17", "wedge", 0.6, 5, "", ""},
    {"16", 138, "01M50QA2TBFKDJ3XGEN4DXCHVA", "S20", "T20", "ring", 3.84, 6.72, "", ""},
    {"16", 139, "01M50QA5HCWK94NZSK31A8SHX4", "S19", "T19", "ring", 2.73, 5.91, "", ""},
    {"16", 140, "01M50QA83NA4QC27ZW27B7B9MC", "25", "Bull", "ring", 3.6, 7.37, "", ""},
    {"16", 142, "01M50QAFWPB2KXHNSZGT7GJ5AP", "T20", "S20", "ring", 5.26, 6.52, "", ""},
    {"16", 143, "01M50QAJ2HD0852DDGWHHQG292", "T20", "S20", "ring", 5.57, 6.13, "", ""},
    {"16", 146, "01M50QBCBW2KWJSAMRK2HVZGYY", "S1", "T1", "ring", 0.62, 6.27, "", ""},
    {"16", 147, "01M50QBJKEYRX9TMV6AD1CDR0X", "S5", "S20", "wedge", 1.18, 5, "", ""},
    {"16", 148, "01M50QBNSZY3GQP16ZP00K531X", "S20", "T20", "ring", 1.73, 5.91, "", ""},
    {"16", 149, "01M50QBR4371AXFX6T81SG0MG9", "T20", "S20", "ring", 6.11, 6.79, "", ""},
    {"16", 150, "01M50QBYRZX50SJ5WXKEMH68FK", "S17", "S3", "wedge", 1.38, 5, "", ""},
    {"16", 152, "01M50QC9QBCQ367CVD9N8BZKGC", "S19", "T19", "ring", 3.74, 5.45, "", ""},
    {"16", 153, "01M50QCGBDWX3BERNX8PA4QCQE", "S15", "S2", "wedge", 1.2, 7.28, "S2", "alternative"},
    {"16", 155, "01M50QDAE7NCN1NEVQQ0X8V4FN", "S12", "S9", "wedge", 1.1, 5, "", "turn"},
    {"16", 158, "01M50QKZ4MM78E21234NPPPVFQ", "S20", "T20", "ring", 2.71, 6.39, "", ""},
    {"16", 162, "01M50QNCT3TQR039Y3229TTZ6T", "S10", "25", "ring", 2.01, 5, "", ""},
    {"16", 171, "01M50QSXE2M46M610PKKX46RES", "S4", "T4", "ring", 6.3, 6.59, "", ""},
    {"16", 172, "01M50QSZACJHKR5RPQSFQ1V1AH", "T20", "T1", "wedge", 2.2, 5, "", ""},
    {"16", 173, "01M50QTE7ZGM89MFSEB1AG1BTH", "T20", "T1", "wedge", 4.51, 5, "", ""},
    {"16", 174, "01M50QTGBG4PXYXKARY4A2FSY1", "T1", "T20", "wedge", 1.22, 5, "", ""},
    {"16", 175, "01M50QV6VGT55CYC9KGBGPSXB1", "S11", "S8", "wedge", 4.03, 6.57, "", ""},
    {"16", 176, "01M50QVEGPPTMZD80A0ZCK3WEQ", "S4", "S13", "wedge", 2.94, 5.93, "", ""},
    {"16", 178, "01M50QWB42P0MJ9NWM005G57B0", "S1", "S20", "wedge", 0.57, 5, "", "turn"},
    {"16", 179, "01M50QWD5WDBR9A2RREYJG7APC", "S1", "S18", "wedge", 1.09, 5, "", "turn"},
    {"16", 182, "01M50QX64QGSKXS6YD59M65JV3", "T20", "S20", "ring", 4.33, 5.67, "", ""},
    {"16", 183, "01M50QXBP7MC78YGBNB1KE12T4", "S5", "T5", "ring", 2.29, 5.99, "", ""},
    {"16", 185, "01M50QXFT2H5ST5RPTC1K1QDXP", "S19", "T19", "ring", 3.1, 11.08, "T19", "alternative"},
    {"16", 189, "01M50QYXS147CKX4KENCS5S9ZB", "T12", "S12", "ring", 5.39, 5.91, "", ""},
    {"16", 190, "01M50QYZT3PD1GD61SKDQ6JGY7", "S3", "T3", "ring", 4.31, 6.59, "", ""},
    {"16", 191, "01M50QZ1XS6VGSDZZ4455ZBM8X", "S1", "S18", "wedge", 1.18, 5, "", ""},
    {"16", 195, "01M50QZJA8S64MFVNCJ84QT1QN", "S20", "T20", "ring", 3.66, 6.3, "", "turn"},
    {"16", 196, "01M50QZNW7FQAZ3VAHVCK959ME", "T1", "S1", "ring", 5.67, 6.27, "", "turn"},
    {"16", 197, "01M50QZTF68PJSWCFW6S8AXGM1", "S17", "S3", "wedge", 4.44, 5, "", "turn"},
    {"16", 200, "01M50R04Q7R821FN1NGF89J88H", "S5", "S20", "wedge", 1.72, 5.32, "", "turn"},
    {"16", 201, "01M50R0TDCFR6W6V3S4568SR8J", "S1", "S20", "wedge", 0.96, 5, "", "turn"},
    {"16", 203, "01M50R0YQD8SNAZNCCABPB3NX7", "S19", "T19", "ring", 9.19, 9.9, "", "turn"},
    {"16", 204, "01M50R1FHJ7DWKZXE2TZW5PT43", "S20", "S1", "wedge", 3.24, 5, "", "turn"},
    {"16", 207, "01M50R21XWBPWTRG0QYKFD6P5V", "S19", "S7", "wedge", 3.09, 5, "", ""},
    {"16", 212, "01M50R2R2K369T1AG8VRSPS9MF", "S20", "T20", "ring", 2, 6.14, "", ""},
    {"16", 213, "01M50R2XZPXEFH7G6YQCECG6WP", "T5", "S5", "ring", 5.04, 5.88, "", ""},
    {"16", 214, "01M50R304WWBDMJVKCDW25TK14", "S1", "T1", "ring", 4.07, 6.06, "", ""},
    {"16", 216, "01M50R394057BK2PW0P1Z62DVD", "S12", "S5", "wedge", 2.21, 5, "", ""},
    {"16", 217, "01M50R3BMGN8SZ3NBEEPA3SGQY", "S20", "S1", "wedge", 2.42, 5, "", ""},
    {"16", 218, "01M50R3DXJ2HNRZ025NXX5GWHM", "S1", "T1", "ring", 4.95, 5.51, "", ""},
    {"16", 219, "01M50R3KF2PRP4R407TDFVH6M8", "T1", "S1", "ring", 4.6, 5.96, "", ""},
    {"16", 225, "01M50R48VAKGGZT0C9CR8DYJC8", "T1", "T18", "wedge", 2.24, 5, "", ""},
    {"16", 226, "01M50R4B6ZAT1T702MZX0A058F", "T19", "S19", "ring", 0.93, 10.71, "", ""},
    {"16", 228, "01M50R4KJ2HVV62SJX3NDH36QM", "S1", "T1", "ring", 4.87, 5.95, "", ""},
    {"16", 231, "01M50R4ZC3J7HPXGTZ59BW4FPH", "S1", "T1", "ring", 4.41, 6.09, "", ""},
    {"16", 232, "01M50R51TYDAVJ417TJRFX7Q3A", "T3", "T19", "wedge", 4.92, 5, "", ""},
    {"16", 233, "01M50R544VN6M92387QB5J5GMS", "S4", "S18", "wedge", 4.79, 5, "", ""},
    {"16", 235, "01M50R5H27WZCTZ6W3TVW89NAJ", "S10", "T10", "ring", 1.61, 5, "", ""},
    {"16", 237, "01M50R5VE0DCSK9WW7SZ3X7KE2", "S19", "T19", "ring", 9.15, 10.35, "", ""},
    {"16", 239, "01M50R62MXEHD5DM6BZHK237WN", "S14", "S9", "wedge", 4.32, 6.95, "", ""},
    {"16", 242, "01M50R6NMC3BYCM852DBCHJVR7", "S11", "S14", "wedge", 4.57, 5, "", ""},
    {"16", 254, "01M52FPSYR53YE4VVJW6PJ8PP8", "S20", "S1", "wedge", 4.65, 5, "", ""},
    {"16", 258, "01M52FX74K2BH61T61Q19MBKGE", "S9", "S12", "wedge", 0.65, 5, "", ""},
    {"16", 259, "01M52FXGASJT5Q3X0633RKR0R5", "S1", "S20", "wedge", 4.21, 5, "", ""},
    {"16", 262, "01M52FXYEXTN26SSM28J24M8T2", "T16", "S7", "wedge", 1.2, 5.94, "", ""},
    {"16", 268, "01M52FYVK09B49S21KHW3YHA7N", "T20", "S20", "ring", 1.17, 5.5, "", ""},
    {"16", 270, "01M52FYZY1HX7DP7MKPFVX80R4", "T5", "S5", "ring", 3.24, 5, "", ""},
    {"16", 271, "01M52FZ9ET3HHAJ6B5SNXWEZZ8", "S5", "T5", "ring", 4.55, 5, "", ""},
    {"16", 280, "01M52G0QPKVBQECRNK9WVMXVP1", "S5", "S12", "wedge", 5, 5, "", ""},
    {"16", 283, "01M52G1DMSC2WJX6XT7EEY5P5H", "T3", "T19", "wedge", 1.73, 5, "", ""},
    {"16", 286, "01M52G1XN940X7A5VC5XAWQJQ1", "S19", "S3", "wedge", 3.56, 5, "", ""},
    {"16", 287, "01M52G1ZNGP8BMHJGZ1ABTHQYA", "S5", "S12", "wedge", 1.49, 6.25, "", ""},
    {"16", 291, "01M52G3JSP3BBVBRXT4KJMV46T", "S20", "S5", "wedge", 2.41, 5, "", ""},
    {"16", 292, "01M52G3X59HX1WN37D8X4GSPFY", "S1", "T1", "ring", 0.8, 5.22, "", ""},
    {"16", 293, "01M52G3Z6MMGKHTNHBKKT1QNDF", "T20", "S20", "ring", 0.94, 6.01, "", ""},
    {"16", 294, "01M52G41PHHBC3QQ0DAXNYYJJ8", "S1", "S18", "wedge", 1.72, 5, "", ""},
    {"16", 297, "01M52G4MMQF1BMEGWPAMSC9KF2", "S20", "S1", "wedge", 2.42, 5, "", ""},
    {"16", 299, "01M52G51KBDSFRJVQPB954NW28", "S8", "S11", "wedge", 2.91, 5.46, "", ""},
    {"16", 300, "01M52G53T9VZ5QH3MJDFS2PMJ7", "S20", "S5", "wedge", 1.33, 5.63, "", ""},
    {"16", 301, "01M52G5FEH24W0BFNATBXX447N", "S18", "S4", "wedge", 1.95, 5.69, "", ""},
    {"16", 307, "01M52G9PAR76C9HHKEN23750Q4", "S18", "S4", "wedge", 2.77, 5.26, "", ""},
    {"16", 308, "01M52G9RBVVDY0KXFG5VZCFSZW", "S20", "S1", "wedge", 0.22, 5, "", ""},
    {"16", 310, "01M52GASGMQRSCYRVMD47BJMQ9", "S5", "S20", "wedge", 3.9, 5, "", ""},
    {"16", 311, "01M52GAWCXHZ9FY6WBA8DY9B53", "S5", "S20", "wedge", 5.81, 6.57, "", ""},
    {"16", 313, "01M52GB7ED54XZT8304HFNTGW8", "S20", "S1", "wedge", 0.66, 5, "T20", "candidate"},
    {"16", 318, "01M52GCCRFHWHFSFR5BSX8K55R", "S20", "T20", "ring", 3.21, 6.28, "", ""},
    {"16", 325, "01M52GDHCWECPB4GMQ153RC4V7", "S3", "S19", "wedge", 1.66, 5, "", ""},
    {"16", 327, "01M52GDP2C048ZX2VF5Q47V86P", "S5", "S20", "wedge", 0.43, 5, "", ""},
    {"16", 328, "01M52GDX6Z72AQ5HANR7YYK8FD", "S1", "T1", "ring", 0.13, 5.1, "", "turn"},
    {"16", 334, "01M52GG7V8DQ8VMQG3HBKZTS5W", "T20", "S20", "ring", 2.41, 5.81, "", ""},
    {"16", 338, "01M52GHFMJJ5B923W5BPT7TX3Q", "S20", "T20", "ring", 1.63, 5.31, "T20", "alternative"},
    {"16", 348, "01M52GKCA28EDJ5WMVQ3AJW96C", "S1", "S20", "wedge", 0.92, 5.78, "S20", "alternative"},
    {"16", 349, "01M52GKQFPN4TFB5RXW3H1FQWM", "S16", "S7", "wedge", 3.15, 5.28, "", ""},
    {"16", 350, "01M52GKSKZ4K7M8WFYGA0PHZTA", "S17", "S3", "wedge", 1.2, 5, "", ""},
    {"16", 351, "01M52GKW778RA3A2A9CWETTNTT", "S3", "S19", "wedge", 6.96, 8.41, "", ""},
    {"16", 352, "01M52GM70NAF1QAAP2PRDTT4HK", "S14", "S11", "wedge", 3.21, 5.74, "", ""},
    {"16", 353, "01M52GM9A4CGAFKRWKBWGEC0P0", "T20", "S20", "ring", 0.69, 5.69, "", ""},
    {"16", 355, "01M52GMNTJ5P4AH2SG3GYZPZ7H", "S5", "S12", "wedge", 2.99, 5, "", "turn"},
    {"16", 356, "01M52GMS497W6Y146QG92NFQPC", "S5", "S20", "wedge", 1.36, 5.08, "", "turn"},
    {"16", 357, "01M52GMW37GA8N52B3D5VY5F4Z", "S20", "S5", "wedge", 1.42, 5, "", "turn"},
    {"16", 361, "01M52GP5HG7F8QRHGN139MB0DF", "T20", "S20", "ring", 2.63, 5.77, "", ""},
    {"16", 362, "01M52GPF9DXB6RWKD21TVNZ0D5", "S5", "S12", "wedge", 2.27, 5.26, "S12", "alternative"},
    {"16", 367, "01M52GR9WPHAHN9GPCZRB1V50B", "S18", "T18", "ring", 0.69, 5, "T18", "alternative"},
    {"16", 368, "01M52GRC1WBMVQ22EJ74G33ASZ", "S20", "T20", "ring", 0.69, 5.59, "", ""},
    {"16", 372, "01M52GSD13WXNDM240QW4ZRCM2", "S16", "S8", "wedge", 3.8, 5.08, "", ""},
    {"16", 376, "01M52GTRRCB06KR6SY6WT73R0T", "S19", "S7", "wedge", 2.45, 5, "", ""},
    {"16", 378, "01M52GV4Q6PAMN74F3AWSKJ0MD", "S9", "S14", "wedge", 1.77, 5.61, "S14", "alternative"},
    {"16", 381, "01M52GWZT3DARKR3NTES0Z5J8G", "S1", "T1", "ring", 4.27, 5.77, "", ""},
    {"16", 386, "01M52GXMASQZV0X663DB4TMKH7", "S5", "S12", "wedge", 0.02, 5, "S12", "alternative"},
    {"16", 387, "01M52GY007W54REXFN6ZVM27F6", "S5", "T5", "ring", 3.04, 5.23, "", ""},
    {"16", 390, "01M52GYE9CV42F7TMQS1PBCZW8", "S18", "T18", "ring", 4.51, 6.17, "", ""},
    {"16", 391, "01M52GYGJXHC70BEH75MFES0V3", "T5", "S5", "ring", 0.76, 5.76, "", ""},
    {"16", 398, "01M52GZGFJAKEB78KWV1ZAB0MN", "S20", "S5", "wedge", 0.42, 7.8, "", ""},
    {"16", 399, "01M52H0066HCDM10T7HQ84ESRC", "S20", "S5", "wedge", 1.29, 5, "S5", "alternative"},
    {"16", 405, "01M52H17JFQJMWAS966FEH93CK", "S12", "T12", "ring", 3.84, 5.08, "", ""},
    {"16", 408, "01M52H1NYZ272CSR2G3ERRT0BT", "S20", "T20", "ring", 3.92, 5.71, "", ""},
    {"16", 409, "01M52H1RT2RFCF1YVN5VR14AZX", "S19", "S7", "wedge", 3.07, 5, "", ""},
    {"16", 411, "01M52H23NAE64229MDPBMQ0EQ4", "S20", "S5", "wedge", 4.29, 5, "", ""},
    {"16", 412, "01M52H2D5Z5CKPRN29F67BC0H5", "T18", "S18", "ring", 2.76, 5, "", ""},
    {"16", 413, "01M52H2G7V4JVRGF890Q6NE00Q", "S13", "S4", "wedge", 1.77, 5, "S4", "alternative"},
    {"16", 415, "01M52H31XEYHDZ93KXVE130RS1", "S20", "T20", "ring", 2.05, 5.45, "", ""},
    {"16", 416, "01M52H342E5YKJ2N96R8205BNB", "T20", "S20", "ring", 2.91, 5.38, "", ""},
    {"16", 423, "01M52H4AY703ZJ4P5BVH65AHVB", "S20", "S1", "wedge", 0.64, 5, "T20", "candidate"},
    {"16", 424, "01M52H4D6HR7DP8KJPBVEZ76SP", "S20", "T20", "ring", 4.52, 5.99, "", ""},
    {"16", 425, "01M52H4FBHAAB8NXG703RYVGAS", "T12", "S12", "ring", 1.57, 5, "", ""},
    {"16", 426, "01M52H4VNQMCPRNC0BX2MWD4GQ", "T20", "S20", "ring", 2.5, 5.87, "", ""},
    {"16", 428, "01M52H51CEHQ0WWFNY0N62GJBF", "T1", "S1", "ring", 3.34, 5.66, "", ""},
    {"16", 431, "01M52H5GXB5P30QZ1KCXSKK2ZJ", "S20", "T20", "ring", 3.85, 5.15, "", ""},
    {"16", 432, "01M52H5TFX0A443VJNBTCVQ7XY", "S20", "T20", "ring", 1.53, 5.56, "", ""},
    {"16", 433, "01M52H5WWQ4ZB3SMRHYQ94HFHB", "S1", "D1", "ring", 9.69, 11.71, "", ""},
    {"16", 435, "01M52H682C5SQJZ5YF0C14H8K8", "S1", "S18", "wedge", 0.51, 5, "", ""},
    {"16", 436, "01M52H6ADW9NB41KY7E37YZ80A", "S20", "T20", "ring", 4.49, 5.37, "", ""},
    {"16", 437, "01M52H6CZF4S1CVZVTGFDNWP1D", "S20", "S1", "wedge", 1.1, 5, "", ""},
    {"16", 439, "01M52H6R9QQ3J18VWGZ18PC34R", "S20", "S1", "wedge", 2.96, 5.13, "", ""},
    {"16", 441, "01M52H7421FAQ52XP97K10KFPN", "S20", "T20", "ring", 2.85, 5.81, "", ""},
    {"16", 442, "01M52H76CH18VBNREYXH9CEMP0", "S20", "S1", "wedge", 0.37, 5, "", ""},
    {"16", 443, "01M52H78QQ7HCDKYGN2K6EHGJK", "S20", "S5", "wedge", 3.87, 5, "", ""},
    {"16", 444, "01M52H7K28YKJ1DVESNQCGERS6", "S20", "T20", "ring", 2.2, 5.88, "", ""},
    {"16", 447, "01M52H815B1SVRN9TQQBQYRZXG", "S4", "S18", "wedge", 1.44, 5, "S18", "alternative"},
    {"16", 449, "01M52H8FQB3V98E89417RF8BH9", "S10", "S6", "wedge", 2.15, 5, "", ""},
    {"16", 455, "01M52H9BHV10V13ZBVR7X2Y1RE", "T5", "S5", "ring", 3.15, 5.23, "", ""},
    {"16", 456, "01M52H9KNQ9EZZK51PMEK3WV0F", "S17", "S3", "wedge", 2.02, 5, "", ""},
    {"16", 458, "01M52H9T3J3N3517P8HHX5FGAG", "S20", "T20", "ring", 1.47, 5.34, "T20", "alternative"},
    {"16", 459, "01M52HA3M1NAN8TGFDWZBXX0JF", "S5", "S12", "wedge", 10.83, 11.65, "", ""},
    {"16", 470, "01M52HD9RCPK6NP7AD2QP4X23B", "S20", "S1", "wedge", 5, 5, "", ""},
};

static bool isCorrected(const Row &r)
{
    return std::strcmp(r.picked, "alternative") == 0 || std::strcmp(r.picked, "candidate") == 0 ||
           std::strcmp(r.picked, "typed") == 0;
}

static bool isSetAside(const Row &r)
{
    return std::strcmp(r.picked, "turn") == 0 || std::strcmp(r.picked, "removed") == 0;
}

static double zOf(const Row &r)
{
    return r.marginMm / r.sigmaMm;
}

static BoundaryCall callAt(const Row &r, double k)
{
    const BoundaryCall flagged = decideBoundaryCall(true, zOf(r) <= 1.0, r.published, r.alternative, r.kind,
                                                    r.marginMm, r.sigmaMm);
    return tightenBoundaryCall(flagged, zOf(r), k);
}

static std::string name(const Row &r)
{
    char b[200];
    snprintf(b, sizeof(b), "2026-10-%s dart %d %s %s/%s z=%.3f%s%s", r.day, r.dart, r.reference, r.published,
             r.alternative, zOf(r), *r.corrected ? " corrected to " : "", r.corrected);
    return b;
}

int main()
{
    const double K = flagSigmasFrom(nullptr);
    printf("I1874 DEFAULT k=%.2f\n", K);
    const char *days[] = {"13", "15", "16"};
    // What this tree's default is predicted to give, per day, in the spans.
    const int kExpectFlags[] = {22, 119, 119};
    const int kExpectFixed10[] = {37, 176, 196};
    const int kExpectFixed07[] = {26, 136, 137};
    int totalBefore = 0, totalAfter = 0, total10 = 0, totalLost = 0;
    for (int d = 0; d < 3; d++)
    {
        int at10 = 0, at07 = 0, atK = 0, corrected = 0, lostK = 0, lost07 = 0, asideK = 0;
        for (const Row &r : kRows)
        {
            if (std::strcmp(r.day, days[d]) != 0)
            {
                continue;
            }
            at10 += callAt(r, 1.0).flagged;
            const bool f07 = callAt(r, 0.7).flagged;
            const bool fK = callAt(r, K).flagged;
            at07 += f07;
            atK += fK;
            asideK += fK && isSetAside(r);
            if (isCorrected(r))
            {
                corrected++;
                lostK += !fK;
                lost07 += !f07;
            }
        }
        printf("I1874 DAY 2026-10-%s flags k=1.00 %d  k=0.70 %d  k=%.2f %d  (removed vs 0.70: %d)  "
               "corrected %d, losing their flag at %.2f: %d (at 0.70: %d)  turn/removed still flagged %d\n",
               days[d], at10, at07, K, atK, at07 - atK, corrected, K, lostK, lost07, asideK);
        say(at10 == kExpectFixed10[d] && at07 == kExpectFixed07[d],
            std::string("fixed: 2026-10-") + days[d] + " flags at 1.0 and 0.7 are the boards' and v0.1.16's");
        say(atK == kExpectFlags[d], std::string("count: 2026-10-") + days[d] + " flags at the default: " +
                                        std::to_string(atK) + ", predicted " + std::to_string(kExpectFlags[d]));
        totalBefore += at07;
        totalAfter += atK;
        total10 += at10;
        totalLost += lostK;
    }
    printf("I1874 TOTAL flags k=1.00 %d  k=0.70 %d  k=%.2f %d  corrected losing their flag %d\n", total10,
           totalBefore, K, totalAfter, totalLost);
    say(totalAfter == 260, "count: three spans' flags at the default: " + std::to_string(totalAfter) + ", predicted 260");
    say(totalLost == 1, "count: corrected darts losing their flag at the default: " + std::to_string(totalLost) +
                            ", predicted 1");

    for (const Row &r : kRows)
    {
        const BoundaryCall c = callAt(r, K);
        if (isCorrected(r))
        {
            if (r.dart == 43 && std::strcmp(r.day, "13") == 0)
            {
                say(!c.flagged, "lost: " + name(r) + " (typed; its offered " + r.alternative +
                                    " could not fix it) is cleared at the default");
            }
            else
            {
                say(c.flagged && c.alternative == r.alternative, "kept: " + name(r) + " keeps its flag");
            }
        }
        else if (!isSetAside(r) && zOf(r) > 0.6 && zOf(r) <= 0.7)
        {
            say(!c.flagged && c.alternative.empty(), "cleared: " + name(r) + " stood, and publishes clear");
        }
        if (r.dart == 2934 && std::strcmp(r.day, "15") == 0)
        {
            say(c.account.find("outside the flag's 0.60 (OD_FLAG_SIGMAS, #1815)") != std::string::npos,
                "account: " + name(r) + " says the threshold that cleared it: " + c.account);
        }
    }

    // #1815's two true flags between 0.5 and 0.6, from its own rows (i1815_flag_check.cpp).
    struct Edge
    {
        const char *where, *published, *alternative, *kind;
        double marginMm, sigmaMm;
    };
    const Edge edges[] = {
        {"board 20 S20 -> None (typed), z 0.524", "S20", "T20", "ring", 2.87, 5.48},
        {"rig-20260929 dev v9.3 S19 for T19, z 0.569", "S19", "T19", "ring", 3.32, 5.83},
    };
    for (const Edge &e : edges)
    {
        const BoundaryCall f =
            decideBoundaryCall(true, true, e.published, e.alternative, e.kind, e.marginMm, e.sigmaMm);
        const BoundaryCall c = tightenBoundaryCall(f, e.marginMm / e.sigmaMm, K);
        say(c.flagged && c.alternative == e.alternative, std::string("edge: ") + e.where + " keeps its flag");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

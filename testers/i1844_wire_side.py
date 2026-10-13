# unrun-tester: a reporter over Turnaus truth lines (real sessions kept off GitHub) for turnaus#1844; it asserts nothing about the tree, its inputs are not in the repository, and docs/rig.md records its counts. --self-test checks its own arithmetic.
"""turnaus#1844: do three-camera geometric solves near a wire publish the wrong side more often than chance?

    python3 i1844_wire_side.py <truth-line.txt> [<truth-line.txt> ...]
    python3 i1844_wire_side.py --self-test

Reads each truth line through i1815_census.read_truth_line (by its own `# <columns>` line,
never by position; a malformed line is exit 3 there) and reuses #1815's verdict: `picked`
alternative / candidate / typed is CORRECTED (the published score was wrong), `-` or
`published` STOOD (nobody corrected it -- not proof it was right), `turn` / `removed` are SET
ASIDE and counted apart.

THE CAMERA CLASS is the `cameras` field's count (2 or 3+), which is the issue's "solved from
three cameras". #1815's rule names read `crossing_deg` instead; a row where the two disagree
is printed as I1844 CLASS-DISAGREES and bucketed by its cameras.

    I1844 BUCKET set=<all|board> cams=<2|3> wire=<ring|wedge> z=<band> posted= flagged=
          corrected= to_alternative= to_candidate= to_typed= stood= set_aside= expected=
        z = margin_mm / sigma_mm, the solve's distance from its nearest wire in its own
        across-wire sigmas; bands <0.25, 0.25-0.5, 0.5-1 (1.0 inclusive, the flag's own
        edge) and >1. `expected` is sum Phi(-z) over the bucket's decided darts (corrected +
        stood): the number of wrong-side publishes an honest Gaussian sigma predicts. Darts
        with no margin or sigma are band `none`.
    I1844 SIDE ref= cams= wire= published= corrected= z= radius_mm= move=<outward|inward|
          across-wedge|to-miss|from-miss|unknown>
        every corrected geometric dart, and every corrected vote dart, with the radial
        direction of the correction: the published and corrected scores' ring bands
        (bull, outer bull, inner single, treble, outer single, double, off the board), a
        single placed inner or outer by the solve's radius x 170 mm against 103 mm (the
        treble's middle), a corrected single across a treble by the same radius. A dart
        corrected to `None` from anything but a double is `to-miss` (it was not there, which
        is no radial side), and a published `None` corrected to a score is `from-miss`.
    I1844 SIDE-TALLY scope= outward= inward= across-wedge= unknown=

Stdlib only. Exit 0 when it read darts, 2 when there were none.
"""

import collections
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i1815_census import num, read_truth_line, verdict, z_of  # noqa: E402

BANDS = (("<0.25", 0.0, 0.25), ("0.25-0.5", 0.25, 0.5), ("0.5-1", 0.5, 1.0), (">1", 1.0, float("inf")))
RING_BAND = {"bull": 0, "outer": 1, "inner-single": 2, "triple": 3, "outer-single": 4, "double": 5, "off": 6}
SCORING_RADIUS_MM = 170.0
TREBLE_MIDDLE_MM = 103.0


def phi_lower(z):
    """Phi(-z): the chance an honest Gaussian puts the tip across a wire z sigmas away."""
    return 0.5 * math.erfc(z / math.sqrt(2.0))


def band_of(z):
    if z is None:
        return "none"
    for name, lo, hi in BANDS:
        if lo <= z < hi or (hi == 1.0 and z == 1.0):
            return name
    return ">1"


def cams_of(row):
    c = row.get("cameras", "-")
    n = 0 if c in ("-", "") else len(c.split(","))
    return "3" if n >= 3 else str(n)


def ring_band(score, radius_mm, published_band=None):
    """The radial band of a score; a single needs the radius (or, corrected, the published band)."""
    if score in ("BULL", "Bull", "50"):
        return RING_BAND["bull"]
    if score in ("OUTER", "25"):
        return RING_BAND["outer"]
    if score in ("None", "MISS"):
        return RING_BAND["off"]
    if not score or score == "-":
        return None
    kind = score[0]
    if kind == "T":
        return RING_BAND["triple"]
    if kind == "D":
        return RING_BAND["double"]
    if kind != "S":
        return None
    if published_band in (RING_BAND["bull"], RING_BAND["outer"]):
        return RING_BAND["inner-single"]
    if published_band in (RING_BAND["double"], RING_BAND["off"]):
        return RING_BAND["outer-single"]
    if radius_mm is None:
        return None
    return RING_BAND["inner-single"] if radius_mm < TREBLE_MIDDLE_MM else RING_BAND["outer-single"]


def move_of(published, corrected, radius_mm):
    if published in ("None", "MISS"):
        return "from-miss"
    if corrected in ("None", "MISS") and not published.startswith("D"):
        return "to-miss"
    p = ring_band(published, radius_mm)
    if p is None:
        return "unknown"
    c = ring_band(corrected, radius_mm, published_band=p if not published.startswith("S") else None)
    if c is None:
        return "unknown"
    if c == p:
        return "across-wedge" if published != corrected else "unknown"
    return "outward" if c > p else "inward"


def tabulate(rows):
    """{(cams, wire, band): Counter} over geometric darts."""
    t = collections.defaultdict(collections.Counter)
    for r in rows:
        if r["path"] != "geometry":
            continue
        z = z_of(r)
        key = (cams_of(r), r["wire_kind"], band_of(z))
        c = t[key]
        c["posted"] += 1
        c["flagged"] += r["flagged"] == "1"
        v = verdict(r)
        if v == "true":
            c["corrected"] += 1
            c["to_" + r["picked"]] += 1
        elif v == "false":
            c["stood"] += 1
        else:
            c["set_aside"] += 1
            c["set_aside_" + r["picked"]] += 1
        if v != "ambiguous" and z is not None:
            c["expected_milli"] += round(1000 * phi_lower(z))
    return t


def print_table(label, t):
    for key in sorted(t, key=lambda k: (k[0], k[1], [b[0] for b in BANDS].index(k[2]) if k[2] in [b[0] for b in BANDS] else 9)):
        c = t[key]
        print("I1844 BUCKET set=%s cams=%s wire=%s z=%s posted=%d flagged=%d corrected=%d to_alternative=%d "
              "to_candidate=%d to_typed=%d stood=%d set_aside=%d(turn=%d,removed=%d) expected=%.2f" % (
                  label, key[0], key[1], key[2], c["posted"], c["flagged"], c["corrected"], c["to_alternative"],
                  c["to_candidate"], c["to_typed"], c["stood"], c["set_aside"], c["set_aside_turn"],
                  c["set_aside_removed"], c["expected_milli"] / 1000.0))


def census(paths):
    allrows = []
    for p in paths:
        rows = read_truth_line(p)
        if rows and "cameras" not in rows[0]:
            sys.stderr.write("%s: the columns line names no `cameras`\n" % p)
            sys.exit(3)
        board = os.path.basename(p).replace("truth-", "").replace(".txt", "")
        for r in rows:
            r["_board"] = board
        geo = [r for r in rows if r["path"] == "geometry"]
        print("I1844 READ %s darts=%d geometric=%d" % (board, len(rows), len(geo)))
        for r in geo:
            two_line = r["crossing_deg"] != "-"
            if two_line != (cams_of(r) == "2"):
                print("I1844 CLASS-DISAGREES %s %s cameras=%s crossing_deg=%s published=%s picked=%s" % (
                    board, r["reference"], r["cameras"], r["crossing_deg"], r["published"], r["picked"]))
        if geo:
            print_table(board, tabulate(rows))
        allrows += rows
    if not allrows:
        return 2
    print_table("all", tabulate(allrows))
    tally = collections.defaultdict(collections.Counter)
    for r in allrows:
        if verdict(r) != "true" or r["path"] not in ("geometry", "vote"):
            continue
        target = r["corrected"]
        rad = num(r["radius"])
        rmm = rad * SCORING_RADIUS_MM if rad is not None else None
        mv = move_of(r["published"], target, rmm)
        cams = cams_of(r) if r["path"] == "geometry" else "vote"
        z = z_of(r)
        print("I1844 SIDE %s %s path=%s cams=%s wire=%s published=%s corrected=%s picked=%s z=%s radius_mm=%s move=%s" % (
            r.get("_board", "-"), r["reference"], r["path"], cams, r["wire_kind"], r["published"], target,
            r["picked"], "%.2f" % z if z is not None else "-", "%.1f" % rmm if rmm is not None else "-", mv))
        tally["%s-cams%s-%s" % (r["path"], cams, r["wire_kind"])][mv] += 1
        tally["all"][mv] += 1
    for scope in sorted(tally):
        c = tally[scope]
        print("I1844 SIDE-TALLY scope=%s outward=%d inward=%d across-wedge=%d to-miss=%d from-miss=%d unknown=%d" % (
            scope, c["outward"], c["inward"], c["across-wedge"], c["to-miss"], c["from-miss"], c["unknown"]))
    return 0


def self_test():
    fails = 0

    def check(name, got, want):
        nonlocal fails
        if got != want:
            fails += 1
            print("FAIL %s: got %r, want %r" % (name, got, want))

    check("band edge 1.0 is the flag's", band_of(1.0), "0.5-1")
    check("band 0.25", band_of(0.25), "0.25-0.5")
    check("band 0", band_of(0.0), "<0.25")
    check("band none", band_of(None), "none")
    check("phi(0)", round(phi_lower(0.0), 3), 0.5)
    check("phi(1)", round(phi_lower(1.0), 3), 0.159)
    check("T16->S16 beyond the treble", move_of("T16", "S16", 108.3), "outward")
    check("T16->S16 inside the treble", move_of("T16", "S16", 99.5), "inward")
    check("S5->T5 from outside", move_of("S5", "T5", 110.0), "inward")
    check("S5->T5 from inside", move_of("S5", "T5", 96.0), "outward")
    check("D1->None", move_of("D1", "None", 169.0), "outward")
    check("S20->None is no side", move_of("S20", "None", 93.0), "to-miss")
    check("None->D20 is no side", move_of("None", "D20", None), "from-miss")
    check("S6->S13 wedge", move_of("S6", "S13", 60.0), "across-wedge")
    check("D20->S20", move_of("D20", "S20", 163.0), "inward")
    check("25->S13", move_of("25", "S13", 11.0), "outward")
    check("S1->25", move_of("S1", "25", 17.0), "inward")
    check("cams", (cams_of({"cameras": "1,2,3"}), cams_of({"cameras": "2,3"})), ("3", "2"))
    print("I1844 SELF-TEST %s (%d failed)" % ("ok" if not fails else "FAILED", fails))
    return 1 if fails else 0


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if not args:
        sys.stderr.write(__doc__)
        return 2
    return census(args)


if __name__ == "__main__":
    sys.exit(main())

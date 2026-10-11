# unrun-tester: a reporter over a Turnaus truth line (real sessions kept off GitHub) and over a #1555 bakeoff's logs (turnaus#1815); it asserts nothing, its inputs are not in the repository, and docs/rig.md records its counts.
"""turnaus#1815: which rule raised each flag, which flags were true, and what OD_FLAG_SIGMAS=<K> changes.

    python3 i1815_census.py --truth-line <truth-casual-N-day.txt> [--k 0.7 ...]
    python3 i1815_census.py --log <run.txt> --corner <i1782_census output for that run> [--k 0.7 ...]

TRUTH-LINE MODE. A session's truth line (turnaus#1786, read by its own `# <columns>` line as
i1789_truth_census.py does). Per dart the flag decision is RECOMPUTED from the account's own
fields, never from the log:

  * a geometric publish is flagged by #1556 where `margin_mm <= sigma_mm` (z <= 1.0) and an
    alternative could be named;
  * a vote publish is flagged by #1773 where its ring-wire margin is under 5 mm: the
    `ring_wire_mm` field where the build wrote it, otherwise recomputed from `radius` (x 170 mm)
    and the published ring against the spec's wires, as ringWireMarginMm does.

THE RULE a flag belongs to is `geo-<wire_kind>-<2line|3line>` (two-line where `crossing_deg`
is set) or `vote-ringwire-<lone|consensus>` (`agreeing` 1 or more). THE VERDICT is the
marker's: `picked` alternative / candidate / typed is TRUE (the score was wrong), `-` or
`published` is FALSE (it stood), `turn` (a Casual total naming no dart) and `removed` (no dart
there) are AMBIGUOUS and counted apart -- neither side may claim them.

    I1815 RECOMPUTE agree= disagree=        the recomputed flag against the board's `flagged`
    I1815 RULE <rule> false= true= ambiguous=
    I1815 SWEEP k= flags= cut_false= cut_true= cut_ambiguous= false_rate=
                                            the geometric flag at z <= k; the vote's unchanged
    I1815 CHANGED k= <posted_at> <published> <alternative> z= verdict=
                                            every flag the first --k clears, by name
    I1815 UNFLAGGED-WRONG path= published= corrected= picked= ...
                                            each wrong dart nobody flagged (#1816's ground)

LOG MODE. A bakeoff run's log, made with OD_GEO_SCORE=on: each geometric publish's
I1556PUBLISH (its boundary and sigma, so z) and, where the binary has it, I1815FLAG (what
#1556's 1.0 decided and what published). The thrown score per window comes from the
i1782_census.py output for the same run (its `I1782 DART` lines), which is where the
detection-to-throw join already lives. Prints I1815 FIXTURE lines per flagged dart and a TALLY.

Stdlib only. Exit 0 when it read something, 2 when there was nothing to census, 3 when the
truth line is not the format this reads.
"""

import argparse
import collections
import re
import sys

PICK_TRUE = ("alternative", "candidate", "typed")
PICK_FALSE = ("-", "published")
WIRES = {
    "bull": [6.35],
    "outer": [6.35, 15.9],
    "triple": [99.0, 107.0],
    "double": [162.0, 170.0],
}
SIGMA_VOTE_MM = 5.0


def num(x):
    return None if x in ("-", "") else float(x)


def read_truth_line(path):
    cols, rows = None, []
    for n, raw in enumerate(open(path, "r", errors="replace"), 1):
        line = raw.rstrip("\n")
        if line.startswith("# turnaus truth line"):
            continue
        if line.startswith("# "):
            cols = line[2:].split()
            continue
        if not line.strip():
            continue
        if cols is None:
            sys.stderr.write("line %d: a data line before the columns line\n" % n)
            sys.exit(3)
        v = line.split(" ")
        if len(v) != len(cols):
            sys.stderr.write("line %d: %d fields where the columns line names %d\n" % (n, len(v), len(cols)))
            sys.exit(3)
        rows.append(dict(zip(cols, v)))
    need = ("published", "alternative", "corrected", "picked", "path", "flagged", "crossing_deg", "sigma_mm",
            "margin_mm", "wire_kind", "agreeing", "ring_wire_mm", "radius", "posted_at")
    if rows and any(c not in rows[0] for c in need):
        sys.stderr.write("the columns line lacks one of: %s\n" % " ".join(need))
        sys.exit(3)
    return rows


def ring_of(score):
    if score in ("BULL", "Bull", "50"):
        return "bull"
    if score in ("OUTER", "25"):
        return "outer"
    if score in ("None", "MISS", "-") or not score:
        return None
    return {"S": "single", "T": "triple", "D": "double"}.get(score[0])


def ring_margin(r):
    """#1773's margin: the field where written, else ringWireMarginMm from the radius."""
    if num(r["ring_wire_mm"]) is not None:
        return num(r["ring_wire_mm"])
    rad, ring = num(r["radius"]), ring_of(r["published"])
    if rad is None or ring is None:
        return None
    mm = rad * 170.0
    wires = WIRES.get(ring) or ([15.9, 99.0] if mm < 103.0 else [107.0, 162.0])
    return min(abs(mm - w) for w in wires)


def z_of(r):
    m, s = num(r["margin_mm"]), num(r["sigma_mm"])
    return None if m is None or not s else m / s


def recomputed_flag(r):
    if r["path"] == "geometry":
        z = z_of(r)
        return z is not None and z <= 1.0 and r["wire_kind"] != "-"
    rm = ring_margin(r)
    return rm is not None and rm < SIGMA_VOTE_MM and r["agreeing"] not in ("0", "-")


def rule_of(r):
    if r["path"] == "geometry":
        return "geo-%s-%s" % (r["wire_kind"], "2line" if r["crossing_deg"] != "-" else "3line")
    return "vote-ringwire-%s" % ("lone" if r["agreeing"] == "1" else "consensus")


def verdict(r):
    if r["picked"] in PICK_TRUE:
        return "true"
    if r["picked"] in PICK_FALSE:
        return "false"
    return "ambiguous"


def truth_mode(path, ks):
    rows = read_truth_line(path)
    if not rows:
        return 2
    agree = sum(recomputed_flag(r) == (r["flagged"] == "1") for r in rows)
    print("I1815 RECOMPUTE darts=%d agree=%d disagree=%d" % (len(rows), agree, len(rows) - agree))
    for r in rows:
        if recomputed_flag(r) != (r["flagged"] == "1"):
            print("I1815 DISAGREE %s path=%s flagged=%s margin=%s sigma=%s ring_margin=%s" % (
                r["posted_at"], r["path"], r["flagged"], r["margin_mm"], r["sigma_mm"], ring_margin(r)))
    rules = collections.defaultdict(collections.Counter)
    flagged = [r for r in rows if r["flagged"] == "1"]
    for r in flagged:
        rules[rule_of(r)][verdict(r)] += 1
    for name in sorted(rules):
        c = rules[name]
        print("I1815 RULE %-24s false=%d true=%d ambiguous=%d" % (name, c["false"], c["true"], c["ambiguous"]))
    for k in sorted(set([1.0] + ks), reverse=True):
        c = collections.Counter()
        for r in flagged:
            kept = r["path"] != "geometry" or z_of(r) <= k
            c[("kept_" if kept else "cut_") + verdict(r)] += 1
        kept = c["kept_false"] + c["kept_true"] + c["kept_ambiguous"]
        print("I1815 SWEEP k=%.2f flags=%d cut_false=%d cut_true=%d cut_ambiguous=%d false_rate=%.3f" % (
            k, kept, c["cut_false"], c["cut_true"], c["cut_ambiguous"], c["kept_false"] / kept if kept else 0.0))
    if ks:
        k = ks[0]
        for r in flagged:
            if r["path"] == "geometry" and z_of(r) > k:
                print("I1815 CHANGED k=%.2f %s %s alt=%s %s z=%.3f verdict=%s picked=%s" % (
                    k, r["posted_at"], r["published"], r["alternative"], rule_of(r), z_of(r), verdict(r), r["picked"]))
    for r in rows:
        if r["flagged"] != "1" and verdict(r) == "true":
            print("I1815 UNFLAGGED-WRONG path=%s published=%s corrected=%s picked=%s margin=%s sigma=%s z=%s "
                  "ring_margin=%s agreeing=%s radius=%s angle=%s candidates=%s" % (
                      r["path"], r["published"], r["corrected"], r["picked"], r["margin_mm"], r["sigma_mm"],
                      "%.2f" % z_of(r) if z_of(r) is not None else "-",
                      "%.2f" % ring_margin(r) if ring_margin(r) is not None else "-", r["agreeing"], r["radius"],
                      r["angle"], r["candidates"]))
    return 0


PUBLISH_RE = re.compile(r"I1556PUBLISH window=(\d+) geometry=1 flagged=(\d) score=(\S+) alt=(\S+) kind=(\S+) "
                        r"boundary=(\S+) sigma=(\S+) conf=(\S+) board=(\S+)")
FLAG_RE = re.compile(r"I1815FLAG window=(\d+) z=(\S+) k=(\S+) default=(\d) flagged=(\d) score=(\S+) alt=(\S+)")
DART_RE = re.compile(r"^I1782 DART fixture=(\S+) window=(\S+) (\S+)(?: thrown=(\S+))? window=(\d+) published=(\S+)")


def log_mode(log, corner, ks):
    publish, flag = {}, {}
    for raw in open(log, "r", errors="replace"):
        m = PUBLISH_RE.search(raw)
        if m:
            publish[int(m.group(1))] = m
        m = FLAG_RE.search(raw)
        if m:
            flag[int(m.group(1))] = m
    darts = {}
    for raw in open(corner, "r", errors="replace"):
        m = DART_RE.search(raw)
        if m:
            darts[int(m.group(5))] = (m.group(1), m.group(2), m.group(3), m.group(4) or "UNCLAIMED")
    if not publish:
        return 2
    tally = collections.Counter()
    for w in sorted(publish):
        p = publish[w]
        f = flag.get(w)
        default = (f.group(4) == "1") if f else (p.group(2) == "1")
        if not default:
            continue
        z = float(p.group(6)) / float(p.group(7)) if float(p.group(7)) > 0 else -1.0
        fixture, window, visit, thrown = darts.get(w, ("-", "-", "-", "-"))
        right = thrown == p.group(9)
        flagged_now = p.group(2) == "1"
        print("I1815 FIXTURE %s %s %s window=%d thrown=%s published=%s alt=%s z=%.3f flagged=%d conf=%s %s" % (
            fixture, window, visit, w, thrown, p.group(9), (f.group(7) if f else p.group(4)), z,
            1 if flagged_now else 0, p.group(8), "right" if right else "WRONG"))
        tally["default_flagged"] += 1
        tally["flagged_" + ("right" if right else "wrong")] += flagged_now
        tally["cleared_" + ("right" if right else "wrong")] += not flagged_now
        for k in ks:
            tally["k%.2f_cut_%s" % (k, "right" if right else "wrong")] += z > k
    print("I1815 TALLY " + " ".join("%s=%d" % kv for kv in sorted(tally.items())))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--truth-line")
    ap.add_argument("--log")
    ap.add_argument("--corner")
    ap.add_argument("--k", type=float, action="append", default=[])
    a = ap.parse_args()
    if a.truth_line:
        return truth_mode(a.truth_line, a.k)
    if a.log and a.corner:
        return log_mode(a.log, a.corner, a.k)
    ap.error("--truth-line, or --log with --corner")
    return 2


if __name__ == "__main__":
    sys.exit(main())

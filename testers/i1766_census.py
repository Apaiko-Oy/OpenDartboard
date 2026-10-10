"""#1766: the two-line census -- what a solve of TWO lines claims across the wire it is
nearest to, where that claim comes from, and what the vote read on the same dart.

    python3 i1766_census.py --log <run.txt> [--log <run.txt> ...] [--min-two-line N]
    python3 i1766_census.py --live <darts-log.txt>

THE QUESTION THE SLICE ASKS. Live on 2026-10-08 eight thrown 19s published as S3/T3 from
two-line solves at claimed across-wire sigmas of 12.2-16.5 mm, where the same session's
three-line solves claim 5-6, and the issue's hypothesis was that the sigma is the RIG's --
the angle the two cameras' lines cross at on that wedge -- and not the dart's. A two-line
solve is a 2x2 system: its covariance is A^-1 diag(s1^2, s2^2) A^-T with A the two unit
normals, and nothing else goes in. So its error ellipse is a function of the two lines'
own sigmas (floored direction sigma over the lever, plus the lateral floor: 4-7 mm on
every fixture) and the crossing angle alone, and with equal sigmas s the major axis is
s / (sqrt(2) sin(pair/2)). This census recomputes that from each solve's own census
lines (I1512CAM's sigmaPerp, I1512ENTRY's pair) and prints the residual against the
sigma the solver claimed, so the claim is checked rather than believed.

WHAT IT REPORTS, per log and pooled, and decides none of it:
    TWO-LINE ROWS   one per two-line solve: the cameras, phi, radius, pair angle, the two
                    sigmaPerps, the claimed ellipse, the recomputed major axis and its
                    residual, the across-wire sigma and crossing, the geometry's score,
                    the vote's, the tips that corroborate, and whether OD_SOLVE_CONTROL=on
                    would have refused it (I1681CONTROL's refused=).
    SWITCH          how many two-line solves the control switch would send to the vote,
                    and on how many the vote's score differs from the geometry's -- the
                    measurement the "make it the default" decision needs.
    FORMULA         the largest residual between the claimed sigmaMajor and the one the
                    two lines' own numbers give. A solve whose sigma is not its lines'
                    would show here.

--live reads a LIVE log, which carries no census lines: it lists every "from 2
intersecting constraint(s)" publication with its UNCERTAINTY sentence, its BOARD line
and the LONE-WIRE line of the same event (the vote's lone reading, when there was one),
which is the whole of what the live log can say about those darts.

A reporter in i1556_census.py's mould: the exit status is about whether the logs could
be read, and the one assertion (--min-two-line) is a floor on how many two-line solves
were parsed, because a census of none has measured nothing (#1490).
"""

import argparse
import math
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
KV = re.compile(r"(\w+)=(\S+)")


def kv(line, upto):
    """The key=value head of a census line, up to (and excluding) the `upto=` field."""
    head = line.split(upto + "=")[0]
    return dict(KV.findall(head))


def two_line_sigma_major(s1, s2, pair_deg):
    """The major-axis sigma of a solve of exactly two lines whose normals cross at
    pair_deg, from the covariance A^-1 diag(s1^2, s2^2) A^-T. The crossing angle of the
    lines is the crossing angle of their normals."""
    t = math.radians(pair_deg)
    st = math.sin(t)
    if abs(st) < 1e-9:
        return float("inf")
    # A = [[1, 0], [cos t, sin t]]; A^-1 = (1/sin t) [[sin t, 0], [-cos t, 1]]
    a = [[1.0, 0.0], [-math.cos(t) / st, 1.0 / st]]
    # C = A^-1 S A^-T with S = diag(s1^2, s2^2)
    c00 = a[0][0] ** 2 * s1 ** 2 + a[0][1] ** 2 * s2 ** 2
    c01 = a[0][0] * a[1][0] * s1 ** 2 + a[0][1] * a[1][1] * s2 ** 2
    c11 = a[1][0] ** 2 * s1 ** 2 + a[1][1] ** 2 * s2 ** 2
    tr = c00 + c11
    det = c00 * c11 - c01 * c01
    disc = max(0.0, tr * tr / 4.0 - det)
    return math.sqrt(tr / 2.0 + math.sqrt(disc))


def read_run(path):
    lines = [ANSI.sub("", l.rstrip("\n")) for l in open(path, encoding="utf-8", errors="replace")]
    ent = [l for l in lines if "I1512ENTRY " in l]
    flg = [l for l in lines if "I1556FLAG " in l]
    ctl = [l for l in lines if "I1681CONTROL " in l]
    pub = [l for l in lines if "I1555PUBLISH " in l]
    cams = {}
    for l in lines:
        if "I1512CAM " in l:
            m = kv(l, "excl")
            cams.setdefault(m["window"], []).append(m)
    rows = []
    ok = len(ent) == len(flg) == len(ctl)
    if not ok:
        print("I1766 WARN %s: %d ENTRY, %d FLAG, %d CONTROL lines -- they are joined by window"
              % (path, len(ent), len(flg), len(ctl)))
    by_w = {}
    for l in flg:
        by_w.setdefault(kv(l, "refusal")["window"], {})["flag"] = kv(l, "refusal")
    for l in ctl:
        by_w.setdefault(kv(l, "story")["window"], {})["ctl"] = kv(l, "story")
    for l in pub:
        by_w.setdefault(dict(KV.findall(l))["window"], {})["pub"] = dict(KV.findall(l))
    solved = 0
    for e in ent:
        m = kv(e, "story")
        if m.get("solved") != "1":
            continue
        solved += 1
        if m.get("usable") != "2":
            continue
        w = m["window"]
        g = by_w.get(w, {}).get("flag", {})
        c = by_w.get(w, {}).get("ctl", {})
        p = by_w.get(w, {}).get("pub", {})
        used = [x for x in cams.get(w, []) if x["usable"] == "1" and x["excluded"] == "0"]
        if len(used) != 2:
            print("I1766 WARN %s window %s: a two-line solve with %d usable camera lines"
                  % (path, w, len(used)))
            continue
        s1, s2 = float(used[0]["sigmaPerp"]), float(used[1]["sigmaPerp"])
        major, minor = (float(v) for v in m["sigma"].split("/"))
        pair = float(m["pair"])
        pred = two_line_sigma_major(s1, s2, pair)
        rows.append({
            "log": path, "window": int(w), "cams": "+".join(x["cam"] for x in used),
            "phi": float(m["phi"]), "r": float(m["r"]), "pair": pair,
            "s1": s1, "s2": s2, "major": major, "minor": minor, "pred": pred,
            "resid": (major - pred) / pred if pred > 0 else float("nan"),
            "kind": g.get("kind", "-"), "boundary": float(g.get("boundary", -1)),
            "sigmaAcross": float(g.get("sigmaAcross", -1)), "z": float(g.get("z", -1)),
            "flag": g.get("flag", "-"), "geo": m["score"], "alt": g.get("alt", "-"),
            "vote": p.get("vote", m.get("published", "-")),
            "tips": c.get("tips", "-"), "nearestTip": c.get("nearestTip", "-"),
            "refused": c.get("refused", "-"), "redundancy": c.get("r", "-"),
        })
    return solved, rows


def print_rows(rows):
    print("I1766 TWO-LINE  window cams   phi      r   pair  sPerp1 sPerp2  claimed(maj/min)  "
          "recomputed  resid  kind   boundary sigmaAcross     z flag  geo    alt    vote   tips  nearestTip refused r=")
    for x in rows:
        print("I1766 TWO-LINE  %6d %-5s %6.1f %6.1f %5.1f  %6.2f %6.2f  %7.2f/%-7.2f  %9.2f %6.1f%%  %-5s %8.2f %11.2f %5.2f %-4s  %-6s %-6s %-6s %-5s %10s %7s %s"
              % (x["window"], x["cams"], x["phi"], x["r"], x["pair"], x["s1"], x["s2"],
                 x["major"], x["minor"], x["pred"], 100.0 * x["resid"], x["kind"],
                 x["boundary"], x["sigmaAcross"], x["z"], x["flag"], x["geo"], x["alt"],
                 x["vote"], x["tips"], x["nearestTip"], x["refused"], x["redundancy"]))


def summarise(label, solved, rows):
    n = len(rows)
    if n == 0:
        print("I1766 SUMMARY %s: solved=%d two-line=0" % (label, solved))
        return
    worst = max(abs(x["resid"]) for x in rows)
    refused = [x for x in rows if x["refused"] == "1"]
    disagree = [x for x in rows if x["vote"] != x["geo"]]
    wide = [x for x in rows if x["sigmaAcross"] >= 10.0]
    print("I1766 SUMMARY %s: solved=%d two-line=%d pair=%.1f..%.1f deg sigmaAcross=%.1f..%.1f mm "
          "(%d at or past 10 mm) formula-worst-resid=%.1f%% control-refused=%d "
          "vote-differs=%d (%s)"
          % (label, solved, n, min(x["pair"] for x in rows), max(x["pair"] for x in rows),
             min(x["sigmaAcross"] for x in rows), max(x["sigmaAcross"] for x in rows), len(wide),
             100.0 * worst, len(refused), len(disagree),
             ", ".join("w%d geo %s vote %s" % (x["window"], x["geo"], x["vote"]) for x in disagree) or "-"))
    print("I1766 SWITCH %s: OD_SOLVE_CONTROL=on would send %d of %d two-line solves to the vote; "
          "on %d of those the vote's score differs from the geometry's (%s)"
          % (label, len(refused), n, len([x for x in refused if x["vote"] != x["geo"]]),
             ", ".join("w%d geo %s vote %s" % (x["window"], x["geo"], x["vote"])
                       for x in refused if x["vote"] != x["geo"]) or "-"))


def live(path):
    lines = [ANSI.sub("", l.rstrip("\n")) for l in open(path, encoding="utf-8", errors="replace")]
    geo = re.compile(r"Geometric score: (\S+) from (\d+) intersecting constraint\(s\) of (\d+) cameras")
    board = re.compile(r"segment=(\d+) \| radius=([-0-9.]+) \| angle=([-0-9.]+)")
    count = 0
    lone = None
    unc = None
    print("I1766 LIVE line  score cams  angle   radius  wedge-wire-mm  UNCERTAINTY | LONE-WIRE (the vote's lone reading, same event)")
    for i, l in enumerate(lines):
        if "SCORER] - SCORE:" in l:
            lone = None
            unc = None
            continue
        if "LONE-WIRE:" in l:
            lone = l.split("LONE-WIRE: ", 1)[1]
        if "UNCERTAINTY:" in l:
            unc = l.split("UNCERTAINTY: ", 1)[1]
        m = geo.search(l)
        if not m or m.group(2) != "2":
            continue
        b = board.search(lines[i + 1]) if i + 1 < len(lines) else None
        if not b:
            continue
        count += 1
        angle = float(b.group(3))
        radius = float(b.group(2)) * 170.0
        d = (angle - 9.0) % 18.0
        d = min(d, 18.0 - d)
        wire_mm = radius * math.sin(math.radians(d))
        # i + 3 is the 1-based line number of the SCORE: line two below, which is how the
        # issue and docs/rig.md number the live publications.
        print("I1766 LIVE %5d  %-5s %s+?   %6.1f  %6.1f  %6.1f  %s | %s"
              % (i + 3, m.group(1), "2", angle, radius, wire_mm, unc or "-", lone or "-"))
    print("I1766 LIVE two-line publications: %d" % count)
    return count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", action="append", default=[])
    ap.add_argument("--live")
    ap.add_argument("--min-two-line", type=int, default=0)
    args = ap.parse_args()
    if args.live:
        return 0 if live(args.live) > 0 else 1
    if not args.log:
        ap.error("--log or --live")
    pooled = []
    total_solved = 0
    for path in args.log:
        solved, rows = read_run(path)
        print("=== %s: %d solved, %d two-line" % (path, solved, len(rows)))
        print_rows(rows)
        summarise(path, solved, rows)
        pooled += rows
        total_solved += solved
    if len(args.log) > 1:
        summarise("POOLED over %d log(s)" % len(args.log), total_solved, pooled)
    if len(pooled) < args.min_two_line:
        print("FAIL I1766: %d two-line solve(s) parsed, under the floor of %d"
              % (len(pooled), args.min_two_line))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

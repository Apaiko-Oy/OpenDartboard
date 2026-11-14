#!/usr/bin/env python3
"""turnaus#1993: the not-straight refusals, counted two ways before any switch.

  i1993_census.py --bakeoff <runs/.../1555>
      Over the six graded replays (the pin is the same footage with the geometry off):
        I1993 NEIGHBOUR  every not-straight camera-window, with the distance on the board
                         from its dart to the nearest EARLIER dart of the same visit. A
                         dart's place is its solved entry (I1512ENTRY x,y, mm); where
                         nothing solved, the published radius (I1911 r=) on the thrown
                         cell's middle angle; where neither, the middle of the thrown cell.
        I1993 ORDINAL    refusals per offered camera-window by the dart's place in its visit
        I1993 POLARITY   how many refused figures #1554's intensity test `applied` on
        I1993 PLANE-*    (only where the log carries `I1993 PLANE`, OD_PLANE_CENSUS=1) the
                         shadow signature on refused and on valid figures, and the retry

  i1993_census.py --live LOG [LOG ...]
      A shipped log carries no entry and no fresh change, so neither count above can be made
      on it. What it carries is each dart's place in its visit (SCORE lines between ENDs) and
      which cameras were refused not-straight (the PATH and LINES sentences):
        I1993 LIVE-ORDINAL  darts with a not-straight camera, by the dart's place in its visit

  i1993_census.py --self-test
"""
import math
import os
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
RUNS = ["r18-dev", "r18-open", "r22-dev", "r22-open", "r29-dev", "r29-open"]
SEQ = [20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5]
NEAR_MM = (15.0, 25.0, 40.0)
SIG_SHARE, SIG_EXTENT = 0.10, 40.0


def kv(line):
    return dict(m.groups() for m in re.finditer(r"(\w+)=(\S+)", line))


def cell_middle(thrown, radius_mm=None):
    """The middle of a thrown cell in sequence space (mm), and how it was placed."""
    m = re.match(r"^([SDT]?)(\d+)$", thrown)
    if not m:
        return None
    ring, n = m.group(1) or "S", int(m.group(2))
    if n in (25, 50):
        return (0.0, 0.0)
    if n not in SEQ:
        return None
    phi = math.radians((SEQ.index(n) + 0.5) * 18.0)
    r = radius_mm if radius_mm is not None else {"T": 103.0, "D": 166.0}.get(ring, 134.5)
    return (r * math.cos(phi), r * math.sin(phi))


def reasons_of(line):
    return {int(c): (r, float(rms)) for c, r, rms in re.findall(r"cam(\d)=([^,\s]+),rms=([\d.]+)", line)}


def read_run(root, run):
    """dart rows in visit order: {visit, ordinal, thrown, win, reasons{cam: (reason, rms)}, at, placed}"""
    log = {}
    plane = []
    for raw in open(os.path.join(root, "%s.txt" % run), errors="replace"):
        line = ANSI.sub("", raw)
        m = re.search(r"(I1911 REFUSALS|I1512ENTRY|I1511AXIS|I1993 PLANE) window=(\d+) ", line)
        if not m:
            continue
        w = log.setdefault(int(m.group(2)), {"reasons": {}, "entry": None, "r": None, "applied": {}})
        body = line[m.start():]
        if m.group(1) == "I1911 REFUSALS":
            w["reasons"] = reasons_of(body)
            f = kv(body)
            w["r"] = None if f.get("r", "-") == "-" else float(f["r"]) * 170.0
        elif m.group(1) == "I1512ENTRY":
            f = kv(body.split(" story=")[0])
            if f.get("solved") == "1":
                w["entry"] = (float(f["x"]), float(f["y"]))
        elif m.group(1) == "I1511AXIS":
            f = kv(body.split(" refusal=")[0])
            w["applied"][int(f["cam"])] = f.get("applied") == "1"
        else:
            f = kv(body)
            f["run"], f["window"] = run, int(m.group(2))
            plane.append(f)
    darts = []
    for line in open(os.path.join(root, "census-%s.txt" % run)):
        if "CAMERA-DART" not in line:
            continue
        f = kv(line)
        m = re.match(r"v(\d+)\.(\d+)", f.get("dart", ""))
        if not m or not f.get("win", "-").isdigit():
            continue
        w = log.get(int(f["win"]))
        if w is None:
            continue
        at, placed = w["entry"], "solved"
        if at is None:
            at, placed = cell_middle(f["thrown"], w["r"]), "radius" if w["r"] is not None else "cell"
        darts.append({"run": run, "visit": int(m.group(1)), "ordinal": int(m.group(2)), "dart": f["dart"],
                      "thrown": f["thrown"], "win": int(f["win"]), "reasons": w["reasons"],
                      "applied": w["applied"], "at": at, "placed": placed})
    return darts, plane


def nearest_earlier(d, darts):
    best = None
    for e in darts:
        if e["run"] == d["run"] and e["visit"] == d["visit"] and e["ordinal"] < d["ordinal"] and e["at"] and d["at"]:
            dist = math.hypot(e["at"][0] - d["at"][0], e["at"][1] - d["at"][1])
            best = dist if best is None or dist < best else best
    return best


def has_signature(f):
    px, co = int(f["px"]), int(f["co"])
    return px > 0 and co >= SIG_SHARE * px and float(f["ext"]) >= SIG_EXTENT


def bakeoff(root):
    darts, plane = [], []
    for run in RUNS:
        d, p = read_run(root, run)
        darts += d
        plane += p
    refused = []
    by_ordinal = {}
    for d in darts:
        near = nearest_earlier(d, darts)
        for cam, (reason, rms) in sorted(d["reasons"].items()):
            if reason in ("ok",) or "not-straight" in reason or "not-a-shaft" in reason:
                slot = by_ordinal.setdefault(min(d["ordinal"], 3), [0, 0])
                slot[0] += 1
                slot[1] += "not-straight" in reason
            if "not-straight" in reason:
                refused.append((d, cam, rms, near))
                print("I1993 NEIGHBOUR run=%s dart=%s win=%d cam=%d rms=%.2f ordinal=%d placed=%s nearest_mm=%s applied=%d" % (
                    d["run"], d["dart"], d["win"], cam, rms, d["ordinal"], d["placed"],
                    "-" if near is None else "%.1f" % near, 1 if d["applied"].get(cam) else 0))
    n = len(refused)
    print("I1993 NEIGHBOUR-TOTAL not_straight=%d no_earlier_dart=%d %s placed solved/radius/cell=%d/%d/%d" % (
        n, sum(1 for r in refused if r[3] is None),
        " ".join("within%d=%d" % (mm, sum(1 for r in refused if r[3] is not None and r[3] <= mm)) for mm in NEAR_MM),
        sum(1 for r in refused if r[0]["placed"] == "solved"), sum(1 for r in refused if r[0]["placed"] == "radius"),
        sum(1 for r in refused if r[0]["placed"] == "cell")))
    for run in RUNS:
        mine = [r for r in refused if r[0]["run"] == run]
        print("I1993 NEIGHBOUR-RUN run=%s not_straight=%d no_earlier_dart=%d %s" % (
            run, len(mine), sum(1 for r in mine if r[3] is None),
            " ".join("within%d=%d" % (mm, sum(1 for r in mine if r[3] is not None and r[3] <= mm)) for mm in NEAR_MM)))
    for o in sorted(by_ordinal):
        fitted, bent = by_ordinal[o]
        print("I1993 ORDINAL dart_of_visit=%d fitted=%d not_straight=%d (%.1f%%)" % (o, fitted, bent, 100.0 * bent / max(1, fitted)))
    print("I1993 POLARITY not_straight=%d applied=%d" % (n, sum(1 for r in refused if r[0]["applied"].get(r[1]))))
    if plane:
        plane_tally(plane)
    return 0


def plane_tally(plane):
    for word, pick in (("not-straight", lambda f: "not-straight" in f["reason"]), ("ok", lambda f: f["reason"] == "ok")):
        rows = [f for f in plane if pick(f) and int(f.get("others", "0")) > 0]
        shares = sorted(int(f["co"]) / max(1, int(f["px"])) for f in rows)
        exts = sorted(float(f["ext"]) for f in rows)
        med = lambda v: v[len(v) // 2] if v else 0.0
        print("I1993 PLANE-TALLY reason=%s n=%d signature=%d share_median=%.3f share_p90=%.3f ext_median=%.1f ext_p90=%.1f" % (
            word, len(rows), sum(1 for f in rows if has_signature(f)), med(shares),
            shares[int(len(shares) * 0.9)] if shares else 0.0, med(exts), exts[int(len(exts) * 0.9)] if exts else 0.0))
    tried = [f for f in plane if f.get("retry", "-") != "-"]
    print("I1993 PLANE-RETRY tried=%d pass=%d adopted=%d" % (
        len(tried), sum(1 for f in tried if f["retry"] == "pass"), sum(1 for f in tried if f.get("adopted") == "1")))
    for run in RUNS:
        mine = [f for f in tried if f["run"] == run]
        print("I1993 PLANE-RETRY-RUN run=%s tried=%d pass=%d adopted=%d%s" % (
            run, len(mine), sum(1 for f in mine if f["retry"] == "pass"), sum(1 for f in mine if f.get("adopted") == "1"),
            "".join(" w%d/c%s:%s->%s" % (f["window"], f["cam"], f["rms"], f["retry_rms"]) for f in mine if f["retry"] == "pass")))


def live_darts(lines):
    """[(place in visit, cameras refused not-straight)] off a shipped log's sentences."""
    out, ordinal, bent = [], 0, 0
    for raw in lines:
        line = ANSI.sub("", raw)
        if "PATH: DEGRADED" in line or "] - LINES:" in line or " - LINES:" in line:
            bent = max(bent, len(re.findall(r"cam \d: no usable axis: (?:not a shaft[^;]*; )?not straight", line)))
        elif "SCORE: END" in line:
            ordinal, bent = 0, 0
        elif "SCORE: " in line:
            ordinal += 1
            out.append((min(ordinal, 3), bent))
            bent = 0
    return out


def live(paths):
    for path in paths:
        darts = live_darts(open(path, errors="replace"))
        for o in (1, 2, 3):
            mine = [b for place, b in darts if place == o]
            print("I1993 LIVE-ORDINAL log=%s dart_of_visit=%d darts=%d with_not_straight=%d (%.1f%%) cameras_not_straight=%d" % (
                os.path.basename(os.path.dirname(path)) + "/" + os.path.basename(path), o, len(mine),
                sum(1 for b in mine if b), 100.0 * sum(1 for b in mine if b) / max(1, len(mine)), sum(mine)))
    return 0


def self_test():
    bad = 0

    def expect(what, got, want):
        nonlocal bad
        ok = got == want
        bad += not ok
        print("%s %s%s" % ("OK  " if ok else "FAIL", what, "" if ok else " got %r want %r" % (got, want)))

    at = cell_middle("T20")
    expect("cell: T20 sits 103 mm out at 9 degrees", (round(at[0], 1), round(at[1], 1)), (101.7, 16.1))
    at = cell_middle("S17", 62.57)
    expect("cell: S17 at a published radius", (round(at[0], 1), round(at[1], 1)), (-61.8, 9.8))
    expect("cell: the bull is the centre", cell_middle("D25"), (0.0, 0.0))
    expect("cell: a miss has no place", cell_middle("MISS"), None)
    expect("reasons", reasons_of("I1911 REFUSALS window=7 path=geometry score=S5 r=0.6605 gate=2.50 cam1=ok,rms=0.70,"
                                 "elong=17.41,scale=1.00 cam2=not-straight,rms=4.42,elong=11.33,scale=1.00"),
           {1: ("ok", 0.7), 2: ("not-straight", 4.42)})
    a = {"run": "r", "visit": 1, "ordinal": 1, "at": (0.0, 0.0)}
    b = {"run": "r", "visit": 1, "ordinal": 2, "at": (3.0, 4.0)}
    c = {"run": "r", "visit": 2, "ordinal": 1, "at": (3.0, 4.0)}
    expect("neighbour: the earlier dart of the visit", nearest_earlier(b, [a, b, c]), 5.0)
    expect("neighbour: a first dart has none", nearest_earlier(a, [a, b, c]), None)
    expect("neighbour: another visit's dart is not one", nearest_earlier(c, [a, b, c]), None)
    expect("signature: a tenth of the figure over 40 mm", has_signature({"px": "1000", "co": "100", "ext": "40.0"}), True)
    expect("signature: the tip's own crossing is not one", has_signature({"px": "1000", "co": "300", "ext": "24.0"}), False)
    expect("signature: a few far pixels are not one", has_signature({"px": "1000", "co": "99", "ext": "90.0"}), False)
    expect("live: place in the visit and the cameras refused", live_darts([
        "x - LINES: 2 of 3 cameras' lines solved this dart (cam 2: no usable axis: not straight: the kept",
        "x - SCORE: S5 | Position", "x - SCORE: T20 | Position",
        "x - PATH: DEGRADED -- (cam 1: no usable axis: not a shaft: extent; not straight: the; cam 3: no usable axis: not straight: x",
        "x - SCORE: S1 | Position", "x - SCORE: END | Position", "x - SCORE: S1 | Position"]),
           [(1, 1), (2, 0), (3, 2), (1, 0)])
    print("I1993 CENSUS SELF-TEST %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        sys.exit(self_test())
    if len(sys.argv) == 3 and sys.argv[1] == "--bakeoff":
        sys.exit(bakeoff(sys.argv[2]))
    if len(sys.argv) >= 3 and sys.argv[1] == "--live":
        sys.exit(live(sys.argv[2:]))
    print(__doc__)
    sys.exit(2)

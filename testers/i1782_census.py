# unrun-tester: a reporter over the #1555 bakeoff's logs (#1782), run by hand on a run directory; it asserts nothing and needs seven whole-clip replays as input. docs/rig.md records its count.
"""#1782: every flagged geometric dart, whether it was a CORNER, and what the corner offered.

    python3 i1782_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d]
    python3 i1782_census.py --log <run.txt> --truth <GROUND-TRUTH.md> --not-annotated \
        --fixture <name> --window <dev|opening>
    python3 i1782_census.py --pool <census output> [<census output> ...]

WHAT IT READS. A detector run made with OD_GEO_SCORE=on (every #1555 bakeoff log). Per
called dart the detector prints one I1782CORNER line: whether #1556 flagged it, whether the
solve was inside its across-wire sigma of BOTH a ring wire and a wedge wire (`corner=`),
#1556's nearest-wire alternative, what the corner offers (alternative first), the cells'
probabilities by the solve's covariance, and every voting camera's own reading as
cam<n>=<score>/<used by the solve>/<wedge margin>/<ring margin>/<clear of every wire>.
The path and the detection-to-throw join are i1555_census's, as in i1773_census.py.

WHAT IT PRINTS, per fixture and window:

    I1782 DART ...   one line per FLAGGED geometric publish: thrown, published, corner,
                     nearest_alt, offered, where the truth is (`published`, `nearest_alt`
                     -- across the wire the flag named, `other_cell` -- a corner cell across
                     the wire the flag did NOT name, or `outside`), and the unused cameras.
    I1782 TALLY ...  flagged, corner, corner_published_right, corner_truth_nearest,
                     corner_truth_other (the truth across the wire #1556 did not name),
                     corner_truth_outside, alt_right_nearest (#1556's alternative was the
                     truth), alt_right_corner (the corner's first alternative was the
                     truth), offered_right_corner (the truth anywhere in what the corner
                     offers), unused_reads (unused-camera readings on corners),
                     unused_corner_cell (of those, reading one of the four cells),
                     unused_corner_cell_true, unused_clear_cell (a clear reading of one of
                     the three other cells: rule 2 fires), unused_clear_cell_true, and
                     score_moved (a geometric publish whose published score differs from
                     the solve's: must be 0, the flag moves no score).

A reporter in i1773_census.py's mould: it decides nothing about the numbers. Exit 0 when
the run parsed and matched something, 2 when there was nothing to census.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402
import i1555_census as publish_census  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*m")
CORNER_RE = re.compile(
    r"I1782CORNER window=(-?\d+) flagged=(\d) corner=(\d) score=(\S+) nearest_alt=(\S+) offered=(\S+) "
    r"ring_mm=(\S+) wedge_mm=(\S+) sigma_ring=(\S+) sigma_wedge=(\S+) rho=(\S+) p_published=(\S+) "
    r"cells=(\S+)(.*)$")
CAM_RE = re.compile(r"cam(\d+)=(\S+?)/(\d)/(\S+?)/(\S+?)/(\d)")


def read_corner(path):
    out = {}
    for raw in open(path, "r", errors="replace"):
        m = CORNER_RE.search(ANSI.sub("", raw).rstrip())
        if not m:
            continue
        cells = []
        if m.group(13) != "-":
            for c in m.group(13).split(","):
                s, p = c.rsplit(":", 1)
                cells.append((s, float(p)))
        cams = [{"cam": int(c[0]), "score": c[1], "used": c[2] == "1", "wedge": float(c[3]),
                 "ring": float(c[4]), "clear": c[5] == "1"} for c in CAM_RE.findall(m.group(14))]
        out[int(m.group(1))] = {
            "flagged": m.group(2) == "1", "corner": m.group(3) == "1", "score": m.group(4),
            "nearest": m.group(5), "offered": [] if m.group(6) == "-" else m.group(6).split(","),
            "ring_mm": float(m.group(7)), "wedge_mm": float(m.group(8)),
            "sigma_ring": float(m.group(9)), "sigma_wedge": float(m.group(10)),
            "rho": float(m.group(11)), "p_published": float(m.group(12)), "cells": cells,
            "cams": cams,
        }
    return out


def same(a, b):
    return publish_census.norm(a) == publish_census.norm(b)


KEYS = ["flagged", "corner", "corner_published_right", "corner_truth_nearest", "corner_truth_other",
        "corner_truth_outside", "alt_right_nearest", "alt_right_corner", "offered_right_corner",
        "unused_reads", "unused_corner_cell", "unused_corner_cell_true", "unused_clear_cell",
        "unused_clear_cell_true", "score_moved"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None)
    ap.add_argument("--log")
    ap.add_argument("--truth")
    ap.add_argument("--annotations")
    ap.add_argument("--not-annotated", action="store_true")
    ap.add_argument("--fixture")
    ap.add_argument("--window", default="?")
    ap.add_argument("--no-arrival", default="")
    args = ap.parse_args()
    if args.pool:
        return pool(args.pool)
    if bool(args.annotations) == args.not_annotated:
        ap.error("give exactly one of --annotations and --not-annotated")

    no_arrival = set()
    for token in args.no_arrival.split(","):
        if token.strip():
            v, d = token.strip().split(".")
            no_arrival.add((int(v), int(d)))
    truth = axis_census.read_truth(args.truth) or publish_census.read_short_truth(args.truth) or []
    if args.not_annotated:
        annots = dict(((v + 1, d + 1), {0: {"line": None, "tip": None, "thrown": t,
                                            "note": "not annotated"}})
                      for v, visit in enumerate(truth) for d, t in enumerate(visit))
    else:
        annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    pubs, _ = publish_census.read_publish_blocks(args.log)
    corners = read_corner(args.log)
    tag = "fixture=%s window=%s" % (args.fixture, args.window)
    if not corners:
        print("I1782 CENSUS %s: no I1782CORNER lines in %s -- OD_GEO_SCORE=on, and a build "
              "with #1782 in it?" % (tag, args.log))
        return 2
    flat = [ev for visit in visits for ev in visit]
    for ev in flat:
        ev.pub = None
    for ev, block in zip(flat, pubs):
        ev.pub = block
    if args.not_annotated:
        assignment, why = publish_census.ordinal_join(visits, truth)
        if assignment is None:
            print("I1782 JOIN-REFUSED %s: %s" % (tag, why))
            return 2
    else:
        assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]

    t = dict((k, 0) for k in KEYS)
    matched = 0
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            pub = getattr(ev, "pub", None)
            if pub is None:
                continue
            key = assigned.get((v, ei))
            if key is not None:
                matched += 1
            if pub["path"] != "geometry":
                continue
            c = corners.get(pub["window"])
            if c is None:
                continue
            if not same(pub["score"], c["score"]):
                t["score_moved"] += 1
            if not c["flagged"]:
                continue
            t["flagged"] += 1
            thrown = list(annots[key].values())[0]["thrown"] if key is not None else None
            label = ("v%d.%d thrown=%s" % (key[0], key[1], publish_census.norm(thrown))
                     if key is not None else "UNCLAIMED v%d#%d" % (v + 1, ei + 1))
            cellnames = [s for s, _ in c["cells"]]
            where = "-"
            if thrown:
                if same(c["score"], thrown):
                    where = "published"
                elif same(c["nearest"], thrown):
                    where = "nearest_alt"
                elif c["corner"] and any(same(s, thrown) for s in cellnames):
                    where = "other_cell"
                else:
                    where = "outside"
            unused = [x for x in c["cams"] if not x["used"]]
            print("I1782 DART %s %s window=%d published=%s corner=%d nearest_alt=%s offered=%s "
                  "ring_mm=%.2f/%.2f wedge_mm=%.2f/%.2f rho=%.2f cells=%s truth_at=%s unused=%s"
                  % (tag, label, pub["window"], c["score"], c["corner"], c["nearest"],
                     ",".join(c["offered"]) or "-", c["ring_mm"], c["sigma_ring"], c["wedge_mm"],
                     c["sigma_wedge"], c["rho"],
                     ",".join("%s:%.2f" % sp for sp in c["cells"]) if c["corner"] else "-", where,
                     ",".join("cam%d:%s%s" % (x["cam"], x["score"], "(clear)" if x["clear"] else "")
                              for x in unused) or "-"))
            if thrown and same(c["nearest"], thrown):
                t["alt_right_nearest"] += 1
            if not c["corner"]:
                continue
            t["corner"] += 1
            if thrown:
                t["corner_published_right"] += where == "published"
                t["corner_truth_nearest"] += where == "nearest_alt"
                t["corner_truth_other"] += where == "other_cell"
                t["corner_truth_outside"] += where == "outside"
                t["alt_right_corner"] += bool(c["offered"]) and same(c["offered"][0], thrown)
                t["offered_right_corner"] += any(same(o, thrown) for o in c["offered"])
            four = [c["score"]] + cellnames
            for x in unused:
                t["unused_reads"] += 1
                in_corner = any(same(x["score"], s) for s in four)
                t["unused_corner_cell"] += in_corner
                if thrown and in_corner and same(x["score"], thrown):
                    t["unused_corner_cell_true"] += 1
                if x["clear"] and any(same(x["score"], s) for s in cellnames):
                    t["unused_clear_cell"] += 1
                    if thrown and same(x["score"], thrown):
                        t["unused_clear_cell_true"] += 1
    print("I1782 TALLY %s matched=%d %s" % (tag, matched, " ".join("%s=%d" % (k, t[k]) for k in KEYS)))
    return 0 if matched else 2


def pool(files):
    tot = {}
    for f in files:
        for line in open(f, errors="replace"):
            if line.startswith("I1782 TALLY "):
                for k, v in re.findall(r"(\w+)=(\d+)", line.split("window=", 1)[1]):
                    tot[k] = tot.get(k, 0) + int(v)
    print("I1782 POOLED over %d census(es): %s" % (len(files), " ".join("%s=%d" % kv for kv in tot.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

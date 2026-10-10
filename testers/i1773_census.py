"""#1773: every vote-path dart's distance to its nearest RING wire, and what the ring flag did.

    python3 i1773_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d]
    python3 i1773_census.py --log <run.txt> --truth <GROUND-TRUTH.md> --not-annotated \
        --fixture <name> --window <dev|opening>
    python3 i1773_census.py --pool <census output> [<census output> ...]

WHAT IT READS. A detector run made with OD_GEO_SCORE=on (the census pin every #1555 run
already carries, so the bakeoff's seven logs are the input). Per called dart the detector
prints one I1773RING line: the vote's chosen reading, its radius in board mm by its own
ruler, the nearer of its ring's two wires, the margin to it, #1628's wedge margin of the
same reading, and whether the ring check found it inside the sigma and flagged it, with
the alternative and the confidence that publishes. The path (vote or geometry) is
I1555PUBLISH's, read from the same log for the same window; the detection-to-throw join
is i1555_census's (i1511's spatial matcher where the fixture is annotated, the ordinal
join where it is not), so this census and #1555's cannot disagree about which detection
was which dart. Nothing of either is edited here.

WHAT IT PRINTS, per fixture and window:

    I1773 DART v<v>.<d> thrown=<s> window=<n> path=<vote|geometry> cam=<n> agreeing=<n>
               score=<s> ring=<r> radius_mm=<mm> wire_mm=<mm> margin=<mm> wedge_margin=<mm>
               near=<0|1> flagged=<0|1> alt=<s> conf=<c> verdict=<exact|ring|wedge|...>
               alt_verdict=<...>
        one line per dart the VOTE published whose reading measured a radius (the only
        darts the rule can act on). `verdict` judges the published score against the
        throw; `alt_verdict` judges the alternative, so a reader can see whether the
        flag offered the right score.
    I1773 UNCLAIMED v<v>#<e> ... the same for a detection the matcher gave to no throw
    I1773 TALLY fixture=... window=... matched=<n> vote=<n> checked=<n> near=<n>
               flagged=<n> unnameable=<n> near_right=<n> near_wrong=<n>
               flagged_right=<n> flagged_wrong=<n> flagged_wrong_alt_right=<n>
               clear_wrong=<n> score_moved=<n>
        `score_moved` counts a vote-path dart whose published score differs from the
        vote's string (I1555PUBLISH score= against vote=): the acceptance criterion is
        that it is 0 on every run, because the flag adds an alternative and moves no
        score. `near_wrong` is how many readings inside the sigma are wrong today;
        `flagged_wrong_alt_right` how many of those the alternative would have fixed.

A reporter in i1628_census.py's mould: it decides nothing about the numbers. Exit 0 when
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
RING_RE = re.compile(
    r"I1773RING window=(-?\d+) agreeing=(\d+) cam=(-?\d+) score=(\S+) ring=(\S+) "
    r"radius_mm=(\S+) wire_mm=(\S+) margin=(\S+) wedge_margin=(\S+) checked=(\d) near=(\d) "
    r"flagged=(\d) alt=(\S+) conf=(\S+)")


def read_ring(path):
    out = {}
    for raw in open(path, "r", errors="replace"):
        m = RING_RE.search(ANSI.sub("", raw))
        if not m:
            continue
        out[int(m.group(1))] = {
            "agreeing": int(m.group(2)), "cam": int(m.group(3)), "score": m.group(4),
            "ring": m.group(5), "radius_mm": float(m.group(6)), "wire_mm": float(m.group(7)),
            "margin": float(m.group(8)), "wedge_margin": float(m.group(9)),
            "checked": m.group(10) == "1", "near": m.group(11) == "1",
            "flagged": m.group(12) == "1", "alt": m.group(13), "conf": float(m.group(14)),
        }
    return out


def good(got, thrown):
    return publish_census.verdict(got, thrown) == "exact"


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
    ring = read_ring(args.log)
    tag = "fixture=%s window=%s" % (args.fixture, args.window)
    if not ring:
        print("I1773 CENSUS %s: no I1773RING lines in %s -- OD_GEO_SCORE=on, and a build "
              "with #1773 in it?" % (tag, args.log))
        return 2
    flat = [ev for visit in visits for ev in visit]
    for ev in flat:
        ev.pub = None
    for ev, block in zip(flat, pubs):
        ev.pub = block
    if args.not_annotated:
        assignment, why = publish_census.ordinal_join(visits, truth)
        if assignment is None:
            print("I1773 JOIN-REFUSED %s: %s" % (tag, why))
            return 2
    else:
        assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]

    t = dict(vote=0, checked=0, near=0, flagged=0, unnameable=0, near_right=0, near_wrong=0,
             flagged_right=0, flagged_wrong=0, flagged_wrong_alt_right=0, clear_wrong=0,
             score_moved=0)
    matched = 0
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            pub = getattr(ev, "pub", None)
            if pub is None:
                continue
            key = assigned.get((v, ei))
            if key is not None:
                matched += 1
            if pub["path"] != "vote":
                continue
            t["vote"] += 1
            if publish_census.norm(pub["score"]) != publish_census.norm(pub["vote"]):
                t["score_moved"] += 1
            rg = ring.get(pub["window"])
            if rg is None or not rg["checked"]:
                continue
            t["checked"] += 1
            thrown = None
            if key is not None:
                thrown = list(annots[key].values())[0]["thrown"]
            label = ("v%d.%d thrown=%s" % (key[0], key[1], publish_census.norm(thrown))
                     if key is not None else "UNCLAIMED v%d#%d" % (v + 1, ei + 1))
            verdict = publish_census.verdict(pub["score"], thrown) if thrown else "-"
            alt_verdict = (publish_census.verdict(rg["alt"], thrown)
                           if thrown and rg["alt"] != "-" else "-")
            print("I1773 DART %s %s window=%d path=%s cam=%d agreeing=%d score=%s ring=%s "
                  "radius_mm=%.2f wire_mm=%.2f margin=%.2f wedge_margin=%.2f near=%d flagged=%d "
                  "alt=%s conf=%.2f verdict=%s alt_verdict=%s"
                  % (tag, label, pub["window"], pub["path"], rg["cam"], rg["agreeing"],
                     rg["score"], rg["ring"], rg["radius_mm"], rg["wire_mm"], rg["margin"],
                     rg["wedge_margin"], rg["near"], rg["flagged"], rg["alt"], rg["conf"],
                     verdict, alt_verdict))
            right = good(pub["score"], thrown) if thrown else None
            if rg["near"]:
                t["near"] += 1
                if rg["flagged"]:
                    t["flagged"] += 1
                else:
                    t["unnameable"] += 1
                if right is True:
                    t["near_right"] += 1
                    if rg["flagged"]:
                        t["flagged_right"] += 1
                elif right is False:
                    t["near_wrong"] += 1
                    if rg["flagged"]:
                        t["flagged_wrong"] += 1
                        if good(rg["alt"], thrown):
                            t["flagged_wrong_alt_right"] += 1
            elif right is False:
                t["clear_wrong"] += 1
    print("I1773 TALLY %s matched=%d %s" % (tag, matched,
                                           " ".join("%s=%d" % kv for kv in t.items())))
    return 0 if matched else 2


def pool(files):
    tot = {}
    for f in files:
        for line in open(f, errors="replace"):
            if line.startswith("I1773 TALLY "):
                for k, v in re.findall(r"(\w+)=(\d+)", line.split("window=", 1)[1]):
                    tot[k] = tot.get(k, 0) + int(v)
    print("I1773 POOLED over %d census(es): %s" % (len(files), " ".join(
        "%s=%d" % kv for kv in tot.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

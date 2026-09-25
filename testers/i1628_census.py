"""#1628: a lone reading that sits on a wedge wire -- what the wire check did, per dart.

    python3 i1628_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d]
    python3 i1628_census.py --pool <census output> [<census output> ...]

WHAT IT READS. A detector run made with OD_GEO_SCORE=on (the census pin every #1555 run
already carries). Per called dart the detector prints one I1628LONE line: every camera's
reading, its wedge-wire margin in board mm by its own rulers (-1 where it measured no
wedge), whether its rings are complete and whether it voted; which camera #1517's
fallback chose, which camera publishes, and whether the fallback's reading was near a
wire and was replaced. The detection-to-throw join is i1555_census's, which is #1504's
spatial matcher imported from i1511_census, so this census and #1555's cannot disagree
about which detection was which dart. Nothing of either is edited here.

WHAT IT PRINTS, per fixture and window:

    I1628 DART v<v>.<d> thrown=<s> path=<vote|geometry> fallback=<cam>:<score> margin=<mm>
               published=<cam>:<score> reselected=<0|1> before=<verdict> after=<verdict> cams...
        one line per MATCHED dart where no two cameras agreed and the fallback's reading
        measured a wedge (the only darts the rule can ask about). `before` is what #1517's
        fallback would have published on this path, `after` is what did.
    I1628 UNCLAIMED v<v>#<e> ... the same for a detection the matcher gave to no throw
    I1628 TALLY fixture=... window=... checked=<n> near=<n> reselected=<n>
               reselected_on_vote=<n> wrong_to_right=<n> right_to_wrong=<n>
               wrong_to_wrong=<n> right_to_right=<n> unclaimed_reselected=<n>
               near_stood_on_vote=<n> near_stood_wrong=<n>
        only a reselection on a dart the VOTE published changes what the board says; a
        reselection behind a geometric publish is counted and changes nothing.
    I1628 SWEEP sigma=<mm> changed=<n> wrong_to_right=<n> right_to_wrong=<n> ...
        the rule recomputed offline from the same lines at other sigmas, so the choice of
        5.0 mm can be seen not to sit on a knife edge.

THE PREDICTION, STATED BEFORE THE RULE WAS FIRST RUN (committed ahead of the run):

    1. rig-20260922 dev, v7.2 (thrown S3), OD_LOOK_BUDGET=1605 + OD_SEEK_ALIGN=1618:
       the geometric entry refused it (TOO-FEW-CONSTRAINTS, three "not straight" axes),
       the vote had no consensus, and the I1628LONE line shows camera 1 reading S19 at a
       wedge margin under 1 mm, camera 2 reading S3 with a margin over 5 mm, and camera 3
       not voting (its tip is ~285 mm out, a MISS). The rule publishes camera 2's S3.
    2. With OD_LONE_WIRE=index on the same binary, v7.2 publishes S19 again.
    3. The rule's effect on every other dart is NOT predicted to be nil, and that is the
       thing to measure: #1618's logs hold 16 lone vote publishes whose chosen reading is
       within 5 mm of SOME wire, most of them correct, and the alternatives' readings are
       not in any log yet. The at-risk dart named in advance is rig-20260922 OPENING v7.2
       itself: camera 1 reads the correct S3 there only 0.9 mm from the same wire, and
       camera 2 read something other than S3 (no consensus formed). If camera 2's
       opening reading is clear of its wires, the rule turns a correct dart wrong and it
       is refused.

A reporter in i1555_census.py's mould: it decides nothing about the numbers. Exit 0 when
the run parsed and matched something, 2 when there was nothing to census.
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402
import i1555_census as publish_census  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LONE_RE = re.compile(
    r"I1628LONE window=(\d+) agreeing=(\d+) fallback=(-?\d+) published=(-?\d+) near=(\d) "
    r"reselected=(\d) margin=(\S+)((?: cam\d=\S+)*)")
CAM_RE = re.compile(r"cam(\d)=([^/]+)/([-0-9.]+)/(\d)/(\d)")
SIGMA = 5.0
SWEEP = (2.5, 5.0, 7.5, 10.0)


def read_lone(path):
    out = {}
    for raw in open(path, "r", errors="replace"):
        m = LONE_RE.search(ANSI.sub("", raw))
        if not m:
            continue
        cams = {}
        for c in CAM_RE.finditer(m.group(8)):
            cams[int(c.group(1))] = {"score": c.group(2), "margin": float(c.group(3)),
                                    "complete": c.group(4) == "1", "votes": c.group(5) == "1"}
        out[int(m.group(1))] = {
            "agreeing": int(m.group(2)), "fallback": int(m.group(3)),
            "published": int(m.group(4)), "near": m.group(5) == "1",
            "reselected": m.group(6) == "1", "margin": float(m.group(7)), "cams": cams,
            "raw": m.group(8).strip(),
        }
    return out


def rule(lone, sigma):
    """The header's checkLoneReadingAgainstWires, recomputed from the census line."""
    if lone["agreeing"] != 1 or lone["fallback"] < 1:
        return lone["fallback"]
    fb = lone["cams"].get(lone["fallback"])
    if fb is None or fb["margin"] < 0 or fb["margin"] >= sigma:
        return lone["fallback"]
    best, best_m = lone["fallback"], -1.0
    for k, c in sorted(lone["cams"].items()):
        if k == lone["fallback"] or not c["votes"] or c["score"] == fb["score"]:
            continue
        if fb["complete"] and not c["complete"]:
            continue
        if c["margin"] >= sigma and c["margin"] > best_m:
            best, best_m = k, c["margin"]
    return best


def good(got, thrown):
    return publish_census.verdict(got, thrown) == "exact"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None)
    ap.add_argument("--log")
    ap.add_argument("--truth")
    ap.add_argument("--annotations")
    ap.add_argument("--fixture")
    ap.add_argument("--window", default="?")
    ap.add_argument("--no-arrival", default="")
    args = ap.parse_args()
    if args.pool:
        return pool(args.pool)

    no_arrival = set()
    for token in args.no_arrival.split(","):
        if token.strip():
            v, d = token.strip().split(".")
            no_arrival.add((int(v), int(d)))
    annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    pubs, _ = publish_census.read_publish_blocks(args.log)
    lone = read_lone(args.log)
    tag = "fixture=%s window=%s" % (args.fixture, args.window)
    if not lone:
        print("I1628 CENSUS %s: no I1628LONE lines in %s -- OD_GEO_SCORE=on, and a build "
              "with #1628 in it?" % (tag, args.log))
        return 2
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, pubs):
        ev.pub = block
    assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]

    t = dict(checked=0, near=0, reselected=0, reselected_on_vote=0, wrong_to_right=0,
             right_to_wrong=0, wrong_to_wrong=0, right_to_right=0, unclaimed_reselected=0,
             near_stood_on_vote=0, near_stood_wrong=0)
    sweep = {s: dict(changed=0, wrong_to_right=0, right_to_wrong=0, wrong_to_wrong=0,
                     unclaimed_changed=0) for s in SWEEP}
    matched = 0
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            pub = getattr(ev, "pub", None)
            if pub is None:
                continue
            ln = lone.get(pub["window"])
            key = assigned.get((v, ei))
            if key is not None:
                matched += 1
            if ln is None or ln["agreeing"] != 1 or ln["fallback"] < 1:
                continue
            fb = ln["cams"].get(ln["fallback"])
            if fb is None or fb["margin"] < 0:
                continue
            t["checked"] += 1
            t["near"] += ln["near"]
            t["reselected"] += ln["reselected"]
            on_vote = pub["path"] == "vote"
            before = fb["score"] if on_vote else pub["score"]
            after = pub["score"]
            thrown = None
            if key is not None:
                thrown = list(annots[key].values())[0]["thrown"]
            label = ("v%d.%d thrown=%s" % (key[0], key[1], publish_census.norm(thrown))
                     if key is not None else "UNCLAIMED v%d#%d" % (v + 1, ei + 1))
            pubcam = ln["published"]
            print("I1628 DART %s %s window=%d path=%s fallback=cam%d:%s margin=%.2f "
                  "published=cam%d:%s reselected=%d near=%d before=%s after=%s | %s"
                  % (tag, label, pub["window"], pub["path"], ln["fallback"], fb["score"],
                     fb["margin"], pubcam, after, ln["reselected"], ln["near"],
                     publish_census.verdict(before, thrown) if thrown else "-",
                     publish_census.verdict(after, thrown) if thrown else "-", ln["raw"]))
            if on_vote and ln["near"] and not ln["reselected"]:
                t["near_stood_on_vote"] += 1
                if thrown is not None and not good(after, thrown):
                    t["near_stood_wrong"] += 1
            if ln["reselected"] and on_vote:
                t["reselected_on_vote"] += 1
                if thrown is None:
                    t["unclaimed_reselected"] += 1
                else:
                    b, a = good(before, thrown), good(after, thrown)
                    t[("right" if b else "wrong") + "_to_" + ("right" if a else "wrong")] += 1
            if on_vote:
                for s in SWEEP:
                    cam = rule(ln, s)
                    if cam == ln["fallback"]:
                        continue
                    new = ln["cams"][cam]["score"]
                    sweep[s]["changed"] += 1
                    if thrown is None:
                        sweep[s]["unclaimed_changed"] += 1
                        continue
                    b, a = good(fb["score"], thrown), good(new, thrown)
                    if a and not b:
                        sweep[s]["wrong_to_right"] += 1
                    elif b and not a:
                        sweep[s]["right_to_wrong"] += 1
                    elif not a and not b:
                        sweep[s]["wrong_to_wrong"] += 1
    print("I1628 TALLY %s matched=%d %s" % (tag, matched,
                                           " ".join("%s=%d" % kv for kv in t.items())))
    for s in SWEEP:
        print("I1628 SWEEP %s sigma=%.1f %s" % (tag, s, " ".join("%s=%d" % kv
                                                                 for kv in sweep[s].items())))
    return 0 if matched else 2


def pool(files):
    tot, sw = {}, {}
    for f in files:
        for line in open(f, errors="replace"):
            if line.startswith("I1628 TALLY "):
                for k, v in re.findall(r"(\w+)=(\d+)", line.split("window=", 1)[1]):
                    tot[k] = tot.get(k, 0) + int(v)
            m = re.match(r"I1628 SWEEP .* sigma=(\S+) (.*)", line)
            if m:
                d = sw.setdefault(m.group(1), {})
                for k, v in re.findall(r"(\w+)=(\d+)", m.group(2)):
                    d[k] = d.get(k, 0) + int(v)
    print("I1628 POOLED over %d census(es): %s" % (len(files), " ".join(
        "%s=%d" % kv for kv in tot.items())))
    for s, d in sorted(sw.items(), key=lambda kv: float(kv[0])):
        print("I1628 POOLED-SWEEP sigma=%s %s" % (s, " ".join("%s=%d" % kv for kv in d.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""#1555: the two scoring paths side by side, over one fixture in one calibration window,
against that fixture's ground truth -- and the pooled tally the publish decision is taken
on.

    python3 i1555_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d] [--tally <file>]
    python3 i1555_census.py --pool <tally file> [<tally file> ...]

WHAT IT READS. One detector run made with OD_GEO_SCORE=on AND OD_SHAFT_CENSUS=1. The
I1511AXIS lines are what #1504's spatial matcher aligns detections to annotated throws
with -- imported from i1511_census rather than re-derived, so no two censuses in this
repository can disagree about which detection was which dart -- and the I1555PUBLISH line
carries, per called dart, what the VOTE said, what the GEOMETRY said, and what the board
really published.

WHAT IT COUNTS, and the denominators are the point. Four columns, because "which path is
more accurate" is four different questions and three of them have different denominators:

    VOTE           the string vote's exact score, over every MATCHED event.
    GEOMETRY-ONLY  the solver's exact score over the events it SOLVED -- a number about
                   the instrument, on its own denominator, and not comparable with the
                   others because the solver chooses which darts it answers about.
    GEOMETRY-FIRST the publishable composite: the geometry where it solved and the vote
                   where it refused, over every matched event. THIS is the column the
                   publish decision is taken on, because it is the only geometric number
                   with the vote's own denominator. A path that refuses half the clip is
                   not more accurate for refusing.
    PUBLISHED      what the binary under test really published, over every matched event.
                   A cross-check, not a fifth opinion: it must equal GEOMETRY-FIRST on a
                   wired build and VOTE under OD_SCORE_PATH=vote, and a disagreement means
                   the wiring is not the decision.

Errors are split the way mocks/rig-20260918/GROUND-TRUTH.md splits them, because they are
different faults: a RING error is the right number in the wrong band (T13 read S13), a
WEDGE error is a different number entirely, a PHANTOM is a score published where a miss
was thrown, and a SILENCE is a MISS published where a score was thrown.

Segmentation is counted apart from both (#1552): a visit the detector merged into its
neighbour is neither a scoring error nor a detection failure, and pooling it with either
is how a scoring census ends up measuring a takeout.

A reporter in i1510p2_census.py's and i1512_census.py's mould: it decides nothing about
the numbers, and the exit status is about whether the run could be read at all.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402  (the matcher, #1504's alignment)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
PUBLISH_RE = re.compile(
    r"I1555PUBLISH window=(-?\d+) path=(\S+) score=(\S+) conf=([-0-9.]+) degraded=(\d) "
    r"outcome=(\S+) vote=(\S+) voteConf=([-0-9.]+) geo=(\S+)"
)
END_RE = re.compile(r"SCORE:\s+END\b")
# #1512's own line, read here for ONE field: how many placed tips corroborated the solve.
# It is printed by the same run, immediately before the publish line for the same dart,
# and it is what the "refuse an uncorroborated solve" counterfactual below is measured on.
TIPS_RE = re.compile(r"I1512ENTRY .* tips=(\d+)/(\d+) ")


def norm(score):
    """The published vocabulary, normalised so `19` and `S19` are one thing."""
    s = (score or "").upper()
    if s in ("MISS", "BULL", "DBULL", "OUTER", "NONE", ""):
        return {"DBULL": "BULL"}.get(s, s)
    return s if s[0] in "TDS" else "S" + s


def parts(score):
    """(ring, segment) of a normalised score; segment None where it has none."""
    s = norm(score)
    if s in ("MISS", "BULL", "OUTER", "NONE", ""):
        return (s.lower(), None)
    return ({"S": "single", "D": "double", "T": "triple"}[s[0]], s[1:])


def verdict(got, thrown):
    """How `got` is wrong about `thrown`, in GROUND-TRUTH.md's own vocabulary."""
    g, t = norm(got), norm(thrown)
    if g == t:
        return "exact"
    gr, gs = parts(g)
    tr, ts = parts(t)
    if t == "MISS":
        return "phantom"
    if g == "MISS":
        return "silence"
    if gs is not None and ts is not None and gs == ts:
        return "ring"
    if gs is not None and ts is not None:
        return "wedge"
    return "other"


ORDER = ["exact", "ring", "wedge", "phantom", "silence", "other", "refused"]


def tallyline(name, counts, denominator):
    body = " ".join("%s=%d" % (k, counts.get(k, 0)) for k in ORDER if counts.get(k, 0))
    return "%-14s exact %d/%d (%s) | %s" % (
        name, counts.get("exact", 0), denominator,
        "%.0f%%" % (100.0 * counts.get("exact", 0) / denominator) if denominator else "n/a",
        body or "nothing counted")


def read_publish_blocks(path):
    """One I1555PUBLISH dict per called dart, in arrival order, plus the END count.

    Bound the way i1512_census binds its entry blocks: the k-th non-END SCORE line of
    the log is the k-th event, and the publish line for that dart is printed before it.
    """
    blocks, pending, ends = [], None, 0
    tips = (0, 0)
    for raw in open(path, "r", errors="replace"):
        line = ANSI.sub("", raw)
        t = TIPS_RE.search(line)
        if t:
            tips = (int(t.group(1)), int(t.group(2)))
            continue
        m = PUBLISH_RE.search(line)
        if m:
            pending = {
                "window": int(m.group(1)), "path": m.group(2), "score": m.group(3),
                "conf": float(m.group(4)), "degraded": m.group(5) == "1",
                "outcome": m.group(6), "vote": m.group(7),
                "voteConf": float(m.group(8)), "geo": m.group(9),
                "tipsAgree": tips[0], "tipsSeen": tips[1],
            }
            tips = (0, 0)
            continue
        if END_RE.search(line):
            ends += 1
            continue
        sm = axis_census.SCORE_RE.search(line)
        if sm and sm.group(1) != "END":
            blocks.append(pending)
            pending = None
    return blocks, ends


def med(xs):
    return sorted(xs)[len(xs) // 2] if xs else float("nan")


def pool(files):
    """Sum the per-run TALLY lines into the pooled figure the decision is taken on."""
    rows = []
    for path in files:
        for raw in open(path, "r", errors="replace"):
            if "I1555 TALLY " not in raw:
                continue
            fields = {}
            for token in ANSI.sub("", raw).split("I1555 TALLY ", 1)[1].split():
                if "=" in token:
                    k, v = token.split("=", 1)
                    fields[k] = v
            rows.append(fields)
    if not rows:
        print("I1555 POOL: no TALLY lines in %s -- nothing to pool" % ", ".join(files))
        return 2
    keys = ["matched", "vote_exact", "geo_solved", "geo_exact", "first_exact",
            "published_exact"]
    total = dict((k, 0) for k in keys)
    for row in rows:
        print("I1555 POOL-ROW fixture=%s window=%s %s"
              % (row.get("fixture", "?"), row.get("window", "?"),
                 " ".join("%s=%s" % (k, row.get(k, "0")) for k in keys)))
        for k in keys:
            total[k] += int(row.get(k, 0))
    n = total["matched"]
    def pct(x):
        return "%.1f%%" % (100.0 * x / n) if n else "n/a"
    print("I1555 POOLED over %d run(s), %d matched darts: vote %d/%d (%s) | "
          "geometry-first %d/%d (%s) | published %d/%d (%s) | geometry solved %d/%d"
          % (len(rows), n, total["vote_exact"], n, pct(total["vote_exact"]),
             total["first_exact"], n, pct(total["first_exact"]),
             total["published_exact"], n, pct(total["published_exact"]),
             total["geo_solved"], n))
    margin = total["first_exact"] - total["vote_exact"]
    print("I1555 VERDICT pooled margin geometry-first minus vote = %+d dart(s) of %d"
          % (margin, n))
    # Per fixture, because a split verdict is the maintainer's call and a pooled number
    # hides it by construction.
    by_fixture = {}
    for row in rows:
        f = row.get("fixture", "?")
        agg = by_fixture.setdefault(f, dict((k, 0) for k in keys))
        for k in keys:
            agg[k] += int(row.get(k, 0))
    for f in sorted(by_fixture):
        agg = by_fixture[f]
        d = agg["first_exact"] - agg["vote_exact"]
        print("I1555 VERDICT-BY-FIXTURE %s: vote %d/%d, geometry-first %d/%d, margin %+d"
              % (f, agg["vote_exact"], agg["matched"], agg["first_exact"],
                 agg["matched"], d))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None,
                    help="sum the TALLY lines of these census outputs and stop")
    ap.add_argument("--log")
    ap.add_argument("--truth")
    ap.add_argument("--annotations")
    ap.add_argument("--fixture")
    ap.add_argument("--window", default="?",
                    help="which calibration window this run was made in (#1551): the "
                         "registry build's 3 s seek is `dev`, OD_SEEK_VIDEO=off is "
                         "`opening`. They are never pooled with each other blind.")
    ap.add_argument("--no-arrival", default="")
    ap.add_argument("--min-matched", type=int, default=1)
    args = ap.parse_args()
    if args.pool:
        return pool(args.pool)
    for needed in ("log", "truth", "annotations", "fixture"):
        if not getattr(args, needed):
            ap.error("--%s is required unless --pool is given" % needed)

    no_arrival = set()
    for token in args.no_arrival.split(","):
        token = token.strip()
        if token:
            v, d = token.split(".")
            no_arrival.add((int(v), int(d)))

    truth = axis_census.read_truth(args.truth)
    annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    publishes, ends = read_publish_blocks(args.log)
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, publishes):
        ev.pub = block
    for ev in flat:
        if not hasattr(ev, "pub"):
            ev.pub = None
    with_pub = [ev for ev in flat if ev.pub is not None]
    tag = "fixture=%s window=%s" % (args.fixture, args.window)
    if not with_pub:
        print("I1555 CENSUS %s: no I1555PUBLISH lines in %s -- was the run made with "
              "OD_GEO_SCORE=on?" % (tag, args.log))
        return 2
    print("I1555 CENSUS %s events=%d with_publish=%d truth_visits=%d detected_visits=%d "
          "ends=%d" % (tag, len(flat), len(with_pub), len(truth), len(visits), ends))

    # ---- SEGMENTATION, counted apart from detection and scoring (#1552) ----------------
    # A visit the detector never closed is a visit whose darts land in its neighbour, and
    # that is a boundary fault, not a wrong score. It is reported here and enters no
    # accuracy denominator.
    merged = max(0, len(truth) - len(visits))
    print("I1555 SEGMENTATION truth_visits=%d detected_visits=%d ends=%d merged_or_lost=%d"
          % (len(truth), len(visits), ends, merged))

    # ---- WHAT THE REFERENCE COVERS, before any accuracy figure is printed --------------
    #
    # The hand annotations (testers/i1511_annotations) are the only thing that can join a
    # detection to a throw, and they do NOT cover every throw of every fixture: on
    # rig-20260922 only truth visits 1-3 are annotated, 9 lines of 24 thrown. An
    # unclaimed detection on such a fixture is therefore usually a throw nobody
    # annotated, NOT a phantom -- and the accuracy columns below are figures about the
    # annotated subset and about nothing else.
    #
    # It matters for a second and sharper reason. axis_census.assign_events maps DETECTED
    # visits monotonically onto disjoint consecutive TRUTH-visit ranges and maximises the
    # summed margin. Where the annotation covers every truth visit, the ranges are pinned
    # at both ends and the assignment is over-determined. Where it stops early, the
    # annotated range is free to slide along the run, and spatial aliasing (players
    # revisit the same wedges, which is the measurement #1554 wrote the matcher against)
    # then decides where it lands. That is a property of the reference, not of either
    # scoring path, and a census that did not print it would report an alignment failure
    # as a scoring failure.
    truth_throws = sum(len(v) for v in truth)
    arrivals = sorted(k for k in annots.keys() if k not in no_arrival)
    annotated_visits = sorted({k[0] for k in annots.keys()})
    covers_all = len(annotated_visits) >= len(truth)
    print("I1555 REFERENCE annotated_throws=%d of %d thrown, arrivals=%d, covering truth "
          "visits %s of %d | detected_visits=%d"
          % (len(annots), truth_throws, len(arrivals),
             "%d-%d" % (annotated_visits[0], annotated_visits[-1]) if annotated_visits else "none",
             len(truth), len(visits)))
    if not covers_all:
        print("I1555 REFERENCE-GAP the annotation stops at truth visit %d while the run "
              "detected %d visits, so the matcher's monotone truth-visit range is free to "
              "slide: an unclaimed detection here is usually an UNANNOTATED throw and the "
              "columns below are about the annotated subset alone"
              % (annotated_visits[-1] if annotated_visits else 0, len(visits)))

    assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]
    suspect = set(k for k, _ in assignment["suspect"])

    vote_counts, geo_counts, first_counts, pub_counts = {}, {}, {}, {}
    # The counterfactual, REPORTED and nothing more: what geometry-first would score if a
    # solve that not one placed tip corroborated were refused back to the vote. #1512
    # left promoting the tip to a CONSTRAINT as a later decision to be taken on numbers
    # rather than in passing; this is the smallest thing one could do with the tip short
    # of that, and it is measured here so the decision to leave it alone is a measured one.
    corroborated_counts = {}
    uncorroborated = 0
    matched = geo_solved = 0
    outcomes = {}
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            if (v, ei) not in assigned or ev.pub is None or (v, ei) in suspect:
                continue
            key = assigned[(v, ei)]
            thrown = norm(list(annots[key].values())[0]["thrown"])
            p = ev.pub
            matched += 1
            outcomes[p["outcome"]] = outcomes.get(p["outcome"], 0) + 1
            solved = p["geo"] != "NONE"
            vv = verdict(p["vote"], thrown)
            gv = verdict(p["geo"], thrown) if solved else "refused"
            fv = gv if solved else vv
            pv = verdict(p["score"], thrown)
            corroborated = solved and p["tipsAgree"] > 0
            cv_ = fv if corroborated or not solved else vv
            if solved and not corroborated:
                uncorroborated += 1
            for counts, word in ((vote_counts, vv), (geo_counts, gv),
                                 (first_counts, fv), (pub_counts, pv),
                                 (corroborated_counts, cv_)):
                counts[word] = counts.get(word, 0) + 1
            if solved:
                geo_solved += 1
            print("I1555 PAIR v%d.%d thrown=%s | vote=%s %s | geo=%s %s | "
                  "geometry-first=%s | published=%s %s path=%s conf=%.1f degraded=%d "
                  "outcome=%s"
                  % (key[0], key[1], thrown, norm(p["vote"]), vv,
                     norm(p["geo"]) if solved else p["outcome"], gv,
                     fv, norm(p["score"]), pv, p["path"], p["conf"],
                     1 if p["degraded"] else 0, p["outcome"]))

    # ---- detections no annotation claimed ---------------------------------------------
    #
    # UNCLAIMED, not PHANTOM, and the word was wrong in the first draft of this file. A
    # phantom is a score published where nothing was thrown -- #1505's lone-witness dart,
    # a real fault. An unclaimed detection is only a detection the REFERENCE does not
    # speak about, and on a fixture the annotation does not cover it is overwhelmingly an
    # ordinary throw nobody annotated. The first run of this census called all 17 of
    # rig-20260922's unclaimed detections phantoms, and among them were an S16 for a
    # thrown 16 and a T8 for a thrown T8 -- correct darts, reported as phantoms, on a
    # fixture where only 9 of 24 throws have a line to be judged against.
    unclaimed_rows = 0
    for (v, ei) in assignment["unmatched"]:
        ev = visits[v][ei]
        if ev.pub is None:
            continue
        p = ev.pub
        unclaimed_rows += 1
        print("I1555 UNCLAIMED v%d#%d vote=%s geo=%s published=%s path=%s degraded=%d "
              "outcome=%s -- no annotated throw claimed this detection"
              % (v + 1, ei + 1, norm(p["vote"]),
                 norm(p["geo"]) if p["geo"] != "NONE" else p["outcome"],
                 norm(p["score"]), p["path"], 1 if p["degraded"] else 0, p["outcome"]))
    geo_silent_on_phantoms = sum(
        1 for (v, ei) in assignment["unmatched"]
        if visits[v][ei].pub is not None and visits[v][ei].pub["geo"] == "NONE")
    print("I1555 PHANTOM-HANDLING unclaimed_detections=%d | the geometry refused %d of "
          "them by name (a dart that is not on the board has no second constraint, "
          "#1505); the vote published a score for all %d%s"
          % (unclaimed_rows, geo_silent_on_phantoms, unclaimed_rows,
             "" if covers_all else
             " -- but on this fixture the annotation covers only part of the clip, so "
             "most of these are unannotated throws and not phantoms at all"))

    # ---- coverage and refusals ---------------------------------------------------------
    print("I1555 COVERAGE solver outcomes over matched darts: " +
          ("; ".join("%s=%d" % kv for kv in sorted(outcomes.items())) or "none"))

    if not matched:
        print("I1555 SCORECARD %s: nothing matched -- a census that compared nothing has "
              "measured nothing (#1490)" % tag)
        return 2

    print("I1555 SCORECARD %s matched=%d of %d annotated arrival(s), %d thrown"
          % (tag, matched, len(arrivals), truth_throws))
    print("I1555 " + tallyline("VOTE", vote_counts, matched))
    print("I1555 " + tallyline("GEOMETRY-ONLY", geo_counts, geo_solved))
    print("I1555 " + tallyline("GEOMETRY-FIRST", first_counts, matched))
    print("I1555 " + tallyline("PUBLISHED", pub_counts, matched))
    print("I1555 COUNTERFACTUAL uncorroborated_solves=%d (no placed tip within the solve's "
          "agreement radius) | geometry-first with those refused back to the vote would "
          "read exact %d/%d -- REPORTED, wired nowhere: it is a new rule fitted to %d "
          "dart(s), and #1512 left the tip a corroboration rather than a constraint on "
          "purpose" % (uncorroborated, corroborated_counts.get("exact", 0), matched,
                       uncorroborated))
    print("I1555 TALLY fixture=%s window=%s matched=%d vote_exact=%d geo_solved=%d "
          "geo_exact=%d first_exact=%d published_exact=%d"
          % (args.fixture, args.window, matched, vote_counts.get("exact", 0), geo_solved,
             geo_counts.get("exact", 0), first_counts.get("exact", 0),
             pub_counts.get("exact", 0)))
    if matched < args.min_matched:
        print("I1555 CENSUS %s: %d matched darts against a floor of %d -- not an "
              "instrument (#1490)" % (tag, matched, args.min_matched))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

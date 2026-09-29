"""#1555: the two scoring paths side by side, over one fixture in one calibration window,
against that fixture's ground truth -- and the pooled tally the publish decision is taken
on.

    python3 i1555_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d] [--tally <file>]
    python3 i1555_census.py --log <run.txt> --truth <GROUND-TRUTH.md> --not-annotated \
        --fixture <name> --window <dev|opening>
    python3 i1555_census.py --pool <tally file> [<tally file> ...] [--pool-label X]

NOT ANNOTATED (#1674). A fixture with no testers/i1511_annotations CSV has nothing the
spatial matcher can join a detection to, so --not-annotated replaces it with the ORDINAL
join (ordinal_join below): detected visit i is truth visit i, a visit that published as
many darts as were thrown pairs them in order, and a SHORT visit places nothing -- every
order-preserving placement of its publications is evaluated and none is chosen. Every
line whose figure needs the spatial matcher says `not annotated` by name instead. The
annotated path is untouched by it.

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

    ACCURACY       (#1587) the one figure the accuracy effort is judged by: correct over
                   EVERY annotated arrival, not over the matched ones, with the 96%
                   target and each row's shortfall printed beside it. The four columns
                   above leave out every arrival nothing matched; this one counts it
                   wrong and names it. See accuracy() for the buckets.

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
import itertools
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
# #1657: #1556's line, printed right after I1555PUBLISH for the same dart, read for the
# wire flag the candidate publish rules below are stated in.
FLAG_RE = re.compile(r"I1556PUBLISH window=(-?\d+) geometry=(\d) flagged=(\d) score=(\S+) "
                     r"alt=(\S+) kind=(\S+)")


# ---- #1657: CANDIDATE PUBLISH RULES, scored offline on a run's own rows ---------------
#
# Named on turnaus#1657 (issuecomment-5857307370) BEFORE any dart was counted. Each maps
# one dart's I1555PUBLISH/I1556PUBLISH fields to the string it would publish; a refused
# geometry (geo=NONE) always falls back to the vote, as decidePublishedPath does. "The
# vote is flagged" is voteConf < 0.9 -- no two cameras agreed on the string (#1489's
# 0.7, a reading with a named reservation). The flag fields exist only where the run
# published geometry, which on a geometry-first run is every solved dart.
def _solved(p):
    return p["geo"] != "NONE"


def _vote_flagged(p):
    return p["voteConf"] < 0.9 - 1e-6


def _geo_flagged(p):
    return p.get("flagged", False)


def _rule_a(p):  # geometry-first, today's decidePublishedPath
    return p["geo"] if _solved(p) else p["vote"]


def _rule_b(p):  # vote-first (= OD_SCORE_PATH=vote)
    return p["vote"]


def _rule_c(p):  # geometry only when SOLVED and the vote is flagged
    return p["geo"] if p["outcome"] == "SOLVED" and _vote_flagged(p) else p["vote"]


def _rule_d(p):  # the vote wins every disagreement where geometry is WIRE-UNCERTAIN
    return p["geo"] if p["outcome"] == "SOLVED" else p["vote"]


def _rule_e(p):  # D with #1556's flag in place of the outcome word
    if not _solved(p):
        return p["vote"]
    return p["vote"] if _geo_flagged(p) else p["geo"]


def _rule_f(p):  # vote wins only across the very wire the flag names
    if not _solved(p):
        return p["vote"]
    if _geo_flagged(p) and norm(p["vote"]) == norm(p.get("alt", "-")):
        return p["vote"]
    return p["geo"]


def _rule_g(p):  # C with the flag: geometry only when unflagged and the vote is flagged
    if _solved(p) and not _geo_flagged(p) and _vote_flagged(p):
        return p["geo"]
    return p["vote"]


RULES = [("A", _rule_a), ("B", _rule_b), ("C", _rule_c), ("D", _rule_d),
         ("E", _rule_e), ("F", _rule_f), ("G", _rule_g)]


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
        f = FLAG_RE.search(line)
        if f and pending is not None and int(f.group(1)) == pending["window"]:
            pending["flagged"] = f.group(3) == "1"
            pending["alt"] = f.group(5)
            pending["kind"] = f.group(6)
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


# #1674: what the pooled lines call themselves. `POOLED` unless --pool-label names the
# subset, so a pool over every fixture and a pool over some of them can be printed side by
# side and never mistaken for each other.
POOL_LABEL = "POOLED"


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
    print("I1555 " + POOL_LABEL + " over %d run(s), %d matched darts: vote %d/%d (%s) | "
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
    pool_accuracy(files)
    pool_detection(files)
    pool_rules(files)
    return 0


TARGET_PERMILLE = 960   # #1488: the accuracy target, 96%, over every arrival


def target_line(correct, n):
    """`target 96% = need/n, short k` -- how many darts this row is short of it."""
    need = -(-TARGET_PERMILLE * n // 1000)   # ceil(0.96 n), in integers
    return "target 96%% = %d/%d, short %d" % (need, n, max(0, need - correct))


def motion_clock(log):
    """#1655: the clock the run's motion timers were on, as the binary itself announced it
    (`MOTION CLOCK: <wall|cycle|capture> ...`, scorer.cpp), so an ACCURACY figure always says
    which instrument made it: on `wall` the cooldown and settle timers span a number of
    replayed frames that depends on the box's load, on `capture` they run on the footage's
    own presentation times. `unknown` when the log never said."""
    for raw in open(log, "r", errors="replace"):
        m = re.search(r"MOTION CLOCK: (\w+)", ANSI.sub("", raw))
        if m:
            return m.group(1)
    return "unknown"


def accuracy(fixture, window, visits, annots, arrivals, assignment, clock="unknown",
             rule=None):
    """#1587: ONE figure over every annotated arrival, not over the matched ones.

        correct / every annotated arrival (--no-arrival rows are not arrivals)

    - an on-board arrival is correct only if its matched publication is exact
      (WRONG-SCORE otherwise, with GROUND-TRUTH.md's ring/wedge/silence word);
    - an arrival with no matched publication is UNDETECTED, and wrong;
    - an off-board arrival (thrown MISS) is correct if it publishes MISS or nothing,
      and OFF-BOARD-SCORED (wrong) if its publication is a score;
    - a publication no arrival claimed is a PHANTOM, named and counted apart, and it
      does not enter the denominator.

    AMBIGUOUS, and only where #1504's rule forces it. Nothing joins a publication to a
    throw but the spatial matcher, and it can decline a pair (cost over its cap). An
    arrival it left unmatched could still be an UNCLAIMED publication lying between the
    arrival's matched neighbours in time -- publications keep the order of the throws,
    so those are exactly its order-preserving placements. Every such placement is
    evaluated beside "no publication at all"; where they all agree the verdict stands,
    and where they differ the dart is ambiguous, the correct count is printed as a
    range and the target shortfall is taken on its lower bound. No placement is ever
    CHOSEN, and the one that scores best least of all.
    """
    tag = "fixture=%s window=%s clock=%s" % (fixture, window, clock)
    # #1657: `rule` is (name, fn) -- the same figure with only the published STRING
    # replaced by what that candidate rule would have published. Matching, buckets and
    # placements are this function's own and do not change; the lines carry an I1657
    # prefix so nothing that pools I1555's lines can read them.
    head = "I1555 ACCURACY" if rule is None else "I1657 RULE rule=%s ACCURACY" % rule[0]
    by_key = dict((key, pos) for pos, key in assignment["assigned"].items())
    order = [(v, ei) for v, visit in enumerate(visits) for ei in range(len(visit))]
    flat_index = dict((pos, i) for i, pos in enumerate(order))
    unclaimed = [pos for pos in assignment["unmatched"]
                 if visits[pos[0]][pos[1]].pub is not None]

    def published(pos):
        p = visits[pos[0]][pos[1]].pub
        return norm(p["score"] if rule is None else rule[1](p))

    def name(pos):
        return "v%d#%d %s" % (pos[0] + 1, pos[1] + 1, published(pos))

    def judge(thrown, pub):
        """(bucket, reason) for one arrival against one publication, or None."""
        if pub is None:
            if thrown == "MISS":
                return "correct", "nothing published for an off-board throw"
            return "undetected", "no publication matched"
        if thrown == "MISS":
            if pub == "MISS":
                return "correct", "published MISS"
            return "off-board-scored", "a score published for an off-board throw"
        if pub == thrown:
            return "correct", "exact"
        return "wrong-score", verdict(pub, thrown)

    buckets = ["correct", "wrong-score", "undetected", "off-board-scored", "ambiguous"]
    counts = dict((k, 0) for k in buckets)
    named = []
    # #1536: each arrival's bucket and the publication the matcher gave it (None where it
    # gave none), handed back so the detection split and the camera table are read off
    # the very verdicts this line printed and cannot drift from them.
    results = {}
    for key in arrivals:
        thrown = norm(list(annots[key].values())[0]["thrown"])
        dart = "v%d.%d thrown=%s" % (key[0], key[1], thrown)
        pos = by_key.get(key)
        if pos is not None:
            bucket, reason = judge(thrown, published(pos))
            counts[bucket] += 1
            results[key] = (bucket, pos)
            if bucket != "correct":
                named.append("%s published=%s %s (%s; detected v%d#%d)"
                             % (dart, published(pos), bucket.upper(), reason,
                                pos[0] + 1, pos[1] + 1))
            continue
        lo = max([flat_index[by_key[k]] for k in by_key if k < key] or [-1])
        hi = min([flat_index[by_key[k]] for k in by_key if k > key] or [len(order)])
        cands = [p for p in unclaimed if lo < flat_index[p] < hi]
        outcomes = [(judge(thrown, None), None)] + \
                   [(judge(thrown, published(p)), p) for p in cands]
        # Ambiguous is about CORRECTNESS: undetected under one placement and a wrong
        # score under another is wrong either way, and stays counted as the matcher
        # left it (undetected), with the alternatives named.
        if len(set(o[0][0] == "correct" for o in outcomes)) == 1:
            bucket, reason = outcomes[0][0]
            counts[bucket] += 1
            results[key] = (bucket, None)
            if bucket != "correct":
                also = ("; wrong under every order-preserving placement: %s" % ", ".join(
                    "%s -> %s" % (name(p), o[0]) for o, p in outcomes[1:])) if cands else ""
                named.append("%s published=- %s (%s%s)"
                             % (dart, bucket.upper(), reason, also))
            continue
        counts["ambiguous"] += 1
        results[key] = ("ambiguous", None)
        named.append("%s published=? AMBIGUOUS (the matcher placed no publication; its "
                     "order-preserving placements read %s)"
                     % (dart, " | ".join("%s -> %s" % ("none" if p is None else name(p),
                                                       o[0])
                                         for o, p in outcomes)))
    scoring_phantoms = sum(1 for p in unclaimed if published(p) != "MISS")
    n = len(arrivals)
    c, amb = counts["correct"], counts["ambiguous"]
    print("%s %s correct %s/%d (%s) | %s | wrong-score %d, undetected %d, "
          "off-board-scored %d, ambiguous %d | phantoms %d (%d scoring)"
          % (head, tag, "%d..%d" % (c, c + amb) if amb else "%d" % c, n,
             "%.1f%%" % (100.0 * c / n) if n else "n/a", target_line(c, n),
             counts["wrong-score"], counts["undetected"], counts["off-board-scored"],
             amb, len(unclaimed), scoring_phantoms))
    if rule is not None:
        print("I1657 RULE-TALLY rule=%s fixture=%s window=%s clock=%s arrivals=%d "
              "correct=%d ambiguous=%d" % (rule[0], fixture, window, clock, n, c, amb))
        return
    for line in named:
        print("I1555 ACCURACY-DART " + line)
    for p in unclaimed:
        print("I1555 ACCURACY-PHANTOM v%d#%d published=%s -- claimed by no arrival, "
              "outside the denominator%s"
              % (p[0] + 1, p[1] + 1, published(p),
                 " (a MISS: it scores nothing)" if published(p) == "MISS" else ""))
    print("I1555 ACCURACY-TALLY fixture=%s window=%s clock=%s arrivals=%d correct=%d "
          "wrong_score=%d undetected=%d offboard_scored=%d ambiguous=%d phantoms=%d "
          "scoring_phantoms=%d"
          % (fixture, window, clock, n, c, counts["wrong-score"], counts["undetected"],
             counts["off-board-scored"], amb, len(unclaimed), scoring_phantoms))
    return results


def counterfactual_rules(fixture, window, visits, annots, arrivals, assignment, clock,
                         acc=None):
    """#1657: every candidate rule's ACCURACY on this run's own rows, and each matched
    dart whose verdict differs from rule A's. REPORTED, wired nowhere. `acc` is the
    ACCURACY function the join calls for (#1674: accuracy_ordinal on a fixture that is
    not annotated), accuracy() otherwise."""
    acc = acc or accuracy
    rows = []
    mismatch = 0
    for pos, key in sorted(assignment["assigned"].items(), key=lambda kv: kv[1]):
        if key not in arrivals:
            continue
        p = visits[pos[0]][pos[1]].pub
        if p is None:
            continue
        if norm(_rule_a(p)) != norm(p["score"]):
            mismatch += 1
        rows.append((key, norm(list(annots[key].values())[0]["thrown"]), p))
    print("I1657 RULE-CHECK fixture=%s window=%s rule A reproduces the published string "
          "on %d of %d matched arrivals" % (fixture, window, len(rows) - mismatch, len(rows)))
    for name, fn in RULES:
        acc(fixture, window, visits, annots, arrivals, assignment, clock,
            rule=(name, fn))
        for key, thrown, p in rows:
            a, r = norm(_rule_a(p)), norm(fn(p))
            ca, cr = a == thrown, r == thrown
            if ca == cr:
                continue
            print("I1657 RULE-PAIR rule=%s fixture=%s window=%s dart=v%d.%d thrown=%s "
                  "A=%s R=%s %s | vote=%s voteConf=%.2f geo=%s outcome=%s flagged=%d "
                  "alt=%s kind=%s"
                  % (name, fixture, window, key[0], key[1], thrown, a, r,
                     "GAIN" if cr else "LOSS", norm(p["vote"]), p["voteConf"],
                     norm(p["geo"]), p["outcome"], 1 if p.get("flagged") else 0,
                     p.get("alt", "-"), p.get("kind", "-")))


def pool_rules(files):
    """#1657: the pooled figure per rule, and its gains and losses per UNIQUE dart -- the
    two windows see the same throws, so a dart gained in both is one dart, not two."""
    tallies, pairs = {}, {}
    for path in files:
        for raw in open(path, "r", errors="replace"):
            line = ANSI.sub("", raw)
            for tagname, store in (("I1657 RULE-TALLY ", tallies), ("I1657 RULE-PAIR ", pairs)):
                if tagname in line:
                    body = line.split(tagname, 1)[1]
                    d = dict(t.split("=", 1) for t in body.split() if "=" in t)
                    d["_kind"] = "GAIN" if " GAIN " in body else "LOSS"
                    store.setdefault(d["rule"], []).append(d)
    if not tallies:
        return
    for name, _ in RULES:
        rows = tallies.get(name, [])
        n = sum(int(r["arrivals"]) for r in rows)
        c = sum(int(r["correct"]) for r in rows)
        amb = sum(int(r["ambiguous"]) for r in rows)
        dart = {}
        for d in pairs.get(name, []):
            dart.setdefault((d["fixture"], d["dart"]), []).append(
                (d["window"], d["_kind"], d["thrown"], d["A"], d["R"]))
        ug = sorted(k for k, v in dart.items() if all(x[1] == "GAIN" for x in v))
        ul = sorted(k for k, v in dart.items() if all(x[1] == "LOSS" for x in v))
        mixed = sorted(k for k in dart if k not in ug and k not in ul)
        wg = sum(1 for v in dart.values() for x in v if x[1] == "GAIN")
        wl = sum(1 for v in dart.values() for x in v if x[1] == "LOSS")
        print("I1657 RULE-POOLED rule=%s correct %s/%d over %d run(s) | window rows +%d -%d "
              "| unique darts +%d -%d mixed %d (net %+d)"
              % (name, "%d..%d" % (c, c + amb) if amb else "%d" % c, n, len(rows),
                 wg, wl, len(ug), len(ul), len(mixed), len(ug) - len(ul)))
        for k in sorted(dart):
            print("I1657 RULE-DART rule=%s %s %s %s"
                  % (name, k[0], k[1], "; ".join("%s %s thrown=%s A=%s rule=%s"
                                                 % (w, kind, t, a, r)
                                                 for w, kind, t, a, r in dart[k])))


def pool_accuracy(files):
    """The pooled ACCURACY line over the per-run ACCURACY-TALLY lines, and per fixture."""
    keys = ["arrivals", "correct", "wrong_score", "undetected", "offboard_scored",
            "ambiguous", "phantoms", "scoring_phantoms", "wrong_unplaced"]
    rows = []
    for path in files:
        for raw in open(path, "r", errors="replace"):
            if "I1555 ACCURACY-TALLY " not in raw:
                continue
            body = ANSI.sub("", raw).split("I1555 ACCURACY-TALLY ", 1)[1]
            rows.append(dict(t.split("=", 1) for t in body.split() if "=" in t))
    if not rows:
        print("I1555 ACCURACY POOLED: no ACCURACY-TALLY lines -- nothing to pool")
        return

    # #1655: the pooled line names the clock of the runs it pooled, and says MIXED rather
    # than pick one when they differ -- a wall-clock figure and a capture-clock figure are
    # two instruments' readings, not one.
    clocks = sorted(set(r.get("clock", "unknown") for r in rows))
    clock = clocks[0] if len(clocks) == 1 else "MIXED(%s)" % ",".join(clocks)

    def line(label, group):
        label = "%s clock=%s" % (label, clock)
        t = dict((k, sum(int(r.get(k, 0)) for r in group)) for k in keys)
        n, c, amb = t["arrivals"], t["correct"], t["ambiguous"]
        # #1674: wrong-unplaced exists only on a not-annotated fixture (accuracy_ordinal)
        # and is printed only where a pooled row has one, so a pool without such a
        # fixture prints the line it always printed.
        wu = (", wrong-unplaced %d" % t["wrong_unplaced"]) if t["wrong_unplaced"] else ""
        # A not-annotated row carries no phantoms= field: its phantoms were not measured,
        # which is not the same as none, and the pooled line says so.
        unmeasured = sum(1 for r in group if "phantoms" not in r)
        if unmeasured == len(group):
            ph = "phantoms not measured (%d not-annotated run(s))" % unmeasured
        else:
            ph = "phantoms %d (%d scoring%s)" % (
                t["phantoms"], t["scoring_phantoms"],
                ("; not measured on %d not-annotated run(s)" % unmeasured)
                if unmeasured else "")
        print("I1555 ACCURACY %s over %d run(s): correct %s/%d (%s) | %s | wrong-score "
              "%d, undetected %d, off-board-scored %d, ambiguous %d%s | %s"
              % (label, len(group), "%d..%d" % (c, c + amb) if amb else "%d" % c, n,
                 "%.1f%%" % (100.0 * c / n) if n else "n/a", target_line(c, n),
                 t["wrong_score"], t["undetected"], t["offboard_scored"], amb, wu, ph))
    line(POOL_LABEL, rows)
    for f in sorted(set(r.get("fixture", "?") for r in rows)):
        line("fixture=%s" % f, [r for r in rows if r.get("fixture", "?") == f])


# ---- #1536: what the detector did not see, and which cameras saw what -----------------
#
# The per-camera signal is #1512's I1512CAM line, one per camera per scored window:
# `usable=1` is a constraint the camera offered the solve (the axis the geometry is
# intersected from), `excluded=1` a constraint the solve set aside, `tipPlaced=1` a tip the
# camera placed on its board. The ISSUE's "saw it" is "published a tip or a constraint",
# so a camera SAW a dart when any of the three holds. It is chosen over the other lines a
# run prints because it is the only one that is (a) printed for EVERY camera of EVERY
# scored window on every path -- I1511AXIS is too, but carries no tip, and I1647FIT /
# I1641LOCAL are about the board fit, not the dart -- and (b) keyed by the window the
# census already joins to its publication. Measured on the #1655 logs: usable=1 and
# I1511AXIS valid=1 agree on every camera row, so the axis line is the fallback where a
# run printed no I1512CAM (a vote-pinned or pre-#1512 log), minus the tip.
CAM_RE = re.compile(r"I1512CAM window=(\d+) cam=(\d+) usable=(\d) excluded=(\d) .*"
                    r"tipPlaced=(\d) .*?excl=(.*)$")


def _why(text):
    """`no usable axis: not straight: the kept ...` -> `not-straight`: the exclusion's
    own first clause, one word, so a table row stays one line."""
    t = (text or "").strip()
    if t.startswith("no usable axis:"):
        t = t[len("no usable axis:"):].strip()
    t = t.split(":", 1)[0].strip()
    return "-".join(t.split()[:3]) if t and t != "-" else ""


def read_camera_evidence(path, visits):
    """{window: {cam: state}} and the cameras the run ever reported on.

    state is `C` (a constraint the solve used), `X:<why>` (a constraint it excluded),
    `t:<why>` (a tip placed, no usable constraint), `-:<why>` (neither)."""
    ev = {}
    for raw in open(path, "r", errors="replace"):
        m = CAM_RE.search(ANSI.sub("", raw).rstrip("\n"))
        if not m:
            continue
        w, cam = int(m.group(1)), int(m.group(2))
        usable, excluded, tip = m.group(3) == "1", m.group(4) == "1", m.group(5) == "1"
        why = _why(m.group(6))
        if usable and not excluded:
            state = "C"
        elif usable:
            state = "X:" + (why or "excluded")
        elif tip:
            state = "t:" + (why or "no-axis")
        else:
            state = "-:" + (why or "no-axis")
        ev.setdefault(w, {})[cam] = state
    source = "I1512CAM"
    if not ev:
        source = "I1511AXIS"
        for visit in visits:
            for e in visit:
                for cam, obs in e.cams.items():
                    ev.setdefault(e.window, {})[cam] = (
                        "C" if obs["valid"] else "-:" + (_why(obs["refusal"]) or "no-axis"))
    cams = sorted({c for per in ev.values() for c in per})
    return ev, cams, source


def _saw(state):
    return state is not None and state[0] in "CXt"


def detection_and_cameras(fixture, window, clock, visits, annots, arrivals, assignment,
                          results, evidence):
    """#1536: the two not-detected numbers side by side, and the per-camera table.

    NOT DETECTED is what #1587's `undetected` already means -- the matcher placed no
    publication and no order-preserving placement would make the verdict correct -- split
    by what was THROWN: a LANDED dart nothing published is a lost score (#1587's
    `undetected`, identical by construction), a thrown MISS nothing published is the
    silence the census already counts correct. They are different objects and are never
    summed. An arrival left AMBIGUOUS by #1587 is in neither and is counted apart.

    Per camera, for every arrival: the state of each camera in the window whose
    publication the matcher gave it. An arrival with no publication has no window, so
    no camera is said to have seen it; the unclaimed publications lying between its
    matched neighbours (#1587's order-preserving placements) are printed beside it as
    UNATTRIBUTED evidence -- a window nothing claimed is where a dart the detector
    half-saw would be, but nothing joins it to this dart, so it is shown and not counted.
    """
    ev_by_win, cams, source = evidence
    tag = "fixture=%s window=%s clock=%s" % (fixture, window, clock)
    by_key = dict((key, pos) for pos, key in assignment["assigned"].items())
    order = [(v, ei) for v, visit in enumerate(visits) for ei in range(len(visit))]
    flat_index = dict((pos, i) for i, pos in enumerate(order))
    unclaimed = [pos for pos in assignment["unmatched"]
                 if visits[pos[0]][pos[1]].pub is not None]

    def thrown_of(key):
        return norm(list(annots[key].values())[0]["thrown"])

    def row(win):
        per = ev_by_win.get(win, {})
        return " ".join("cam%d=%s" % (c, per.get(c, "none")) for c in cams)

    # ---- item 1: the two numbers ------------------------------------------------------
    landed = [k for k in arrivals if thrown_of(k) != "MISS"]
    missed = [k for k in arrivals if thrown_of(k) == "MISS"]

    def not_detected(keys):
        return [k for k in keys if results[k][1] is None and results[k][0] != "ambiguous"
                and by_key.get(k) is None]

    def ambiguous(keys):
        return [k for k in keys if results[k][0] == "ambiguous"]

    lnd, mnd = not_detected(landed), not_detected(missed)
    lam, mam = ambiguous(landed), ambiguous(missed)
    print("I1536 DETECTION %s landed-and-not-detected %d/%d | missed-and-not-detected %d/%d"
          " | unmatched-ambiguous landed %d, missed %d -- two numbers, never summed: an "
          "unseen landed dart is a lost score, an unseen miss is a silence counted correct"
          % (tag, len(lnd), len(landed), len(mnd), len(missed), len(lam), len(mam)))
    for k in lnd:
        print("I1536 DETECTION-DART v%d.%d thrown=%s LANDED-AND-NOT-DETECTED"
              % (k[0], k[1], thrown_of(k)))
    for k in mnd:
        print("I1536 DETECTION-DART v%d.%d thrown=MISS MISSED-AND-NOT-DETECTED"
              % (k[0], k[1]))
    print("I1536 DETECTION-TALLY %s landed=%d landed_not_detected=%d missed=%d "
          "missed_not_detected=%d landed_ambiguous=%d missed_ambiguous=%d"
          % (tag, len(landed), len(lnd), len(missed), len(mnd), len(lam), len(mam)))

    # ---- item 2: per camera, for every arrival -----------------------------------------
    saw = dict((c, 0) for c in cams)
    constraint = dict((c, 0) for c in cams)
    seen_by = dict((i, 0) for i in range(len(cams) + 1))
    constrained_by = dict((i, 0) for i in range(len(cams) + 1))
    unseen_nothing_between = unseen_window_between = 0
    for key in arrivals:
        bucket, _ = results[key]
        pos = by_key.get(key)
        head = "I1536 CAMERA-DART %s dart=v%d.%d thrown=%s bucket=%s" % (
            tag, key[0], key[1], thrown_of(key), bucket)
        if pos is not None:
            win = visits[pos[0]][pos[1]].window
            per = ev_by_win.get(win, {})
            n = nc = 0
            for c in cams:
                if _saw(per.get(c)):
                    saw[c] += 1
                    n += 1
                if per.get(c) == "C":
                    constraint[c] += 1
                    nc += 1
            seen_by[n] += 1
            constrained_by[nc] += 1
            print("%s detected=v%d#%d win=%d %s seen_by=%d/%d constrained_by=%d/%d"
                  % (head, pos[0] + 1, pos[1] + 1, win, row(win), n, len(cams), nc,
                     len(cams)))
            continue
        seen_by[0] += 1
        constrained_by[0] += 1
        lo = max([flat_index[by_key[k]] for k in by_key if k < key] or [-1])
        hi = min([flat_index[by_key[k]] for k in by_key if k > key] or [len(order)])
        between = [p for p in unclaimed if lo < flat_index[p] < hi]
        if between:
            unseen_window_between += 1
            also = "; ".join(
                "v%d#%d win=%d published %s, claimed by no arrival: %s"
                % (p[0] + 1, p[1] + 1, visits[p[0]][p[1]].window,
                   norm(visits[p[0]][p[1]].pub["score"]), row(visits[p[0]][p[1]].window))
                for p in between)
        else:
            unseen_nothing_between += 1
            also = "none -- no window published between its matched neighbours, so no " \
                   "camera reported anything that could be this dart"
        print("%s detected=- seen_by=0/%d | unattributed windows between its neighbours: %s"
              % (head, len(cams), also))
    n = len(arrivals)
    print("I1536 CAMERA-TALLY %s source=%s arrivals=%d %s %s %s %s unseen_nothing_between=%d "
          "unseen_unattributed_window=%d"
          % (tag, source, n,
             " ".join("cam%d_saw=%d" % (c, saw[c]) for c in cams),
             " ".join("cam%d_constraint=%d" % (c, constraint[c]) for c in cams),
             " ".join("seen_by_%d=%d" % (i, seen_by[i]) for i in sorted(seen_by, reverse=True)),
             " ".join("constrained_by_%d=%d" % (i, constrained_by[i])
                      for i in sorted(constrained_by, reverse=True)),
             unseen_nothing_between, unseen_window_between))


def pool_detection(files):
    """#1536: the DETECTION and CAMERA tallies summed, pooled and per fixture."""
    det, cam = [], []
    for path in files:
        for raw in open(path, "r", errors="replace"):
            line = ANSI.sub("", raw)
            for tagname, store in (("I1536 DETECTION-TALLY ", det),
                                   ("I1536 CAMERA-TALLY ", cam)):
                if tagname in line:
                    body = line.split(tagname, 1)[1]
                    store.append(dict(t.split("=", 1) for t in body.split() if "=" in t))
    if not det:
        return

    def total(group, k):
        return sum(int(r.get(k, 0)) for r in group)

    for label, pick in [(POOL_LABEL, lambda r: True)] + [
            ("fixture=%s" % f, (lambda f: lambda r: r.get("fixture") == f)(f))
            for f in sorted(set(r.get("fixture", "?") for r in det))]:
        d = [r for r in det if pick(r)]
        c = [r for r in cam if pick(r)]
        print("I1536 DETECTION %s over %d run(s): landed-and-not-detected %d/%d | "
              "missed-and-not-detected %d/%d | unmatched-ambiguous landed %d, missed %d"
              % (label, len(d), total(d, "landed_not_detected"), total(d, "landed"),
                 total(d, "missed_not_detected"), total(d, "missed"),
                 total(d, "landed_ambiguous"), total(d, "missed_ambiguous")))
        keys = sorted({k for r in c for k in r
                       if re.match(r"cam\d+_(saw|constraint)$|(seen|constrained)_by_\d+$|unseen_", k)})
        rank = ("_saw", "_constraint", "seen_by", "constrained_by", "unseen")
        keys.sort(key=lambda k: (min(i for i, r in enumerate(rank) if r in k),
                                 -int(k[-1]) if k[-1].isdigit() else 0, k))
        print("I1536 CAMERA %s over %d run(s): arrivals=%d %s"
              % (label, len(c), total(c, "arrivals"),
                 " ".join("%s=%d" % (k, total(c, k)) for k in keys)))


# ---- #1674: a fixture with NO annotations, joined by visit order ------------------------
#
# The spatial matcher above is the only thing in this census that joins a detection to a
# throw, and it needs testers/i1511_annotations/<fixture>.csv: without one every cost is
# undefined and nothing matches. rig-20260929 has none, and hand-measuring 36 throws is
# not what #1674 is. What the fixture DOES have is a truth table in visit order, and a run
# whose visits are separated by takeouts the detector saw. So the ORDINAL join:
#
#   - detected visit i is truth visit i, and the join REFUSES the run (no figure at all)
#     where that cannot hold on its face: more detected visits than thrown ones (a split
#     visit), or a detected visit with more publications than its truth visit has throws
#     (a merge or a phantom). What it cannot see is a visit that published NOTHING in the
#     middle of the clip -- read_run drops an empty visit, and every later visit would
#     slide one truth visit early. The JOIN-VISIT lines print the pairing so a reader can;
#   - a visit that published as many darts as were thrown pairs them IN ORDER, and those
#     pairs are the `assigned` darts every column above is taken over;
#   - a SHORT visit (k publications for n > k throws) places NOTHING. Its publications are
#     the C(n, k) order-preserving placements inside that visit, every one of them is
#     judged, and -- #1587's rule -- none is chosen, the one that scores best least of all.
#     A dart correct under some placements and wrong under others is AMBIGUOUS; one wrong
#     under all of them, but as a wrong score under one and unpublished under another, is
#     WRONG-UNPLACED: wrong, and which kind of wrong the join cannot say without the
#     frame-level evidence an annotation carries.
#
# #1504 forbids aligning by detection ORDER where a spatial reference exists, because an
# undetected throw shifts everything after it. The ordinal join is used only where no
# spatial reference exists, and it never lets a missing dart shift a pairing: a short
# visit pairs nothing.
def read_short_truth(path):
    """The one-line short form (`5 13 12, t9 t14 t11, ...`) as read_truth's lists, or None.

    A fixture's GROUND-TRUTH.md is read in its own form rather than transcribed into a
    table under testers/ (as i1499_truth_rig20260922.md was), so there is one copy to
    correct. 50 is written BULL and 25 OUTER, the vocabulary the detector publishes."""
    for raw in open(path, "r", errors="replace"):
        line = raw.strip()
        if not line:
            continue
        visits = []
        for chunk in line.split(","):
            throws = []
            for token in chunk.split():
                m = axis_census.THROW_RE.match({"50": "BULL", "25": "OUTER"}.get(token, token))
                if not m:
                    return None
                throws.append(m.group(1).upper())
            if not throws:
                return None
            visits.append(throws)
        return visits
    return None


def ordinal_join(visits, truth):
    """(join, None) or (None, why). The join has assign_events' keys -- `assigned`, the
    in-order pairs of every visit that published all its throws; `unmatched`, every
    publication of a short visit -- and `placements`: (visit1, dart) -> one entry per
    order-preserving placement of its visit, the (visit0, event_index) it receives there
    or None."""
    if len(visits) > len(truth):
        return None, ("the run detected %d visits where %d were thrown: a visit split in "
                      "two, which visit order cannot place" % (len(visits), len(truth)))
    for i, visit in enumerate(visits):
        if len(visit) > len(truth[i]):
            return None, ("detected visit %d published %d darts where truth visit %d threw "
                          "%d: a merged visit or a phantom, which visit order cannot place"
                          % (i + 1, len(visit), i + 1, len(truth[i])))
        if any(ev.pub is None for ev in visit):
            return None, ("detected visit %d holds a dart with no I1555PUBLISH line"
                          % (i + 1))
    join = {"assigned": {}, "suspect": [], "undetected": [], "recording_absent": [],
            "unmatched": [], "placements": {}}
    for t, throws in enumerate(truth):
        k = len(visits[t]) if t < len(visits) else 0
        n = len(throws)
        combos = list(itertools.combinations(range(n), k))
        for d in range(n):
            join["placements"][(t + 1, d + 1)] = [
                (t, combo.index(d)) if d in combo else None for combo in combos]
        for ei in range(k):
            if k == n:
                join["assigned"][(t, ei)] = (t + 1, ei + 1)
            else:
                join["unmatched"].append((t, ei))
    return join, None


def accuracy_ordinal(fixture, window, visits, annots, arrivals, assignment, clock="unknown",
                     rule=None):
    """accuracy()'s figure and buckets on the ORDINAL join (`assignment` is ordinal_join's).

    A dart's verdict is judged under every placement of its visit; one verdict under all
    of them stands, correct under some and wrong under others is AMBIGUOUS (the range),
    and wrong under all but of different kinds is WRONG-UNPLACED. There is no unclaimed
    publication, so phantoms are NOT MEASURED here, not zero: the join assumes a visit's
    publications are its throws."""
    tag = "fixture=%s window=%s clock=%s join=ordinal" % (fixture, window, clock)
    head = "I1555 ACCURACY" if rule is None else "I1657 RULE rule=%s ACCURACY" % rule[0]

    def published(pos):
        p = visits[pos[0]][pos[1]].pub
        return norm(p["score"] if rule is None else rule[1](p))

    def name(pos):
        return "v%d#%d %s" % (pos[0] + 1, pos[1] + 1, published(pos))

    def judge(thrown, pub):
        if pub is None:
            if thrown == "MISS":
                return "correct", "nothing published for an off-board throw"
            return "undetected", "no publication in its visit"
        if thrown == "MISS":
            if pub == "MISS":
                return "correct", "published MISS"
            return "off-board-scored", "a score published for an off-board throw"
        if pub == thrown:
            return "correct", "exact"
        return "wrong-score", verdict(pub, thrown)

    buckets = ["correct", "wrong-score", "undetected", "off-board-scored", "ambiguous",
               "wrong-unplaced"]
    counts = dict((k, 0) for k in buckets)
    named, results = [], {}
    for key in arrivals:
        thrown = norm(list(annots[key].values())[0]["thrown"])
        dart = "v%d.%d thrown=%s" % (key[0], key[1], thrown)
        places = sorted(set(assignment["placements"][key]),
                        key=lambda pos: (-1, -1) if pos is None else pos)
        outcomes = [(judge(thrown, None if p is None else published(p)), p) for p in places]
        if len(outcomes) == 1:
            (bucket, reason), pos = outcomes[0]
            counts[bucket] += 1
            results[key] = (bucket, pos)
            if bucket != "correct":
                if pos is not None:
                    named.append("%s published=%s %s (%s; detected v%d#%d)"
                                 % (dart, published(pos), bucket.upper(), reason,
                                    pos[0] + 1, pos[1] + 1))
                else:
                    named.append("%s published=- %s (%s: truth visit %d published nothing)"
                                 % (dart, bucket.upper(), reason, key[0]))
            continue
        readings = " | ".join("%s -> %s" % ("none" if p is None else name(p), o[0])
                              for o, p in outcomes)
        right = set(o[0] == "correct" for o, _ in outcomes)
        kinds = set(o[0] for o, _ in outcomes)
        if len(right) == 2:
            bucket = "ambiguous"
            named.append("%s published=? AMBIGUOUS (not annotated, so the ordinal join "
                         "places nothing inside short visit %d; its order-preserving "
                         "placements read %s)" % (dart, key[0], readings))
        elif len(kinds) == 1:
            bucket = kinds.pop()
            if bucket != "correct":
                named.append("%s published=? %s (not annotated; the same under every "
                             "placement in short visit %d: %s)"
                             % (dart, bucket.upper(), key[0], readings))
        else:
            bucket = "wrong-unplaced"
            named.append("%s published=? WRONG-UNPLACED (not annotated: wrong under every "
                         "order-preserving placement in short visit %d, but not the same "
                         "wrong: %s)" % (dart, key[0], readings))
        counts[bucket] += 1
        results[key] = (bucket, None)
    n = len(arrivals)
    c, amb, wu = counts["correct"], counts["ambiguous"], counts["wrong-unplaced"]
    print("%s %s correct %s/%d (%s) | %s | wrong-score %d, undetected %d, "
          "off-board-scored %d, ambiguous %d%s | phantoms not measured (not annotated: the "
          "ordinal join takes a visit's publications to be its throws)"
          % (head, tag, "%d..%d" % (c, c + amb) if amb else "%d" % c, n,
             "%.1f%%" % (100.0 * c / n) if n else "n/a", target_line(c, n),
             counts["wrong-score"], counts["undetected"], counts["off-board-scored"],
             amb, (", wrong-unplaced %d" % wu) if wu else ""))
    if rule is not None:
        print("I1657 RULE-TALLY rule=%s fixture=%s window=%s clock=%s join=ordinal "
              "arrivals=%d correct=%d ambiguous=%d" % (rule[0], fixture, window, clock, n,
                                                       c, amb))
        return
    for line in named:
        print("I1555 ACCURACY-DART " + line)
    # No `phantoms=` field: pool_accuracy says `not measured` for a row without one.
    print("I1555 ACCURACY-TALLY fixture=%s window=%s clock=%s join=ordinal arrivals=%d "
          "correct=%d wrong_score=%d undetected=%d offboard_scored=%d ambiguous=%d "
          "wrong_unplaced=%d"
          % (fixture, window, clock, n, c, counts["wrong-score"], counts["undetected"],
             counts["off-board-scored"], amb, wu))
    return results


def detection_ordinal(fixture, window, clock, visits, annots, arrivals, assignment,
                      results, evidence):
    """detection_and_cameras() on the ORDINAL join. Not detected is what EVERY placement
    agrees on: a dart of a visit that published nothing. A dart of a short visit is
    unpublished under some placements and published under others, so it is counted
    `unmatched-ambiguous` -- which dart of the visit went unpublished is exactly what the
    join cannot say -- and in the camera table it is UNPLACED, with its visit's windows
    printed beside it and attributed to nothing."""
    ev_by_win, cams, source = evidence
    tag = "fixture=%s window=%s clock=%s join=ordinal" % (fixture, window, clock)

    def thrown_of(key):
        return norm(list(annots[key].values())[0]["thrown"])

    def row(win):
        per = ev_by_win.get(win, {})
        return " ".join("cam%d=%s" % (c, per.get(c, "none")) for c in cams)

    def places(key):
        return set(assignment["placements"][key])

    landed = [k for k in arrivals if thrown_of(k) != "MISS"]
    missed = [k for k in arrivals if thrown_of(k) == "MISS"]
    lnd = [k for k in landed if places(k) == {None}]
    mnd = [k for k in missed if places(k) == {None}]
    lam = [k for k in landed if len(places(k)) > 1]
    mam = [k for k in missed if len(places(k)) > 1]
    print("I1536 DETECTION %s landed-and-not-detected %d/%d | missed-and-not-detected %d/%d"
          " | unmatched-ambiguous landed %d, missed %d -- two numbers, never summed: an "
          "unseen landed dart is a lost score, an unseen miss is a silence counted correct"
          " -- not annotated: `unmatched-ambiguous` is a dart of a short visit, which of "
          "whose throws went unpublished the ordinal join cannot say"
          % (tag, len(lnd), len(landed), len(mnd), len(missed), len(lam), len(mam)))
    for k in lnd:
        print("I1536 DETECTION-DART v%d.%d thrown=%s LANDED-AND-NOT-DETECTED"
              % (k[0], k[1], thrown_of(k)))
    for k in mnd:
        print("I1536 DETECTION-DART v%d.%d thrown=MISS MISSED-AND-NOT-DETECTED"
              % (k[0], k[1]))
    for k in lam + mam:
        print("I1536 DETECTION-DART v%d.%d thrown=%s UNPLACED (not annotated: short visit "
              "%d published %d of %d)"
              % (k[0], k[1], thrown_of(k), k[0], len(visits[k[0] - 1]),
                 len([a for a in arrivals if a[0] == k[0]])))
    print("I1536 DETECTION-TALLY %s landed=%d landed_not_detected=%d missed=%d "
          "missed_not_detected=%d landed_ambiguous=%d missed_ambiguous=%d"
          % (tag, len(landed), len(lnd), len(missed), len(mnd), len(lam), len(mam)))

    saw = dict((c, 0) for c in cams)
    constraint = dict((c, 0) for c in cams)
    seen_by = dict((i, 0) for i in range(len(cams) + 1))
    constrained_by = dict((i, 0) for i in range(len(cams) + 1))
    unseen_nothing = unplaced = 0
    for key in arrivals:
        bucket, _ = results[key]
        head = "I1536 CAMERA-DART %s dart=v%d.%d thrown=%s bucket=%s" % (
            tag, key[0], key[1], thrown_of(key), bucket)
        opts = places(key)
        if len(opts) == 1 and None not in opts:
            pos = opts.pop()
            win = visits[pos[0]][pos[1]].window
            per = ev_by_win.get(win, {})
            n = nc = 0
            for c in cams:
                if _saw(per.get(c)):
                    saw[c] += 1
                    n += 1
                if per.get(c) == "C":
                    constraint[c] += 1
                    nc += 1
            seen_by[n] += 1
            constrained_by[nc] += 1
            print("%s detected=v%d#%d win=%d %s seen_by=%d/%d constrained_by=%d/%d"
                  % (head, pos[0] + 1, pos[1] + 1, win, row(win), n, len(cams), nc,
                     len(cams)))
            continue
        if opts == {None}:
            seen_by[0] += 1
            constrained_by[0] += 1
            unseen_nothing += 1
            print("%s detected=- seen_by=0/%d | truth visit %d published nothing, so no "
                  "camera reported anything that could be this dart"
                  % (head, len(cams), key[0]))
            continue
        unplaced += 1
        also = "; ".join("v%d#%d win=%d published %s: %s"
                         % (p[0] + 1, p[1] + 1, visits[p[0]][p[1]].window,
                            norm(visits[p[0]][p[1]].pub["score"]),
                            row(visits[p[0]][p[1]].window))
                         for p in sorted(p for p in opts if p is not None))
        print("%s detected=? UNPLACED | not annotated: its visit's windows, attributed to "
              "no dart: %s" % (head, also))
    print("I1536 CAMERA-TALLY %s source=%s arrivals=%d %s %s %s %s unseen_nothing_between=%d "
          "unseen_unattributed_window=0 unseen_unplaced=%d"
          % (tag, source, len(arrivals),
             " ".join("cam%d_saw=%d" % (c, saw[c]) for c in cams),
             " ".join("cam%d_constraint=%d" % (c, constraint[c]) for c in cams),
             " ".join("seen_by_%d=%d" % (i, seen_by[i]) for i in sorted(seen_by, reverse=True)),
             " ".join("constrained_by_%d=%d" % (i, constrained_by[i])
                      for i in sorted(constrained_by, reverse=True)),
             unseen_nothing, unplaced))


def plant(visits, annots, no_arrival, fixture, window, annotated=True):
    """#1536's plant: OD_CENSUS_PLANT=<fixture>:<window>:<visit>.<dart> removes, BEFORE the
    matching, the publication the matcher gives that arrival -- the run as if the
    detector had never published it. The proof it exists for: `undetected` (and
    landed-and-not-detected) must move by exactly one and name that dart. Returns None
    when the plant is not for this census, else (ok, message)."""
    spec = os.environ.get("OD_CENSUS_PLANT", "").strip()
    if not spec:
        return None
    try:
        f, w, vd = spec.split(":")
        v, d = (int(x) for x in vd.lstrip("v").split("."))
    except ValueError:
        return (False, "I1536 PLANT-FAIL OD_CENSUS_PLANT=%s is not "
                       "<fixture>:<window>:<visit>.<dart>" % spec)
    if (f, w) != (fixture, window):
        return None
    if not annotated:
        return (False, "I1536 PLANT-FAIL OD_CENSUS_PLANT=%s: %s is not annotated. The plant "
                       "proves the SPATIAL matcher's count; on the ordinal join a dropped "
                       "publication turns its whole visit short and re-places every dart "
                       "in it, so it would move more than one dart by construction"
                       % (spec, fixture))
    pre = axis_census.assign_events(visits, annots, no_arrival)
    pos = dict((key, p) for p, key in pre["assigned"].items()).get((v, d))
    if pos is None:
        return (False, "I1536 PLANT-FAIL OD_CENSUS_PLANT=%s: arrival v%d.%d has no matched "
                       "publication to drop, so the plant proves nothing" % (spec, v, d))
    ev = visits[pos[0]].pop(pos[1])
    if not visits[pos[0]]:
        visits.pop(pos[0])
    return (True, "I1536 PLANT %s dropped v%d#%d (window %d, published %s) -- the "
                  "publication the matcher gave arrival v%d.%d -- before matching. THIS "
                  "CENSUS IS PLANTED: it is a proof of the instrument, not a measurement"
                  % (spec, pos[0] + 1, pos[1] + 1, ev.window,
                     norm(ev.pub["score"]) if ev.pub else "-", v, d))


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
    ap.add_argument("--not-annotated", action="store_true",
                    help="#1674: the fixture has no testers/i1511_annotations CSV; join "
                         "by visit order (ordinal_join) and say `not annotated` wherever a "
                         "figure needs the spatial matcher")
    ap.add_argument("--pool-label", default="POOLED",
                    help="with --pool: what the pooled lines call themselves")
    ap.add_argument("--min-matched", type=int, default=1)
    args = ap.parse_args()
    if args.pool:
        global POOL_LABEL
        POOL_LABEL = args.pool_label
        return pool(args.pool)
    for needed in ("log", "truth", "fixture"):
        if not getattr(args, needed):
            ap.error("--%s is required unless --pool is given" % needed)
    if bool(args.annotations) == args.not_annotated:
        ap.error("give exactly one of --annotations and --not-annotated")

    no_arrival = set()
    for token in args.no_arrival.split(","):
        token = token.strip()
        if token:
            v, d = token.split(".")
            no_arrival.add((int(v), int(d)))

    truth = axis_census.read_truth(args.truth) or read_short_truth(args.truth) or []
    if args.not_annotated:
        # The truth stands in for the annotation's `thrown` column and for nothing else:
        # no line, no tip, so nothing below can take a spatial figure off it.
        annots = dict(((v + 1, d + 1), {0: {"line": None, "tip": None, "thrown": t,
                                            "note": "not annotated"}})
                      for v, visit in enumerate(truth) for d, t in enumerate(visit))
        if not annots:
            print("I1555 CENSUS fixture=%s: no throws read from %s" % (args.fixture,
                                                                      args.truth))
            return 2
    else:
        annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    publishes, ends = read_publish_blocks(args.log)
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, publishes):
        ev.pub = block
    for ev in flat:
        if not hasattr(ev, "pub"):
            ev.pub = None
    planted = plant(visits, annots, no_arrival, args.fixture, args.window,
                    annotated=not args.not_annotated)
    if planted is not None:
        print(planted[1])
        if not planted[0]:
            return 2
        flat = [ev for visit in visits for ev in visit]
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
    ordinal = args.not_annotated
    if ordinal:
        join, why = ordinal_join(visits, truth)
        print("I1555 REFERENCE not annotated: there is no testers/i1511_annotations CSV for "
              "%s, so no detection is joined to a throw spatially and nothing below is a "
              "spatial figure. Joined by VISIT ORDER instead (ordinal_join): %d thrown, "
              "arrivals=%d, truth visits %d | detected_visits=%d"
              % (args.fixture, truth_throws, len(arrivals), len(truth), len(visits)))
        if join is None:
            print("I1555 JOIN-REFUSED %s: %s -- no figure is taken off this run" % (tag, why))
            return 2
        for t, throws in enumerate(truth):
            k = len(visits[t]) if t < len(visits) else 0
            print("I1555 JOIN-VISIT %s truth=v%d thrown=%s detected=%s published=%s %s"
                  % (tag, t + 1, ",".join(norm(x) for x in throws),
                     "v%d" % (t + 1) if t < len(visits) else "-",
                     ",".join(norm(ev.pub["score"]) for ev in visits[t]) if k else "-",
                     "paired in order" if k == len(throws) else
                     "SHORT: %d of %d published, %d placements judged, none chosen"
                     % (k, len(throws), len(join["placements"][(t + 1, 1)]))))
    else:
        print("I1555 REFERENCE annotated_throws=%d of %d thrown, arrivals=%d, covering "
              "truth visits %s of %d | detected_visits=%d"
              % (len(annots), truth_throws, len(arrivals),
                 "%d-%d" % (annotated_visits[0], annotated_visits[-1])
                 if annotated_visits else "none", len(truth), len(visits)))
    if not covers_all and not ordinal:
        print("I1555 REFERENCE-GAP the annotation stops at truth visit %d while the run "
              "detected %d visits, so the matcher's monotone truth-visit range is free to "
              "slide: an unclaimed detection here is usually an UNANNOTATED throw and the "
              "columns below are about the annotated subset alone"
              % (annotated_visits[-1] if annotated_visits else 0, len(visits)))

    assignment = join if ordinal else axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]
    suspect = set(k for k, _ in assignment["suspect"])

    vote_counts, geo_counts, first_counts, pub_counts = {}, {}, {}, {}
    # The counterfactual, REPORTED and nothing more: what geometry-first would score if a
    # solve that not one placed tip corroborated were refused back to the vote. #1512
    # left promoting the tip to a CONSTRAINT as a later decision to be taken on numbers
    # rather than in passing; this is the smallest thing one could do with the tip short
    # of that, and it is measured here so the decision to leave it alone is a measured
    # one. What the measurement says on this tree: it buys ONE dart in rig-20260918's dev
    # window (15/17 -> 16/17) and NOTHING in the opening window, where the same fixture
    # has no uncorroborated solve at all. A rule that helps in one calibration window of
    # one fixture and is inert in the other is fitted to a dart, not measured.
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
    for (v, ei) in (assignment["unmatched"] if not ordinal else []):
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
    for (v, ei) in (assignment["unmatched"] if ordinal else []):
        p = visits[v][ei].pub
        print("I1555 UNPLACED v%d#%d vote=%s geo=%s published=%s path=%s degraded=%d "
              "outcome=%s -- not annotated: one of truth visit %d's %d throws, and which "
              "one the ordinal join does not say"
              % (v + 1, ei + 1, norm(p["vote"]),
                 norm(p["geo"]) if p["geo"] != "NONE" else p["outcome"],
                 norm(p["score"]), p["path"], 1 if p["degraded"] else 0, p["outcome"],
                 v + 1, len(truth[v])))
    geo_silent_on_phantoms = sum(
        1 for (v, ei) in assignment["unmatched"]
        if visits[v][ei].pub is not None and visits[v][ei].pub["geo"] == "NONE")
    if ordinal:
        print("I1555 PHANTOM-HANDLING not annotated: the ordinal join takes every "
              "publication of a visit to be one of its throws, so no detection is unclaimed "
              "and phantoms are not measured on this fixture; %d publication(s) of short "
              "visits are unplaced, not unclaimed" % len(assignment["unmatched"]))
    else:
        print("I1555 PHANTOM-HANDLING unclaimed_detections=%d | the geometry refused %d "
              "of them by name (a dart that is not on the board has no second constraint, "
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

    print("I1555 SCORECARD %s matched=%d of %d %s, %d thrown"
          % (tag, matched, len(arrivals),
             "arrival(s), not annotated: paired by the ordinal join in visits that "
             "published every throw" if ordinal else "annotated arrival(s)", truth_throws))
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
    acc = accuracy_ordinal if ordinal else accuracy
    results = acc(args.fixture, args.window, visits, annots, arrivals, assignment,
                  motion_clock(args.log))
    (detection_ordinal if ordinal else detection_and_cameras)(
        args.fixture, args.window, motion_clock(args.log), visits, annots, arrivals,
        assignment, results, read_camera_evidence(args.log, visits))
    counterfactual_rules(args.fixture, args.window, visits, annots, arrivals, assignment,
                         motion_clock(args.log), acc=acc)
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

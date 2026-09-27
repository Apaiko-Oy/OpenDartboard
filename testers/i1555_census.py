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
    pool_accuracy(files)
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
    for key in arrivals:
        thrown = norm(list(annots[key].values())[0]["thrown"])
        dart = "v%d.%d thrown=%s" % (key[0], key[1], thrown)
        pos = by_key.get(key)
        if pos is not None:
            bucket, reason = judge(thrown, published(pos))
            counts[bucket] += 1
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
            if bucket != "correct":
                also = ("; wrong under every order-preserving placement: %s" % ", ".join(
                    "%s -> %s" % (name(p), o[0]) for o, p in outcomes[1:])) if cands else ""
                named.append("%s published=- %s (%s%s)"
                             % (dart, bucket.upper(), reason, also))
            continue
        counts["ambiguous"] += 1
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


def counterfactual_rules(fixture, window, visits, annots, arrivals, assignment, clock):
    """#1657: every candidate rule's ACCURACY on this run's own rows, and each matched
    dart whose verdict differs from rule A's. REPORTED, wired nowhere."""
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
        accuracy(fixture, window, visits, annots, arrivals, assignment, clock,
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
            "ambiguous", "phantoms", "scoring_phantoms"]
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
        print("I1555 ACCURACY %s over %d run(s): correct %s/%d (%s) | %s | wrong-score "
              "%d, undetected %d, off-board-scored %d, ambiguous %d | phantoms %d "
              "(%d scoring)"
              % (label, len(group), "%d..%d" % (c, c + amb) if amb else "%d" % c, n,
                 "%.1f%%" % (100.0 * c / n) if n else "n/a", target_line(c, n),
                 t["wrong_score"], t["undetected"], t["offboard_scored"], amb,
                 t["phantoms"], t["scoring_phantoms"]))
    line("POOLED", rows)
    for f in sorted(set(r.get("fixture", "?") for r in rows)):
        line("fixture=%s" % f, [r for r in rows if r.get("fixture", "?") == f])


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
    accuracy(args.fixture, args.window, visits, annots, arrivals, assignment,
             motion_clock(args.log))
    counterfactual_rules(args.fixture, args.window, visits, annots, arrivals, assignment,
                         motion_clock(args.log))
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

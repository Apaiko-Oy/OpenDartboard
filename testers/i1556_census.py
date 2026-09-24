"""#1556: the flag census -- how many darts say their uncertainty reaches a wire, and
whether the wrong answers are among them.

    python3 i1556_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d] [--min-matched N]
    python3 i1556_census.py --pool <census output> [<census output> ...]

THE POINT OF THE SLICE IS THE CENSUS, so this file reports four things and decides none of
them:

    FLAG RATE      how many published darts carry a flag, over every called dart and over
                   the matched subset. Stated honestly and without apology: a rule that
                   flags most darts is useless to a human, and if that is what the
                   instrument measures then that is what this prints.
    CATCH          the contingency table a reader actually needs -- flagged against
                   wrongly-scored, on the matched darts. The slice's whole claim is that
                   wrong answers concentrate in the flagged set, and the only honest way
                   to say it is with four numbers and the names of the darts in each cell.
    THE COMPARISON #1555's rule (`boundaryMm` against the MAJOR AXIS of the error ellipse)
                   is computed on the SAME dart in the SAME process, so the two flag rates
                   are one measurement rather than two runs. `published=` on each line
                   says which of the two the run demoted on.
    THE SWEEP      the flag rate and the catch at every crossing threshold from 0.25 to
                   2.0 sigmas, recomputed from each dart's own `z`. The constant in the
                   header is 1.0 because that is what the published confidence vocabulary
                   already means; the table is here so moving it is a decision somebody
                   takes on numbers rather than a constant somebody nudges.

AND THE FLOOR'S OWN EVIDENCE. `Params::sigmaAcrossFloorMm` is a measurement (#1511's
finding one stage on: a claimed sigma understates what a reference can see), so this
census prints the distribution of claimed across-boundary sigmas beside the
solved-vs-annotated position error on the same darts. A floor that has stopped matching
the instrument is then visible rather than inherited.

WHAT A READER MUST CARRY ABOUT rig-20260922: only 9 of its 24 throws are annotated
(#1585), so its accuracy columns are about the annotated subset and about nothing else --
the CATCH table on that fixture is reported and is not evidence. Its FLAG RATE is a
coverage figure and is honest, because a flag needs no annotation to be counted.

A reporter in i1510p2_census.py's, i1512_census.py's and i1555_census.py's mould: it
decides nothing about the numbers, and the exit status is about whether the run could be
read at all.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402  (the matcher, #1504's alignment)
import i1555_census as publish_census  # noqa: E402  (norm/verdict, one vocabulary)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
FLAG_RE = re.compile(
    r"I1556FLAG window=(-?\d+) solved=(\d) score=(\S+) alt=(\S+) kind=(\S+) "
    r"boundary=([-0-9.]+) sigmaAcross=([-0-9.]+) sigmaRadial=([-0-9.]+) "
    r"sigmaTangent=([-0-9.]+) z=([-0-9.]+) sigmaMajor=([-0-9.]+) sigmaMinor=([-0-9.]+) "
    r"sigmaTheta=([-0-9.]+) r=([-0-9.]+) phi=([-0-9.]+) flag=(\d) crude=(\d) "
    r"published=(\S+)"
)
# `board=` is the score the BOARD published; `score=` is the crossing's subject. They are
# two fields because a vote publish has no crossing at all, so its `score=` reads "-" --
# and the first run of this census took the published score off that field, which read the
# three correctly-scored vote publishes in rig-20260918's dev window as wrong and turned a
# catch table of 1 of 2 into 1 of 5. Every verdict below is taken on `board`.
PUB_RE = re.compile(
    r"I1556PUBLISH window=(-?\d+) geometry=(\d) flagged=(\d) score=(\S+) alt=(\S+) "
    r"kind=(\S+) boundary=([-0-9.]+) sigma=([-0-9.]+) conf=([-0-9.]+) board=(\S+)"
)
END_RE = re.compile(r"SCORE:\s+END\b")

norm = publish_census.norm
verdict = publish_census.verdict

SWEEP = [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0]


def read_blocks(path):
    """One dict per called dart, in arrival order: what the SOLVER measured about the
    crossing (I1556FLAG) and what the BOARD published about it (I1556PUBLISH).

    Bound the way i1555_census binds its publish blocks -- the k-th non-END SCORE line of
    the log is the k-th event, and both lines for that dart are printed before it. They
    are two lines rather than one because they answer different questions: on a degraded
    dart the solver measured nothing at all and the board still published something.
    """
    blocks, pending, ends = [], {}, 0
    for raw in open(path, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = FLAG_RE.search(line)
        if m:
            pending["solver"] = {
                "window": int(m.group(1)), "solved": m.group(2) == "1",
                "score": m.group(3), "alt": m.group(4), "kind": m.group(5),
                "boundary": float(m.group(6)), "sigmaAcross": float(m.group(7)),
                "sigmaRadial": float(m.group(8)), "sigmaTangent": float(m.group(9)),
                "z": float(m.group(10)), "sigmaMajor": float(m.group(11)),
                "sigmaMinor": float(m.group(12)), "sigmaTheta": float(m.group(13)),
                "r": float(m.group(14)), "phi": float(m.group(15)),
                "flag": m.group(16) == "1", "crude": m.group(17) == "1",
                "publishedRule": m.group(18),
            }
            continue
        p = PUB_RE.search(line)
        if p:
            pending["board"] = {
                "window": int(p.group(1)), "geometry": p.group(2) == "1",
                "flagged": p.group(3) == "1", "score": p.group(4), "alt": p.group(5),
                "kind": p.group(6), "boundary": float(p.group(7)),
                "sigma": float(p.group(8)), "conf": float(p.group(9)),
                "board": p.group(10),
            }
            continue
        if END_RE.search(line):
            ends += 1
            continue
        sm = axis_census.SCORE_RE.search(line)
        if sm and sm.group(1) != "END":
            blocks.append(pending if pending else None)
            pending = {}
    return blocks, ends


def med(xs):
    return sorted(xs)[len(xs) // 2] if xs else float("nan")


def p90(xs):
    return sorted(xs)[min(len(xs) - 1, int(len(xs) * 0.9))] if xs else float("nan")


def rate(n, d):
    return "%d/%d (%s)" % (n, d, "%.0f%%" % (100.0 * n / d) if d else "n/a")


def pool(files):
    rows = []
    for path in files:
        for raw in open(path, "r", errors="replace"):
            if "I1556 TALLY " not in raw:
                continue
            fields = {}
            for token in ANSI.sub("", raw).split("I1556 TALLY ", 1)[1].split():
                if "=" in token:
                    k, v = token.split("=", 1)
                    fields[k] = v
            rows.append(fields)
    if not rows:
        print("I1556 POOL: no TALLY lines in %s -- nothing to pool" % ", ".join(files))
        return 2
    keys = ["called", "geometric", "flagged", "crude", "matched", "wrong",
            "wrong_flagged", "right_flagged", "unnameable",
            "geo_matched", "geo_wrong", "geo_wrong_flagged"]
    total = dict((k, 0) for k in keys)
    for row in rows:
        print("I1556 POOL-ROW fixture=%s window=%s %s"
              % (row.get("fixture", "?"), row.get("window", "?"),
                 " ".join("%s=%s" % (k, row.get(k, "0")) for k in keys)))
        for k in keys:
            total[k] += int(row.get(k, 0))
    print("I1556 POOLED over %d run(s): %d called dart(s), %d published geometrically, "
          "flagged %s of those | #1555 major-axis rule would flag %s"
          % (len(rows), total["called"], total["geometric"],
             rate(total["flagged"], total["geometric"]),
             rate(total["crude"], total["geometric"])))
    print("I1556 POOLED-CATCH over %d matched dart(s): %d wrongly scored, of which %s "
          "flagged | %s of the correctly-scored darts flagged too"
          % (total["matched"], total["wrong"],
             rate(total["wrong_flagged"], total["wrong"]),
             rate(total["right_flagged"], max(0, total["matched"] - total["wrong"]))))
    print("I1556 POOLED-CATCH-GEOMETRIC of the %d matched dart(s) the GEOMETRY published, "
          "%d are wrongly scored and %s flagged | the other %d were published by the "
          "string vote, which measures no millimetres and can never flag"
          % (total["geo_matched"], total["geo_wrong"],
             rate(total["geo_wrong_flagged"], total["geo_wrong"]),
             total["matched"] - total["geo_matched"]))
    if total["wrong"] and total["wrong_flagged"] == total["wrong"]:
        print("I1556 POOLED-VERDICT every wrongly-scored dart in the pooled reference is "
              "in the flagged set")
    elif total["wrong"]:
        print("I1556 POOLED-VERDICT %d wrongly-scored dart(s) are NOT flagged -- the flag "
              "does not catch every wrong answer, and that is the number"
              % (total["wrong"] - total["wrong_flagged"]))
    else:
        print("I1556 POOLED-VERDICT no wrongly-scored dart in the pooled reference, so "
              "the catch column measured nothing (#1490)")
    if total["unnameable"]:
        print("I1556 POOLED-UNNAMEABLE %d crossing(s) were measured and could not name a "
              "second candidate, so they published unflagged" % total["unnameable"])
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None)
    ap.add_argument("--log")
    ap.add_argument("--truth")
    ap.add_argument("--annotations")
    ap.add_argument("--fixture")
    ap.add_argument("--window", default="?")
    ap.add_argument("--no-arrival", default="")
    ap.add_argument("--min-matched", type=int, default=1)
    ap.add_argument("--expect-empty", action="store_true",
                    help="the mutation run: assert the flag census is EMPTY and fail "
                         "loudly if it is not")
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
    blocks, ends = read_blocks(args.log)
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, blocks):
        ev.flagblock = block
    for ev in flat:
        if not hasattr(ev, "flagblock"):
            ev.flagblock = None
    called = [ev for ev in flat if ev.flagblock and ev.flagblock.get("board")]
    tag = "fixture=%s window=%s" % (args.fixture, args.window)
    if not called:
        print("I1556 CENSUS %s: no I1556PUBLISH lines in %s -- was the run made with "
              "OD_GEO_SCORE=on?" % (tag, args.log))
        return 2
    rules = set(ev.flagblock["solver"]["publishedRule"] for ev in called
                if ev.flagblock.get("solver"))
    rule = "/".join(sorted(rules)) if rules else "?"
    print("I1556 CENSUS %s events=%d called=%d ends=%d rule=%s"
          % (tag, len(flat), len(called), ends, rule))

    # ---- THE FLAG RATE, over every called dart -----------------------------------------
    geometric = [ev for ev in called if ev.flagblock["board"]["geometry"]]
    flagged = [ev for ev in geometric if ev.flagblock["board"]["flagged"]]
    crude = [ev for ev in called
             if ev.flagblock.get("solver") and ev.flagblock["solver"]["crude"]]
    unnameable = [ev for ev in called
                  if ev.flagblock.get("solver") and ev.flagblock["solver"]["flag"] is False
                  and ev.flagblock["solver"]["z"] >= 0.0
                  and ev.flagblock["solver"]["z"] <= 1.0
                  and ev.flagblock["solver"]["alt"] == "-"]
    print("I1556 FLAG-RATE called=%d published_geometrically=%d flagged=%s | #1555 "
          "major-axis rule on the same darts: %s"
          % (len(called), len(geometric), rate(len(flagged), len(geometric)),
             rate(len(crude), len(geometric))))
    if len(geometric) and len(flagged) * 2 > len(geometric):
        print("I1556 FLAG-RATE-NOTE more than half the published darts flag. At this "
              "instrument's precision most darts really do sit within one sigma of some "
              "wire, so a consumer that asks a human on every flag asks on most darts -- "
              "the honest reading of the field is `this call was close`, with the "
              "millimetres beside it")
    if unnameable:
        print("I1556 UNNAMEABLE %d dart(s) crossed by the measurement could not name a "
              "second candidate and published UNFLAGGED" % len(unnameable))

    # ---- WHAT KIND OF WIRE, and the numbers the floor is judged on ----------------------
    kinds = {}
    zs, sigmas, majors = [], [], []
    for ev in called:
        s = ev.flagblock.get("solver")
        if not s or not s["solved"]:
            continue
        kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
        if s["z"] >= 0.0:
            zs.append(s["z"])
            sigmas.append(s["sigmaAcross"])
            majors.append(s["sigmaMajor"])
    if sigmas:
        print("I1556 UNCERTAINTY across-boundary sigma n=%d median=%.1f p90=%.1f mm | the "
              "major axis on the same darts median=%.1f p90=%.1f mm | boundary distance "
              "in sigmas median=%.2f"
              % (len(sigmas), med(sigmas), p90(sigmas), med(majors), p90(majors), med(zs)))
    print("I1556 BOUNDARY-KIND " +
          ("; ".join("%s=%d" % kv for kv in sorted(kinds.items())) or "none"))

    # ---- THE CATCH: flagged against wrongly scored, on the matched darts ----------------
    assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]
    suspect = set(k for k, _ in assignment["suspect"])
    annotated_visits = sorted({k[0] for k in annots.keys()})
    covers_all = len(annotated_visits) >= len(truth)
    if not covers_all:
        print("I1556 REFERENCE-GAP the annotation covers truth visits %s of %d while the "
              "run detected %d, so the CATCH table below is about the annotated subset "
              "alone and the FLAG RATE above is the figure this fixture can carry"
              % ("%d-%d" % (annotated_visits[0], annotated_visits[-1])
                 if annotated_visits else "none", len(truth), len(visits)))

    # A VOTE publish cannot flag at all -- it measures no board-millimetre position -- so
    # a catch rate over every matched dart is partly a figure about how often the solver
    # refused. The geometric subset is counted apart for that reason, and both are printed:
    # the first is what a consumer sees, the second is what the flag itself can be judged
    # on.
    matched = wrong = wrong_flagged = right_flagged = 0
    geo_matched = geo_wrong = geo_wrong_flagged = 0
    sweep_rows = []
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            if (v, ei) not in assigned or ev.flagblock is None or (v, ei) in suspect:
                continue
            board = ev.flagblock.get("board")
            if board is None:
                continue
            key = assigned[(v, ei)]
            thrown = norm(list(annots[key].values())[0]["thrown"])
            got = norm(board["board"])
            how = verdict(got, thrown)
            matched += 1
            solver = ev.flagblock.get("solver") or {}
            is_wrong = how != "exact"
            if is_wrong:
                wrong += 1
            if board["geometry"]:
                geo_matched += 1
                if is_wrong:
                    geo_wrong += 1
                    if board["flagged"]:
                        geo_wrong_flagged += 1
            if board["flagged"]:
                if is_wrong:
                    wrong_flagged += 1
                else:
                    right_flagged += 1
            # The alternative is the question a consumer would ask a human. Where the
            # dart is wrong, whether the ALTERNATIVE is the thrown score is the sharpest
            # thing this census can say: a flag whose other candidate is also wrong would
            # send a human to the wrong answer.
            alt = norm(board["alt"]) if board["alt"] != "-" else "-"
            rescue = "alt=right" if (is_wrong and alt == thrown) else (
                "alt=wrong" if (is_wrong and alt != "-") else "-")
            print("I1556 PAIR v%d.%d thrown=%s published=%s %s flagged=%d alt=%s kind=%s "
                  "boundary=%.2f sigma=%.2f z=%.2f conf=%.2f geometry=%d crude=%d %s"
                  % (key[0], key[1], thrown, got, how, 1 if board["flagged"] else 0, alt,
                     board["kind"], board["boundary"], board["sigma"],
                     solver.get("z", -1.0), board["conf"],
                     1 if board["geometry"] else 0,
                     1 if solver.get("crude") else 0, rescue))
            if board["geometry"] and solver.get("z", -1.0) >= 0.0 and solver.get("alt", "-") != "-":
                sweep_rows.append((solver["z"], is_wrong))

    if not matched:
        print("I1556 CENSUS %s: nothing matched -- a census that compared nothing has "
              "measured nothing (#1490)" % tag)
        return 2

    right = matched - wrong
    print("I1556 CATCH matched=%d | wrongly scored %d, of which flagged %s | correctly "
          "scored %d, of which flagged %s"
          % (matched, wrong, rate(wrong_flagged, wrong), right,
             rate(right_flagged, right)))
    print("I1556 CATCH-GEOMETRIC of the %d matched dart(s) the GEOMETRY published, %d are "
          "wrongly scored and %s flagged | the other %d matched dart(s) were published by "
          "the string vote, which measures no millimetres and can never flag"
          % (geo_matched, geo_wrong, rate(geo_wrong_flagged, geo_wrong),
             matched - geo_matched))
    if wrong and wrong_flagged == wrong:
        print("I1556 VERDICT every wrongly-scored dart in this window is in the flagged "
              "set")
    elif wrong:
        print("I1556 VERDICT %d wrongly-scored dart(s) are NOT flagged -- the flag does "
              "not catch every wrong answer here, and that is the number"
              % (wrong - wrong_flagged))
    else:
        print("I1556 VERDICT no wrongly-scored dart matched in this window, so the catch "
              "column measured nothing (#1490)")

    # ---- THE SWEEP, recomputed from each dart's own z ------------------------------------
    if sweep_rows:
        for k in SWEEP:
            f = sum(1 for z, _ in sweep_rows if z <= k)
            w = sum(1 for z, bad in sweep_rows if bad)
            wf = sum(1 for z, bad in sweep_rows if bad and z <= k)
            print("I1556 SWEEP k=%.2f flagged=%s wrong_caught=%s"
                  % (k, rate(f, len(sweep_rows)), rate(wf, w)))
        print("I1556 SWEEP-NOTE the header keeps k=1.00 because that is what the published "
              "confidence vocabulary already means (#1555 wired 0.7 to `the sigma reaches "
              "a wire`). A k fitted to the row that catches the wrong darts most cheaply "
              "would be a rule fitted to %d dart(s)."
              % sum(1 for _, bad in sweep_rows if bad))

    print("I1556 TALLY fixture=%s window=%s called=%d geometric=%d flagged=%d crude=%d "
          "matched=%d wrong=%d wrong_flagged=%d right_flagged=%d unnameable=%d "
          "geo_matched=%d geo_wrong=%d geo_wrong_flagged=%d"
          % (args.fixture, args.window, len(called), len(geometric), len(flagged),
             len(crude), matched, wrong, wrong_flagged, right_flagged, len(unnameable),
             geo_matched, geo_wrong, geo_wrong_flagged))

    if args.expect_empty:
        # The mutation's own assertion, made where the numbers are: zeroing the
        # uncertainty must empty the flag census, and an empty census on a run that
        # called no darts would prove nothing, so both halves are checked.
        if not called:
            print("I1556 MUTATION-FAIL the zeroed run called no darts at all, so an empty "
                  "flag census measures nothing")
            return 1
        if flagged or crude:
            print("I1556 MUTATION-FAIL the zeroed run flagged %d dart(s) (and the "
                  "major-axis rule %d) -- zeroing the uncertainty must empty both"
                  % (len(flagged), len(crude)))
            return 1
        print("I1556 MUTATION-OK the zeroed run called %d dart(s) and flagged none, by "
              "either rule" % len(called))

    if matched < args.min_matched:
        print("I1556 CENSUS %s: %d matched darts against a floor of %d -- not an "
              "instrument (#1490)" % (tag, matched, args.min_matched))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

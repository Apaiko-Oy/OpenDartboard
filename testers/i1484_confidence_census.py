"""#1484: how a run's darts were SCORED, not only what they scored.

Reads one detector run's console output and prints the census. It is a reporter: it
decides nothing and asserts nothing about the numbers, which is the acceptance criterion
this issue was filed with. A threshold here would be a constant fitted to today's tree
(#1322), and the whole point of the three confidences is that they move BEFORE any score
becomes correct.

    python3 i1484_confidence_census.py --log <run.txt> --fixture <name> \
        [--lengths <clip-lengths.txt>] [--truth <GROUND-TRUTH.md>] [--caveat <sentence>]

Exit status is about whether the run could be READ, never about what it said: 0 when the
log was parsed, 2 when it was not a scoring run at all (nothing to report on, which a
caller must not mistake for "no darts"). The caller decides what to do with that.

WHAT IT READS, and each of these is a line this repository already prints:

    SCORE: <score> | Position: (x,y) | Confidence: <f> | Camera: <n> | Processing: ...
        scorer.cpp. `END` is published on the same line when the board goes CLEAN, which
        is how a visit is separated from the next one.
    BOARD: wedge measured|wedge by default | ring=... | segment=... | ...
        score_processing.cpp, logged for the SAME camera and the SAME decision the score
        string came from, just before the SCORE line. It is the independent witness for
        the 0.5 bucket: a census that read the confidence alone could not tell a defaulted
        wedge from a float that happened to print 0.5.
    [i803] cycle budget reached: cycles=N ...      the run stopped where it was TOLD to
    [i803] cam C pos_ms=P
    END OF FOOTAGE: ... after N cycles (M ms)      the run stopped where the CLIP did
    END OF FOOTAGE cam C last pos_ms=P

AND FROM THE TRUTH FILE, beside its throws table: the table under "Throws that produce no
event", which is the one thing the within-visit alignment may place a gap by (#1504). A
SCORE line carries no time and neither does the truth file, so without that record a
visit with fewer publications than throws is reported AMBIGUOUS per dart, never resolved
to whichever placement scores best. See alignments().

THE 0.5 BUCKET IS TWO THINGS AND THEY ARE NOT THE SAME FINDING. `by_default` is a wedge
nobody measured, published at 0.5 with #1346's fallback asserting the 20 -- a score that
is an assertion. A published `MISS` also carries 0.5, and it means no camera voted at all:
not a wrong wedge but no reading. Both are counted, separately, under one 0.5 heading.
"""

import argparse
import itertools
import os
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
SCORE_RE = re.compile(
    r"SCORE:\s+(\S+)\s+\|\s+Position:\s+\((-?\d+),(-?\d+)\)\s+\|\s+"
    r"Confidence:\s+([0-9.]+)\s+\|\s+Camera:\s+(-?\d+)"
)
BOARD_RE = re.compile(r"BOARD:\s+wedge (measured|by default)\s+\|\s+ring=(\S*)\s+\|\s+segment=(-?\d+)")
CAP_RE = re.compile(r"\[i803\] cycle budget reached: cycles=(\d+)\s+loop_ms=(\d+)")
CAP_POS_RE = re.compile(r"\[i803\] cam (\d+) pos_ms=(-?\d+)")
END_RE = re.compile(r"END OF FOOTAGE: every file source has reached its end after (\d+) cycles \((\d+) ms\)")
END_POS_RE = re.compile(r"END OF FOOTAGE cam (\d+) last pos_ms=(-?\d+)")

# A throw in the ground-truth table: 13, T13, D20, miss -- with the markdown emphasis the
# file writes trebles and misses in.
THROW_RE = re.compile(r"^(?:\*{1,2})?(miss|BULL|DBULL|[TDS]?\d{1,2})(?:\*{1,2})?$", re.IGNORECASE)
PUBLISHED_RE = re.compile(r"^([SDT])(\d{1,2})$")


def strip_ansi(line):
    return ANSI.sub("", line)


class Dart(object):
    def __init__(self, score, confidence, camera, position):
        self.score = score
        self.confidence = confidence
        self.camera = camera
        self.position = position
        self.wedge = None          # "measured" / "by default", from the BOARD line beside it
        self.ring = None
        self.segment = None


def read_run(path):
    """The run, as visits of darts, plus how and where it stopped."""
    visits = [[]]
    pending_board = None
    stop = {"how": "unknown", "cycles": None, "loop_ms": None, "pos_ms": {}}
    reached_loop = False
    for raw in open(path, "r", errors="replace"):
        line = strip_ansi(raw.rstrip("\n"))
        m = BOARD_RE.search(line)
        if m:
            pending_board = m
            continue
        m = SCORE_RE.search(line)
        if m:
            score, x, y, conf, cam = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
            if score == "END":
                visits.append([])
                pending_board = None
                continue
            dart = Dart(score, float(conf), int(cam), (int(x), int(y)))
            if pending_board is not None:
                dart.wedge = pending_board.group(1)
                dart.ring = pending_board.group(2)
                dart.segment = int(pending_board.group(3))
                pending_board = None
            visits[-1].append(dart)
            continue
        m = CAP_RE.search(line)
        if m:
            stop["how"] = "cycle-cap"
            stop["cycles"] = int(m.group(1))
            stop["loop_ms"] = int(m.group(2))
            continue
        m = END_RE.search(line)
        if m:
            stop["how"] = "end-of-footage"
            stop["cycles"] = int(m.group(1))
            stop["loop_ms"] = int(m.group(2))
            continue
        m = CAP_POS_RE.search(line) or END_POS_RE.search(line)
        if m:
            stop["pos_ms"][int(m.group(1))] = int(m.group(2))
            continue
        if "Scorer running with" in line or "SCORING:" in line:
            reached_loop = True
    # A trailing visit with no darts in it is the empty tail after the last END, not a
    # visit the board was part way through.
    closed = len(visits) - 1
    if visits and not visits[-1]:
        visits.pop()
    return visits, closed, stop, reached_loop


def read_lengths(path):
    """clip -> duration_ms, as testers/i1484_clip_length.cpp printed them.

    Keyed by the WHOLE path and never by the basename, which is not a detail here: both
    fixtures in this repository call their clips cam_1.mp4, cam_2.mp4, cam_3.mp4, so a
    basename key silently reports the rig's 60 seconds against the shipped mocks' 143 and
    prints a share of a clip that was never played. Measured, on the first run of this
    harness: the rig read "0.0% of cam_1.mp4" against a length belonging to mocks/.
    """
    lengths = {}
    if not path or not os.path.exists(path):
        return lengths
    for line in open(path):
        parts = line.split()
        if len(parts) >= 4:
            try:
                lengths[parts[0]] = float(parts[3])
            except ValueError:
                pass
    return lengths


def normalise_throw(token):
    t = token.strip().strip("`").replace("*", "").strip()
    if t.lower() == "miss":
        return "MISS"
    t = t.upper()
    if t.startswith(("T", "D", "S")) and t[1:].isdigit():
        return t
    if t.isdigit():
        return "S" + t
    return t


def read_truth(path):
    """The throws table of a GROUND-TRUTH.md, as a list of visits of throw strings.

    A table row counts when its first cell is a visit number and EVERY other cell is a
    throw token. That is what keeps the comparison table further down the same file out
    of this: its rows are prose and backticks, and it has three cells rather than four.
    """
    visits = []
    for raw in open(path, "r", errors="replace"):
        line = raw.strip()
        if not line.startswith("|") or not line.endswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 2 or not cells[0].isdigit():
            continue
        throws = cells[1:]
        if not all(THROW_RE.match(c.strip().strip("`")) for c in throws):
            continue
        visits.append((int(cells[0]), [normalise_throw(c) for c in throws]))
    visits.sort(key=lambda v: v[0])
    return visits


# The heading GROUND-TRUTH.md records its eventless throws under. The table beneath it is
# visit | throw | what the footage shows, and a row is read only while it is under this
# heading, so a table elsewhere in the file that happens to start with two numbers is not
# mistaken for one.
NO_EVENT_HEADING = re.compile(r"^#+\s+Throws that produce no event", re.IGNORECASE)


def read_no_event(path):
    """{visit: {throw index from 0: what the footage shows}} -- the throws the truth file
    records as producing no event on the board, from the recording and not from any run.

    This is the only thing the within-visit alignment below may use to place a gap, and it
    is deliberately a fact about the FOOTAGE: a dart thrown off the board, a dart that lands
    as the clip ends. It says nothing about what any detector published, so a run cannot
    make it say something more convenient.
    """
    recorded = {}
    under = False
    for raw in open(path, "r", errors="replace"):
        line = raw.strip()
        if line.startswith("#"):
            under = bool(NO_EVENT_HEADING.match(line))
            continue
        if not under or not line.startswith("|") or not line.endswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 3 or not cells[0].isdigit() or not cells[1].isdigit():
            continue
        recorded.setdefault(int(cells[0]), {})[int(cells[1]) - 1] = cells[2]
    return recorded


def alignments(found, throws, eventless):
    """Every alignment of one visit's publications to its throws this harness can DEFEND,
    and why. A list of (pairs, rule), pairs being [(publication index, throw index)].

    Never the one that scores best. The rule is built only from what the data carries:

      * Within a visit, publications keep the order of the throws. The board publishes a
        dart when the state moves DART_n -> DART_n+1, so it cannot publish a dart before
        the one thrown ahead of it. Only ORDER-PRESERVING alignments exist.
      * A SCORE line carries no timestamp and GROUND-TRUTH.md no per-throw time (#1504
        measured both), so nothing can say WHICH throw a missing publication belongs to --
        except the truth file's own record of throws that produce no event.

    So: as many publications as throws has one order-preserving alignment, the positional
    one, and it is determined. Fewer publications than throws is determined only when the
    truth file records exactly the missing number of eventless throws in that visit; else
    every order-preserving placement of the gaps is returned and the caller reports each
    dart that differs between them as AMBIGUOUS. More publications than throws is not this
    rule's to resolve -- it is either a phantom or a merged visit (#1552) -- and keeps the
    positional reading it always had, labelled as such.
    """
    k, n = len(found), len(throws)
    if k == n:
        return [(list(zip(range(k), range(n))), "determined")]
    if k > n:
        return [(list(zip(range(n), range(n))), "more-published")]
    recorded = [t for t in range(n) if t in eventless]
    # The recorded gaps are gaps; the rest, if any remain, may fall on any other throw.
    # A record naming MORE gaps than the run left is a record and a run that disagree --
    # the board published something for a throw the footage says made no event -- and
    # then the record places nothing.
    candidates = [t for t in range(n) if t not in eventless] if len(recorded) <= n - k else list(range(n))
    placements = list(itertools.combinations(candidates, k))
    if len(placements) == 1 and len(recorded) == n - k:
        return [(list(zip(range(k), placements[0])), "recorded-gap")]
    return [(list(zip(range(k), slots)), "ambiguous") for slots in placements]


def verdict(published, thrown):
    """correct / off-board / no-vote / ring / wedge -- a description, never a judgement."""
    if thrown == "MISS":
        return "correct" if published == "OUTER" else ("no-vote" if published == "MISS" else "on-board")
    if published == thrown:
        return "correct"
    if published == "OUTER":
        return "off-board"
    if published == "MISS":
        return "no-vote"
    p, t = PUBLISHED_RE.match(published), PUBLISHED_RE.match(thrown)
    if p and t and p.group(2) == t.group(2):
        return "ring"
    return "wedge"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--clips", default="")
    ap.add_argument("--lengths", default="")
    ap.add_argument("--truth", default="")
    ap.add_argument("--caveat", default="")
    args = ap.parse_args()

    if not os.path.exists(args.log):
        print("FAIL census: there is no run log at %s, so nothing was measured" % args.log)
        return 2

    visits, closed, stop, reached_loop = read_run(args.log)
    darts = [d for v in visits for d in v]

    print("=== FIXTURE %s ===" % args.fixture)
    print("    log:     %s" % args.log)
    if args.caveat:
        print("    caveat:  %s" % args.caveat)

    if not reached_loop and not darts:
        print("FAIL census: this run never reached the scoring loop, so it holds no darts to")
        print("             report on. That is not a census of nought -- it is no census.")
        return 2

    # ---- how much of the fixture was consumed (the trap this issue names) ---------------
    lengths = read_lengths(args.lengths)
    clips = [c for c in args.clips.split(",") if c]
    print()
    print("--- how much of the fixture this run consumed ---")
    if stop["how"] == "cycle-cap":
        print("    THE RUN ENDED ON THE CYCLE CAP, not on the end of the footage: it stopped")
        print("    after %s cycles because OD_MAX_CYCLES told it to. Every number below is a" % stop["cycles"])
        print("    census of the OPENING of this fixture and not of the whole of it, and the")
        print("    last line of the run is an ordinary END exactly like a visit that finished.")
    elif stop["how"] == "end-of-footage":
        print("    The run ended where the FOOTAGE ended, after %s cycles: the whole of this" % stop["cycles"])
        print("    fixture was played.")
    else:
        print("    The run ended for neither of the two reasons this harness can name -- no")
        print("    cycle-budget line and no end-of-footage line. It was killed, or it faulted;")
        print("    how much of the fixture it saw is UNKNOWN and the numbers below are of an")
        print("    unknown share of it.")
    if stop["loop_ms"] is not None:
        print("    loop_ms=%s" % stop["loop_ms"])
    # The stream position each camera stopped on. At the END of the footage the detector's
    # own number is 0 or -1 and that is not a camera that played nothing: the position is
    # taken from the read that FAILED, which is how the loop learned the file had ended. So
    # the two stops are reported differently, because the same number means two things.
    for index in sorted(stop["pos_ms"]):
        pos = stop["pos_ms"][index]
        clip = clips[index] if index < len(clips) else ""
        total = lengths.get(clip)
        name = os.path.basename(clip) if clip else "camera %d" % index
        if stop["how"] == "end-of-footage" and pos <= 0:
            if total:
                print("    cam %d played %s to its end, all %d ms of it" % (index, name, total))
            else:
                print("    cam %d played %s to its end (its length was not measured)" % (index, name))
            continue
        if total:
            print("    cam %d stopped at %8d ms of %8d ms  (%5.1f%% of %s)"
                  % (index, pos, total, 100.0 * pos / total, name))
        else:
            print("    cam %d stopped at %8d ms  (the length of %s is not known here)"
                  % (index, pos, name))
    if not stop["pos_ms"]:
        print("    no camera reported a stream position, so the share consumed is unknown")

    # ---- the census itself --------------------------------------------------------------
    at = lambda f: [d for d in darts if abs(d.confidence - f) < 0.01]
    nine, seven = at(0.9), at(0.7)
    five = at(0.5)
    five_default = [d for d in five if d.score != "MISS"]
    five_miss = [d for d in five if d.score == "MISS"]
    other = [d for d in darts if d not in nine and d not in seven and d not in five]
    by_default_board = [d for d in darts if d.wedge == "by default"]

    print()
    print("--- how these darts were scored ---")
    print("    0.9  two or more cameras MEASURED a wedge and agreed          %4d" % len(nine))
    print("    0.7  measured, but no two agreed (published is measured[0])   %4d" % len(seven))
    print("    0.5  by_default: NO camera measured a wedge, #1346 asserted   %4d" % len(five_default))
    print("    0.5  MISS: no camera voted at all, so there is no reading     %4d" % len(five_miss))
    if other:
        print("    other confidences (this harness does not know them)          %4d" % len(other))
        for d in other:
            print("         %s at %.3f" % (d.score, d.confidence))
    print("    ----------------------------------------------------------- ----")
    print("    darts detected                                               %4d" % len(darts))
    print("    visits: %d seen, %d closed by an END line" % (len(visits), closed))
    print("    cross-check, from the BOARD line beside each dart:")
    print("        darts whose wedge the board itself called 'by default'    %4d" % len(by_default_board))
    # The BOARD line is the independent witness, logged for the SAME camera and the same
    # decision the score string came from. A dart published at 0.7 or 0.9 -- confidences
    # whose whole meaning is "a camera MEASURED a wedge" -- whose board line says the wedge
    # was not measured is not a rounding difference between two numbers. It is the two
    # halves of one decision disagreeing about what was read, and a reader taking the
    # confidence as a statement about the anchor would be reading it wrong.
    unmeasured_high = [d for d in (nine + seven) if d.wedge == "by default"]
    if unmeasured_high:
        print("        of the %d darts at 0.7 or 0.9, %d carry a board line saying the wedge"
              % (len(nine) + len(seven), len(unmeasured_high)))
        print("        was NOT measured. Those two confidences mean 'a camera measured a")
        print("        wedge', so on those darts they do not mean what they say:")
        kinds = {}
        for d in unmeasured_high:
            kinds[d.score] = kinds.get(d.score, 0) + 1
        for score in sorted(kinds):
            print("            %-8s %4d  at %s" % (score, kinds[score],
                  ", ".join(sorted(set("%.1f" % d.confidence for d in unmeasured_high if d.score == score)))))
    elif len(by_default_board) != len(five_default):
        print("        -- which is NOT the 0.5 count above, and the two are counted off the")
        print("           same darts. One of the two is saying something the other does not.")

    print()
    print("--- every dart, in the order it was published ---")
    for vi, visit in enumerate(visits, start=1):
        cells = ["%s@%.1f/cam%d%s" % (d.score, d.confidence, d.camera,
                                      "" if d.wedge != "by default" else "*")
                 for d in visit]
        print("    visit %-2d %s" % (vi, "  ".join(cells) if cells else "(no dart published)"))
    print("             (* the board called this wedge asserted rather than measured)")

    # ---- against the ground truth, visit by visit from the first --------------------------
    if not args.truth:
        print()
        print("--- accuracy ---")
        print("    No ground-truth file for this fixture, so NOTHING here says whether a score")
        print("    is right. The census above is about how each score was decided and is the")
        print("    whole of what this fixture can answer.")
        print("CENSUS_RC=0")
        return 0

    truth = read_truth(args.truth)
    if not truth:
        print("FAIL census: %s holds no throws table this harness can read, so the accuracy" % args.truth)
        print("             half would silently report on nothing.")
        return 2
    widths = set(len(t[1]) for t in truth)
    numbers = [t[0] for t in truth]
    if numbers != list(range(1, len(numbers) + 1)):
        print("FAIL census: the throws table's visits are %s, which is not 1..N -- the" % numbers)
        print("             alignment below starts at visit 1 and would be measuring nothing.")
        return 2
    thrown_total = sum(len(t[1]) for t in truth)
    misses = sum(1 for _, throws in truth for t in throws if t == "MISS")
    no_event = read_no_event(args.truth)
    no_event_total = sum(len(v) for v in no_event.values())

    print()
    print("--- detection: darts detected against darts thrown, visit by visit from the first ---")
    print("    truth:  %s" % args.truth)
    print("            %d visits, %d throws (%d of them misses), %s per visit"
          % (len(truth), thrown_total, misses,
             "%d" % widths.pop() if len(widths) == 1 else "a varying number"))
    print("            %d throw%s recorded as producing NO EVENT on the board, from the footage%s"
          % (no_event_total, "" if no_event_total == 1 else "s",
             ": " + ", ".join("visit %d throw %d" % (v, t + 1)
                              for v in sorted(no_event) for t in sorted(no_event[v]))
             if no_event_total else ""))
    print()
    print("    visit  thrown  detected  note")
    detected_total = 0
    for index, (number, throws) in enumerate(truth):
        found = visits[index] if index < len(visits) else []
        detected_total += len(found)
        recorded = len(no_event.get(number, {}))
        note = ""
        if index >= len(visits):
            note = "the run ended before this visit"
        elif index == len(visits) - 1 and stop["how"] == "cycle-cap" and len(found) < len(throws):
            note = "the run was cut off DURING this visit by the cycle cap"
        elif len(found) < len(throws) and recorded == len(throws) - len(found):
            note = "fewer detected than thrown, and the footage records exactly that many throws as making no event"
        elif len(found) < len(throws):
            note = "fewer detected than thrown -- a DETECTION failure, not a wrong score"
        elif len(found) > len(throws):
            note = "more detected than thrown"
        print("      %-5d  %-6d  %-8d  %s" % (number, len(throws), len(found), note))
    if len(visits) > len(truth):
        for extra in range(len(truth), len(visits)):
            print("      %-5s  %-6s  %-8d  a visit the ground truth does not hold"
                  % ("-", "-", len(visits[extra])))
    print()
    print("    %d darts detected against %d thrown." % (detected_total, thrown_total))
    print("    Fewer detected than thrown is a DETECTION failure and is not a wrong score;")
    print("    where the run ended first it is neither, and the notes above say which.")

    # ---- scoring, aligned WITHIN a visit by a rule the data can defend (#1504) ----------
    #
    # Until #1504 this compared the k-th published dart with the k-th thrown, and one throw
    # that made no event -- a dart off the board, a dart landing as the clip ends -- shifted
    # every dart after it in its visit to wrong. See alignments() for the rule that
    # replaced it, and what it deliberately does NOT do: pick the placement that scores best.
    print()
    print("--- scoring: correct against published, over the darts that were detected ---")
    print("    A thrown miss counts CORRECT where the detector published OUTER.")
    print("    Within a visit, publications keep the order of the throws. Where fewer were")
    print("    published than thrown, a gap is placed only where the truth file records a")
    print("    throw that made no event; otherwise every placement is reported and a dart whose")
    print("    verdict differs between them is AMBIGUOUS and counted as neither.")
    print()
    tally = {}
    compared = 0
    correct = 0
    ambiguous = 0
    correct_low = correct_high = 0
    for index, (number, throws) in enumerate(truth):
        found = visits[index] if index < len(visits) else []
        if not found:
            continue
        options = alignments(found, throws, no_event.get(number, {}))
        rule = options[0][1]
        # What each publication reads against, under every alignment left standing.
        readings = []
        for p in range(len(found)):
            seen = []
            for pairs, _ in options:
                thr = dict(pairs).get(p)
                seen.append((throws[thr], verdict(found[p].score, throws[thr])) if thr is not None
                            else ("(none)", "unthrown"))
            readings.append(seen)
        counts = [sum(1 for p, t in pairs if verdict(found[p].score, throws[t]) == "correct")
                  for pairs, _ in options]
        correct_low += min(counts)
        correct_high += max(counts)
        cells = []
        for p, seen in enumerate(readings):
            pub = found[p].score
            # Ambiguity is about the VERDICT: a dart that is a wrong wedge against every
            # throw it could be is a wrong wedge, whichever of them it was.
            if len(set(v for _, v in seen)) == 1:
                v = seen[0][1]
                thr = "|".join(sorted(set(t for t, _ in seen)))
                if v != "unthrown":
                    tally[v] = tally.get(v, 0) + 1
                    compared += 1
                    correct += v == "correct"
                cells.append("%s vs %s [%s]" % (pub, thr, v))
            else:
                ambiguous += 1
                compared += 1
                cells.append("%s AMBIGUOUS {%s}" % (pub, ", ".join(
                    "vs %s [%s]" % (thr, v) for thr, v in sorted(set(seen)))))
        if rule in ("determined", "more-published", "recorded-gap"):
            # A throw with no publication is written where it was thrown, so the row reads
            # in throw order: visit 6 is `(none) vs MISS | S7 vs S7 | S2 vs S2`.
            placed = dict((t, p) for p, t in options[0][0])
            ordered = []
            for t in range(len(throws)):
                if t in placed:
                    ordered.append(cells[placed[t]])
                else:
                    ordered.append("(none) vs %s [%s]" % (
                        throws[t], "no event, recorded" if t in no_event.get(number, {}) else "undetected"))
            ordered.extend(cells[p] for p in range(len(found)) if p not in placed.values())
            cells = ordered
        else:
            cells.append("%d of %s undetected, and nothing says which" % (
                len(throws) - len(found), "/".join(throws)))
        label = {"determined": "", "more-published": "  (more published than thrown: positional, not aligned -- #1552)",
                 "recorded-gap": "  (gap placed by the truth file's record)",
                 "ambiguous": "  (%d placements, %d..%d correct)" % (len(options), min(counts), max(counts))}[rule]
        print("    visit %-2d %s%s" % (number, " | ".join(cells), label))
    print()
    print("    %d correct of %d compared." % (correct, compared))
    if ambiguous:
        print("    %d AMBIGUOUS: which throw a missing publication belongs to is not in the data," % ambiguous)
        print("    so those darts are counted neither right nor wrong. Every placement taken")
        print("    together gives %d..%d correct; this harness reports the range and picks none." % (correct_low, correct_high))
    for name in ("correct", "wedge", "ring", "off-board", "no-vote", "on-board"):
        if name in tally:
            print("        %-10s %4d   %s" % (name, tally[name], {
                "correct": "the published score is the dart that was thrown",
                "wedge": "a different number entirely",
                "ring": "the right number, the wrong ring -- single for treble and so on",
                "off-board": "published OUTER where a scoring dart was thrown",
                "no-vote": "published MISS: no camera voted on this dart at all",
                "on-board": "published a score where a MISS was thrown",
            }[name]))
    print()
    print("    This harness asserts nothing about any number above. It reports.")
    print("CENSUS_RC=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""turnaus#1537: what OD_VISIT_FILL does to a fixture replay, against the fixture's truth line.

    python3 i1537_census.py --log <run.txt> --fixture rig-20260918 --truth <GROUND-TRUTH.md> \
        --label r18-dev
    python3 i1537_census.py --log <run.txt> --fixture rig-20260929 --short-truth <GROUND-TRUTH.md> \
        --label r29-dev

WHAT IT READS. One detector run's SCORE lines, split into visits at each END, and the I1537
lines the scorer writes: `I1537 VISIT SHORT` at every END that closed a visit of one or two
darts (on and off alike), and `I1537 VISIT FILL` before each inferred MISS (on only). A SCORE
line that follows a VISIT FILL line is the inferred MISS; every other SCORE line was measured.

THE JOIN IS BY VISIT ORDER, and says so when it cannot be: detected visit i closed by an END
is truth visit i. That is i1555_census.py's ordinal join, and the same caveat holds: a visit
split in two by a false END, or two visits merged, shifts every later visit, so a run whose
closed visits outnumber the truth's, or whose visit published more darts than were thrown,
is reported MISALIGNED and its rows after the fault are not classified.

WHAT IT COUNTS, per short visit (fewer than three MEASURED darts at its END):

    unseen      3 - measured: what the fill infers, on or off
    inferred    the inferred MISSes this run really published (0 with the switch off)
    real-miss   of the unseen, how many the truth line shows as misses not already
                published as MISS by the detector -- an inferred MISS that is right
    board-dart  the rest: a dart the truth shows ON the board that the detector did not
                publish. The fill writes it as a MISS; without the fill it was nothing. No
                measured score is ever replaced (the fill only adds), so the score this
                dart should have carried was already lost -- the fill changes a hole into
                a nought, which a correction then has to change.
    phantom     an unseen dart in a visit whose truth line is three darts on the board AND
                whose three were all published across the visits the run made of it -- the
                only way the fill can invent a dart that was seen. Reported as board-dart
                otherwise; the target on rig-20260918 and rig-20260922 is 0.

A reporter: the exit status is about whether the run could be read, not about the figures.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SCORE_RE = re.compile(r"SCORE: (\S+) \| Position")
FILL_RE = re.compile(r"I1537 VISIT FILL: dart (\d+) of 3")
SHORT_RE = re.compile(r"I1537 VISIT SHORT: the visit closed with (\d+) of 3")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
THROW = re.compile(r"^(?:[sdt]?\d{1,2}|miss|bull|outer)$", re.IGNORECASE)


def norm(tok):
    t = tok.strip().strip("`").replace("*", "").upper()
    if t == "50":
        return "BULL"
    if t == "25":
        return "OUTER"
    if t.isdigit():
        return "S" + t
    return t


def read_table_truth(path):
    visits = []
    for raw in open(path, errors="replace"):
        line = raw.strip()
        if not (line.startswith("|") and line.endswith("|")):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) != 4 or not cells[0].isdigit():
            continue
        if not all(THROW.match(c.strip("`*").strip()) for c in cells[1:]):
            continue
        visits.append((int(cells[0]), [norm(c) for c in cells[1:]]))
    visits.sort()
    return [v for _, v in visits]


def read_short_truth(path):
    for raw in open(path, errors="replace"):
        line = raw.strip()
        if not line:
            continue
        out = []
        for chunk in line.split(","):
            toks = chunk.split()
            if not toks or not all(THROW.match(t) for t in toks):
                return None
            out.append([norm(t) for t in toks])
        return out
    return None


def read_run(path):
    """[(darts, closed, short_said, filled)], darts as (score, inferred)."""
    visits = [{"darts": [], "closed": False, "short": None}]
    pending_fill = 0
    for raw in open(path, errors="replace"):
        line = ANSI.sub("", raw)
        m = SHORT_RE.search(line)
        if m:
            visits[-1]["short"] = int(m.group(1))
            continue
        if FILL_RE.search(line):
            pending_fill += 1
            continue
        m = SCORE_RE.search(line)
        if not m:
            continue
        score = m.group(1)
        if score == "END":
            visits[-1]["closed"] = True
            visits.append({"darts": [], "closed": False, "short": None})
            pending_fill = 0
            continue
        inferred = pending_fill > 0 and score == "MISS"
        if inferred:
            pending_fill -= 1
        visits[-1]["darts"].append((score, inferred))
    if not visits[-1]["darts"]:
        visits.pop()
    return visits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--label", required=True)
    ap.add_argument("--truth")
    ap.add_argument("--short-truth")
    a = ap.parse_args()
    truth = read_table_truth(a.truth) if a.truth else read_short_truth(a.short_truth)
    if not truth:
        print("I1537 CENSUS %s unreadable truth" % a.label)
        return 2
    visits = read_run(a.log)
    if not visits:
        print("I1537 CENSUS %s no visits in %s" % (a.label, a.log))
        return 2

    tot = {"closed": 0, "short": 0, "unseen": 0, "inferred": 0, "real": 0, "board": 0, "phantom": 0,
           "said": 0}
    misaligned = None
    for i, v in enumerate(visits):
        measured = [s for s, inf in v["darts"] if not inf]
        inferred = sum(1 for _, inf in v["darts"] if inf)
        tline = truth[i] if i < len(truth) else None
        row = "I1537 VISIT %s v%d %s published=%s" % (
            a.label, i + 1, "closed" if v["closed"] else "open(no END)",
            " ".join(s + ("*" if inf else "") for s, inf in v["darts"]) or "-")
        row += " truth=%s" % (" ".join(tline) if tline else "-")
        if not v["closed"]:
            print(row + " -- not closed, nothing filled")
            continue
        tot["closed"] += 1
        if v["short"] is not None:
            tot["said"] += 1
        if tline is None and misaligned is None:
            misaligned = "the run closed visit %d where the truth has %d" % (i + 1, len(truth))
        if tline is not None and len(measured) > len(tline) and misaligned is None:
            misaligned = "visit %d published %d measured darts for %d thrown" % (i + 1, len(measured), len(tline))
        unseen = 3 - len(measured) if 0 < len(measured) < 3 else 0
        if unseen == 0:
            print(row + (" -- whole" if len(measured) >= 3 else " -- empty END, not filled"))
            continue
        tot["short"] += 1
        tot["unseen"] += unseen
        tot["inferred"] += inferred
        if misaligned is not None:
            print(row + " -- short by %d, inferred %d, UNCLASSIFIED (%s)" % (unseen, inferred, misaligned))
            continue
        truth_misses = sum(1 for t in tline if t == "MISS")
        published_misses = sum(1 for s in measured if s == "MISS")
        real = min(unseen, max(0, truth_misses - published_misses))
        board = unseen - real
        phantom = 0
        if board and truth_misses == 0:
            # Three darts on the board: was every one of them published somewhere? Only a
            # split visit can do that, and then the neighbouring visit holds the rest.
            nxt = visits[i + 1]["darts"] if i + 1 < len(visits) else []
            prv = visits[i - 1]["darts"] if i > 0 else []
            if len(measured) + len([d for d in nxt if not d[1]]) == 3 or \
               len(measured) + len([d for d in prv if not d[1]]) == 3:
                phantom = board
        tot["real"] += real
        tot["board"] += board - phantom
        tot["phantom"] += phantom
        print(row + " -- short by %d, inferred %d: real-miss %d, board-dart %d, phantom %d"
              % (unseen, inferred, real, board - phantom, phantom))
    print("I1537 CENSUS %s fixture=%s closed=%d short=%d said=%d unseen=%d inferred=%d real-miss=%d "
          "board-dart=%d phantom=%d truth-visits=%d cut-short-in-truth=%d%s" % (
              a.label, a.fixture, tot["closed"], tot["short"], tot["said"], tot["unseen"], tot["inferred"],
              tot["real"], tot["board"], tot["phantom"], len(truth),
              sum(1 for t in truth if len(t) < 3),
              (" MISALIGNED: " + misaligned) if misaligned else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

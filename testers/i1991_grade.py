"""turnaus#1991: a session's darts graded against its truth line -- what the board published,
or what a replay of its kept windows (OD_KEEP_WINDOWS, window_keep.hpp) reads now.

    python3 i1991_grade.py --truth <truth-line> --board
    python3 i1991_grade.py --truth <truth-line> --replay <replay-lines> [--expect k=v ...]
    python3 i1991_grade.py --truth <truth-line> --emit-board-replay   (the board's own reads, as replay lines)
    python3 i1991_grade.py --self-test

WHAT TRUTH IS, AND IS NOT. The truth line (turnaus#1786; read by i1789_truth_census.py's
read_truth, which this imports and does not copy) says what the board published for every
dart and what a person changed it to. That makes three kinds of dart, and they are not
equally true:

  corrected      a correction stands (`picked` alternative / candidate / typed): somebody
                 looked at the board and said what the dart was. TRUTH = the `corrected`
                 sector. The only darts whose truth a person asserted.
  uncorrected,   nobody changed it while somebody was correcting others. TRUTH = what was
  in the span    published, and that is PROBABLY right, NOT CERTAINLY: the maintainer says
                 the marker fixes some misreads and lets some stand. A rate over these is
                 an upper bound on agreement with the board, not a measured accuracy.
  outside        before the first standing correction of the day or after the last. Nobody
  the span       is known to have been checking. TRUTH UNKNOWN: counted, never graded.

THE SPAN is the day's darts from the first standing sector correction to the last, in the
truth line's own order (docs/rig.md, turnaus#1874: 2026-10-15 is darts 2519-3004). Kept
apart inside it and in no rate, as i1789 keeps them: `turn` (a Casual total was corrected,
naming no dart), `removed` (the dart was never there: truth is NO dart, and a read of any
sector is the same phantom) and `inferred` (turnaus#1537: no detection was made).

A REPLAY FILE holds one line per dart the replay read, in any order, other lines ignored:

    I1991 REPLAY ref=<reference> sector=<S20|D5|T19|25|Bull|None|-> path=<geometry|vote|refused>
                 cameras=<1,2,3|-> reason=<slug|->

`sector` is in the truth line's grammar (`None` a miss; `-` only with path=refused, where
`reason` says why no dart was called, one token). A reference named twice is a LOUD failure
(exit 3), as is a path that is none of the three: a grader that guesses is not one.

WHAT IT PRINTS.

    I1991 TRUTH ...                the three kinds above, said on every run
    I1991 SPAN first= last= darts= corrected= uncorrected= turn= removed= inferred= outside=
    I1991 GRADE who=<board|replay> graded= exact= rate= corrected_exact=a/b uncorrected_exact=c/d
          three_camera=n/m vote=n/m refused= missing=
        `graded` is the span's corrected + uncorrected darts. `exact`: the read IS the truth.
        `corrected_exact` is the number that can only go up by reading a dart better:
        the board's own is 0 by construction (a corrected dart is one it got wrong).
        `three_camera` and `vote` are shares of the graded darts that were read at all.
    I1991 REFUSED reason=<slug> n=    the replay's refusals inside the span, by reason
    I1991 REMOVED n= read_anyway=     darts that were never there, and how many the replay still reads
    I1991 DIFF ref= dart=<n> where=<span|outside> truth=<sector|?> board= replay= path= kind=<...>
        EVERY dart the replay reads differently from what the board published, anywhere in
        the day: fixed (now the truth), broke (was the truth -- a refusal of a dart that
        was right is one), moved (neither), phantom-unread (a removed dart the replay no
        longer calls), or unknown (outside the span, or a turn / inferred dart).
        `I1991 SAME n= of=` counts the rest.
    I1991 MISSING n= first=<ref>    span darts the replay file does not name (no kept window)

Stdlib only; runs in $OD_IMAGE's python. Exit 0 when it ran and every --expect held, 1
when an expectation did not (each named), 3 on a format refusal.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1789_truth_census as truth_line  # noqa: E402

SECTOR_PICKS = ("alternative", "candidate", "typed")
REPLAY_PATHS = ("geometry", "vote", "refused")

TRUTH_NOTE = (
    "I1991 TRUTH corrected darts: truth is the correction (a person said so). "
    "Uncorrected darts inside the span: truth is what was published -- PROBABLY right, NOT "
    "CERTAINLY (the marker lets some misreads stand); a rate over them is agreement with the "
    "board, not accuracy. Outside the span: truth unknown, never graded."
)


class FormatError(Exception):
    pass


def kind_of(row):
    """corrected / uncorrected / turn / removed / inferred -- by the truth line's own fields."""
    if truth_line.inferred(row):
        return "inferred"
    if row["picked"] in SECTOR_PICKS:
        return "corrected"
    if row["picked"] in ("turn", "removed"):
        return row["picked"]
    return "uncorrected"


def span_of(rows):
    """(first, last) as 0-based row indexes of the first and last standing sector correction, or None."""
    marked = [i for i, r in enumerate(rows) if kind_of(r) == "corrected"]
    return (marked[0], marked[-1]) if marked else None


def truth_of(row):
    """The sector the dart is taken to be, for a corrected or uncorrected dart."""
    return row["corrected"] if kind_of(row) == "corrected" else row["published"]


def board_reads(rows):
    """What the board itself published, in the shape of a replay: {ref: (sector, path, cameras, reason)}."""
    out = {}
    for r in rows:
        cams = r.get("cameras", "-")
        out[r["reference"]] = (r["published"], truth_line.path_of(r), cams, "-")
    return out


def read_replay(text):
    out = {}
    for n, line in enumerate(text.splitlines(), start=1):
        at = line.find("I1991 REPLAY ")
        if at < 0:
            continue
        fields = dict(f.split("=", 1) for f in line[at + len("I1991 REPLAY "):].split() if "=" in f)
        for need in ("ref", "sector", "path"):
            if need not in fields:
                raise FormatError("replay line %d: no %s= on an I1991 REPLAY line" % (n, need))
        if fields["path"] not in REPLAY_PATHS:
            raise FormatError("replay line %d: path=%s is none of %s" % (n, fields["path"], " ".join(REPLAY_PATHS)))
        if (fields["sector"] == "-") != (fields["path"] == "refused"):
            raise FormatError("replay line %d: sector=%s with path=%s -- a refused window reads no "
                              "sector and a read one is not refused" % (n, fields["sector"], fields["path"]))
        if fields["ref"] in out:
            raise FormatError("replay line %d: %s is read twice; which one is graded?" % (n, fields["ref"]))
        out[fields["ref"]] = (fields["sector"], fields["path"], fields.get("cameras", "-"), fields.get("reason", "-"))
    return out


def cameras_in(cams):
    return 0 if cams in ("-", "") else len([c for c in cams.split(",") if c])


def grade(rows, reads, who, out):
    """Print the grade; return the figures an --expect line can hold."""
    figures = {}
    span = span_of(rows)
    print(TRUTH_NOTE, file=out)
    if span is None:
        print("I1991 SPAN none: no sector correction stands on this line, so nobody is known to have "
              "been checking and NOTHING is graded (darts=%d)" % len(rows), file=out)
        figures.update(graded=0, exact=0)
        return figures
    first, last = span
    inside = rows[first:last + 1]
    tally = {k: sum(1 for r in inside if kind_of(r) == k) for k in ("corrected", "uncorrected", "turn", "removed", "inferred")}
    print("I1991 SPAN first=%d last=%d darts=%d corrected=%d uncorrected=%d turn=%d removed=%d inferred=%d outside=%d"
          % (first + 1, last + 1, len(inside), tally["corrected"], tally["uncorrected"], tally["turn"],
             tally["removed"], tally["inferred"], len(rows) - len(inside)), file=out)

    graded = [r for r in inside if kind_of(r) in ("corrected", "uncorrected")]
    exact = {"corrected": 0, "uncorrected": 0}
    read_n = three = vote = 0
    refused = {}
    missing = []
    for r in graded:
        got = reads.get(r["reference"])
        if got is None:
            missing.append(r["reference"])
            continue
        sector, path, cams, reason = got
        if path == "refused":
            refused[reason] = refused.get(reason, 0) + 1
            continue
        read_n += 1
        three += cameras_in(cams) >= 3
        vote += path == "vote"
        exact[kind_of(r)] += sector == truth_of(r)
    total_exact = exact["corrected"] + exact["uncorrected"]
    rate = (100.0 * total_exact / len(graded)) if graded else 0.0
    print("I1991 GRADE who=%s graded=%d exact=%d rate=%.2f%% corrected_exact=%d/%d uncorrected_exact=%d/%d "
          "three_camera=%d/%d vote=%d/%d refused=%d missing=%d"
          % (who, len(graded), total_exact, rate, exact["corrected"], tally["corrected"], exact["uncorrected"],
             tally["uncorrected"], three, read_n, vote, read_n, sum(refused.values()), len(missing)), file=out)
    for reason in sorted(refused, key=lambda k: (-refused[k], k)):
        print("I1991 REFUSED reason=%s n=%d" % (reason, refused[reason]), file=out)
    removed = [r for r in inside if kind_of(r) == "removed"]
    read_anyway = sum(1 for r in removed if reads.get(r["reference"], ("-", "refused"))[1] != "refused")
    print("I1991 REMOVED n=%d read_anyway=%d (never there: truth is no dart)" % (len(removed), read_anyway), file=out)
    if missing:
        print("I1991 MISSING n=%d first=%s (span darts the replay does not name: no kept window, so not graded "
              "and counted against the rate)" % (len(missing), missing[0]), file=out)

    diffs = same = 0
    kinds = {}
    if who != "board":
        for i, r in enumerate(rows):
            got = reads.get(r["reference"])
            if got is None:
                continue
            sector = got[0]
            if sector == r["published"]:
                same += 1
                continue
            diffs += 1
            in_span = first <= i <= last and kind_of(r) in ("corrected", "uncorrected")
            if first <= i <= last and kind_of(r) == "removed" and got[1] == "refused":
                kind, truth = "phantom-unread", "no-dart"
            elif not in_span:
                kind, truth = "unknown", "?"
            else:
                truth = truth_of(r)
                kind = "fixed" if sector == truth else "broke" if r["published"] == truth else "moved"
            kinds[kind] = kinds.get(kind, 0) + 1
            print("I1991 DIFF ref=%s dart=%d where=%s truth=%s board=%s replay=%s path=%s kind=%s"
                  % (r["reference"], i + 1, "span" if first <= i <= last else "outside", truth, r["published"],
                     sector, got[1] if got[1] != "refused" else "refused:" + got[3], kind), file=out)
        print("I1991 SAME n=%d of=%d diffs=%d %s" % (same, same + diffs, diffs,
              " ".join("%s=%d" % (k, kinds[k]) for k in sorted(kinds))), file=out)
    figures.update(first=first + 1, last=last + 1, graded=len(graded), exact=total_exact,
                   corrected_exact=exact["corrected"], uncorrected_exact=exact["uncorrected"],
                   three_camera=three, vote=vote, refused=sum(refused.values()), missing=len(missing),
                   diffs=diffs, same=same, fixed=kinds.get("fixed", 0), broke=kinds.get("broke", 0),
                   moved=kinds.get("moved", 0), phantom_unread=kinds.get("phantom-unread", 0))
    return figures


def check(figures, expectations, out):
    bad = 0
    for e in expectations:
        key, _, want = e.partition("=")
        got = figures.get(key)
        if got is None or str(got) != want:
            bad += 1
            print("I1991 MISMATCH %s expected %s got %s" % (key, want, got), file=out)
    if expectations:
        print("I1991 EXPECT %d held, %d did not" % (len(expectations) - bad, bad), file=out)
    return bad


# ---- self-test ----------------------------------------------------------------------------
def self_test(out):
    cols = ("reference published alternative corrected picked path flagged degraded candidates cameras "
            "crossing_deg sigma_mm margin_mm wire_kind agreeing lone_wire_mm ring_wire_mm radius angle bounced "
            "outcome corrections posted_at inferred")

    def row(ref, published, corrected="-", picked="-", path="geometry", cameras="1,2,3", inferred="0"):
        f = dict.fromkeys(cols.split(), "-")
        f.update(reference=ref, published=published, corrected=corrected, picked=picked, path=path, flagged="0",
                 degraded="0", cameras=cameras, bounced="0", outcome="COUNTED",
                 corrections="1" if picked != "-" else "0", inferred=inferred)
        return " ".join(f[c] for c in cols.split())

    text = "\n".join([
        "# turnaus truth line v1 board=device/9 day=2026-01-01 zone=Europe/Helsinki",
        "# " + cols,
        row("A0", "S1"),                                             # outside (before)
        row("A1", "S20", "S5", "typed", cameras="1,2"),              # corrected: truth S5
        row("A2", "T19"),                                            # uncorrected: truth T19
        row("A3", "D2", picked="turn", path="vote", cameras="1"),    # turn: apart
        row("A4", "S7", picked="removed", path="vote", cameras="2"), # removed: apart
        row("A5", "None", path="inferred", cameras="-", inferred="1"),
        row("A6", "S3", path="vote", cameras="1"),                   # uncorrected
        row("A7", "D16", "S16", "alternative"),                      # corrected: truth S16
        row("A8", "S9"),                                             # outside (after)
    ]) + "\n"
    _, _, rows, _ = truth_line.read_truth(text)
    fails = []

    def expect(what, got, want):
        ok = got == want
        print("%s %s (got %r, want %r)" % ("PASS" if ok else "FAIL", what, got, want), file=out)
        if not ok:
            fails.append(what)

    sink = open(os.devnull, "w")
    expect("the span is the first to the last sector correction, 1-based", tuple(i + 1 for i in span_of(rows)), (2, 8))
    b = grade(rows, board_reads(rows), "board", sink)
    expect("board: 4 darts graded (2 corrected + 2 uncorrected); turn, removed and inferred are in no rate", b["graded"], 4)
    expect("board: a corrected dart is graded against its CORRECTION, so the board has none of them right", b["corrected_exact"], 0)
    expect("board: an uncorrected dart in the span is graded against what was published", b["uncorrected_exact"], 2)
    expect("board: three-camera reads among the graded", b["three_camera"], 2)
    expect("board: vote-path reads among the graded", b["vote"], 1)
    replay = read_replay("\n".join([
        "noise before",
        "[12:00:00.000][INFO][X] - I1991 REPLAY ref=A0 sector=S5 path=geometry cameras=1,2,3 reason=-",   # outside: unknown
        "I1991 REPLAY ref=A1 sector=S5 path=geometry cameras=1,2,3 reason=-",    # fixed
        "I1991 REPLAY ref=A2 sector=S19 path=vote cameras=1 reason=-",           # broke
        "I1991 REPLAY ref=A4 sector=- path=refused cameras=- reason=no-fresh-figure",
        "I1991 REPLAY ref=A6 sector=- path=refused cameras=- reason=one-usable-line",
        "I1991 REPLAY ref=A7 sector=T16 path=geometry cameras=1,3 reason=-",     # moved
    ]))
    r = grade(rows, replay, "replay", sink)
    expect("replay: exact is the one dart it now reads as its correction", (r["exact"], r["corrected_exact"]), (1, 1))
    expect("replay: fixed / broke / moved are told apart; refusing a dart that was right is a break, refusing "
           "one that was never there is phantom-unread, and the dart outside the span is none of them",
           (r["fixed"], r["broke"], r["moved"], r["phantom_unread"], r["diffs"]), (1, 2, 1, 1, 6))
    expect("replay: a refusal inside the span is counted and is not a read", (r["refused"], r["three_camera"], r["vote"]), (1, 1, 1))
    expect("replay: no span dart is missing (A1 A2 A6 A7 are all named)", r["missing"], 0)
    r2 = grade(rows, {k: v for k, v in replay.items() if k != "A7"}, "replay", sink)
    expect("replay: a span dart the file does not name is MISSING, not silently skipped", r2["missing"], 1)
    for bad, why in (("I1991 REPLAY ref=A1 sector=S5 path=geometry\nI1991 REPLAY ref=A1 sector=S1 path=vote", "read twice"),
                     ("I1991 REPLAY ref=A1 sector=S5 path=guess", "none of"),
                     ("I1991 REPLAY ref=A1 sector=- path=vote", "refused")):
        try:
            read_replay(bad)
            expect("a replay file is refused loudly: " + why, "accepted", "refused")
        except FormatError as e:
            expect("a replay file is refused loudly: " + why, why in str(e), True)
    none = [dict(x, picked="-", corrected="-") for x in rows]
    expect("no standing correction: nothing is graded", grade(none, board_reads(none), "board", sink)["graded"], 0)
    print("SELF-TEST %d failed" % len(fails), file=out)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--truth")
    ap.add_argument("--board", action="store_true")
    ap.add_argument("--replay")
    ap.add_argument("--expect", nargs="*", default=[])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--emit-board-replay", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test(sys.stdout)
    if a.emit_board_replay and a.truth:
        # The identity replay: what a replay that reproduces the board exactly would print.
        _, _, rows, _ = truth_line.read_truth(open(a.truth, encoding="utf-8").read())
        for ref, (sector, path, cams, _) in board_reads(rows).items():
            print("I1991 REPLAY ref=%s sector=%s path=%s cameras=%s reason=-"
                  % (ref, sector, path if path in ("geometry", "vote") else "vote", cams))
        return 0
    if not a.truth or (a.board == bool(a.replay)):
        ap.error("--truth and exactly one of --board / --replay")
    try:
        _, _, rows, _ = truth_line.read_truth(open(a.truth, encoding="utf-8").read())
        if a.board:
            reads, who = board_reads(rows), "board"
        else:
            reads, who = read_replay(open(a.replay, encoding="utf-8", errors="replace").read()), "replay"
    except (FormatError, truth_line.FormatError) as e:
        print("I1991 FORMAT-REFUSED %s" % e)
        return 3
    figures = grade(rows, reads, who, sys.stdout)
    return 1 if check(figures, a.expect, sys.stdout) else 0


if __name__ == "__main__":
    sys.exit(main())

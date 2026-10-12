"""turnaus#1789: a session's truth line and uploaded log, read back on the detector side.

    python3 i1789_truth_census.py --truth <truth-line.txt> [--log <log.txt>] [--expect]
    python3 i1789_truth_census.py --self-test

WHAT IT READS. Two files Turnaus serves for one board-day (turnaus#1786, #1790):

  * the TRUTH LINE (`GET /api/v1/operator/boards/{kind}/{board}/{day}/truth-line`), written
    by Turnaus's `App\\Autoscoring\\BoardSession::truthLine()`:

        # turnaus truth line v1 board=<kind>/<id> day=<day> zone=Europe/Helsinki
        # <the columns, space-separated, in order>
        <one line per dart, fields separated by one space, `-` for anything not said>

    The columns are read from the export's OWN `# <columns>` line and never by position.
    `degraded` was added after `flagged` while the header still said v1, so a parser that
    counted fields would have read `degraded` as `candidates` and every column after it one
    to the left -- and miscounted without a word. So: a column this script needs that the
    columns line does not name, a data line whose field count is not the columns line's, a
    `picked` value Turnaus's `App\\Autoscoring\\Pick` does not have, or a `path` that is not
    geometry / vote / `-` is a LOUD failure (exit 3) naming the line, never a count. The
    columns this script reads are REQUIRED below; Turnaus main writes, in this order:

        reference published alternative corrected picked path flagged degraded candidates
        cameras crossing_deg sigma_mm margin_mm wire_kind agreeing lone_wire_mm ring_wire_mm
        radius angle bounced outcome corrections posted_at

  * the day's LOG (`.../log`): the board's own `--log-file` text, each file it posted joined
    in the order begun. Lines are `[HH:MM:SS.mmm][LEVEL][module] - message` (logging.hpp).
    The client prints every accepted push as `TURNAUS: <OUTCOME> <body>` and the body holds
    the dart's `reference`, which is the join.

WHAT IT PRINTS.

    I1789 COUNT path=<geometry|vote|unknown|total> posted= corrected= alternative=
          candidate= typed= unflagged= removed= turn= undone= flagged= false_flag=
          false_flag_rate=
        The four counts (`alternative candidate typed unflagged`) are Turnaus's
        `BoardSession::countOf`, recounted from the line: a dart is counted by its LAST
        correction, which is what `picked` carries; `published` (a fix undone), `removed`
        (the dart was never there) and `turn` (a Casual total, naming no dart) are in none
        of the four and are printed beside them; an unflagged dart corrected to anything is
        `unflagged` whatever it picked. `corrected` is the desk's figure: darts with any
        correction at all (`corrections` > 0). `false_flag` is a flagged dart whose
        published score stood: `picked` is `-` or `published`. The server's counts are the
        authority; this is the detector side's independent recount, to be compared.
    I1789 DART ref= published= corrected= picked= path= flagged= class=<class> why=<...>
        one per dart with a correction standing (picked is not `-` / `published`; a dart
        of a corrected Casual turn is printed too, as class turn-total), then
        that dart's log window: its SCORE line and the UNCERTAINTY / Geometric score /
        Consensus / No consensus / LONE-WIRE / RING-WIRE / BOARD / I1707 RIM CARRIED lines
        between the previous SCORE line and its push, and its push line; or
    I1789 ABSENT ref= where=<...> posted=<clock> log=<first>..<last> mentioned=<n> ...
        no push line names the reference. Never silently dropped, and never a bare "not in
        the log" (#1817): that sentence was false for 7 of board 20's 14 absences on
        2026-10-11, whose references are in the log on a `TURNAUS: frames for <ref> are
        gone` line, and it sent an issue looking for a lookup that misses push lines. There
        was none: on boards 17, 20 and 21 every truth reference a push line names is found
        and nothing else is. So an absence says what the log DOES hold. `mentioned` counts
        the lines naming the reference anyway and the first is quoted; `where` places the
        dart's `posted_at`, in the header's zone and counted from the header's day, against
        the log's own clock, split into its files at the session banner:
          before-the-log / after-the-log  the board logged nothing then: an upload that does
                                          not cover the dart, not a dart the log lost
          between-files                   in the gap between two joined files, same reading
          within-a-file                   the board was logging and no push names the dart:
                                          the one case that is a loss, or a lookup miss
          posted-unread                   no posted_at, zone or day to place it with
        posted_at is the server's clock and the log is the board's, so a dart a second or
        two from a file's edge is the reader's call; both clocks are printed for that.
    I1789 PICK-DISAGREES ref=  `picked` is not what Turnaus's `DartCorrections::picked`
        would decide from `published`, `alternative`, `candidates` and `corrected`.
    I1789 CLASS <class>=<n> ...  the fault-class tally.

THE FAULT CLASSES, decided from the account (the truth line's own fields) in this order,
the first that holds winning; the classes that need the log are the takeout's and the
re-read's, because the truth line carries no END and no pixel position:

  turn-total        #1786  `picked` turn: a Casual turn's TOTAL was corrected, which names
                           no dart (Turnaus's Pick::Turn: "it says nothing about which of the
                           board's darts was wrong"). Not a fault class: decided first so no
                           dart nobody corrected lands in one. The `why` says what the account
                           alone would have called it (#1819: 56 of the first two real
                           sessions' 93 DART lines were these, 34 of them inside fault classes)
  phantom-takeout   #1781  the dart's SCORE line is within PHANTOM_S of a `SCORE: END` in
                           the log (live 2026-10-10: D11 0.74 s before, S2 0.98 s after)
  reread-after-takeout #1820 a removed dart whose SCORE Position is within REREAD_PX of a
                           dart of the visit the END before it closed, with no END between:
                           a dart still in the board, published again (live casual/20
                           00:57:23 and :25, 0.0 and 9.2 px, 2.41 and 4.49 s after the END)
  phantom-unplaced    -    any other removed dart: it was never there, and nothing here says
                           why -- usually because the log does not cover it (#1818). A
                           removed dart is never put in a wire class: it was not misread
  miss-for-a-dart   #1821  published a miss and corrected to a sector (live casual/20 S13 by
                           the rim-only carry, casual/17 D20, 2026-10-10 16:24:24 S3)
  rim-one-tip       #1707  a lone vote reading (agreeing 1) that published a double, or was
                           corrected to a miss (`None`): one camera's tip on a rim dart,
                           at the rim or the barrel over the board (live 2026-10-10: D16,
                           D3, D18, D18 at 0.96-0.999, and a miss read S20 at 0.562)
  corner            #1782  a geometric solve within its own sigma of a ring wire AND a wedge
                           wire: the flag offers the nearest wire's alternative only (live
                           16:38:53: T3 for S19, 0.6 mm and 1.8 mm across 5.3 mm)
  two-line-wire     #1766  a two-camera geometric solve within its own sigma of the wire
                           it names (live 16:49:15: S1 for S20, 2.1 mm across 5.1 mm)
  three-line-wire     -    the same with three or more cameras: the sigma covered the wire,
                           the flag fired and offered the answer (live casual/20 01:07:36:
                           S4 for S18, 1.1 mm across 5.0 mm). The flag's own case, no fault
  lone-beyond-sigma #1822  a lone vote reading corrected across a wire it was more than
                           SIGMA_VOTE_MM clear of: no wire check could have flagged it for
                           that wire (live casual/17: S3 for S19 9.4 mm off, S3 for T3 21.3)
  ring-wire         #1773  a vote reading within SIGMA_VOTE_MM of a ring wire, lone or
                           consensus (live: T19 at 3.1 mm, OUTER at 2.3 and 0.5 mm)
  lone-wedge-wire   #1628  a lone vote reading within SIGMA_VOTE_MM of a wedge wire (the
                           nearer of the two margins decides between this and ring-wire)
  unflagged-geometric #1556 a geometric solve that was not flagged and was corrected
  unclassified             the honest default

The vote classes read `ring_wire_mm` and `lone_wire_mm` where the board posted them and
otherwise the same margins from `radius` and `angle` on the 170 mm model, marked `(model)`
in the `why` (#1819: casual/17's build posted neither for any of its darts, so before this
#1773 and #1628 could not hold there). Which wire a correction crossed is read from the
two sectors: the number changed is a wedge wire, the ring letter a ring wire.

Stdlib only; it runs in $OD_IMAGE's python 3.11 (the box that runs the suite has no host
python3). Exit 0 when it ran (and, with --expect, every expectation held), 1 when an
expectation failed, 3 when the export is not the format this reads.
"""

import argparse
import math
import re
import sys
from datetime import datetime, timezone

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover -- 3.9+ has it; a missing tz database is caught below
    ZoneInfo = None

# ---- the format, from Turnaus main ------------------------------------------------------
HEADER_PREFIX = "# turnaus truth line v"
REQUIRED = (
    "reference", "published", "alternative", "corrected", "picked", "path", "flagged",
    "candidates", "cameras", "sigma_mm", "margin_mm", "wire_kind", "agreeing",
    "lone_wire_mm", "ring_wire_mm", "radius", "angle", "corrections",
)
# App\Autoscoring\Pick, plus `-` for a dart nobody corrected.
PICKS = ("alternative", "published", "candidate", "typed", "removed", "turn", "-")
# BoardSession::PATHS; `-` is a board that did not say, counted as `unknown` as the desk does.
PATHS = ("geometry", "vote", "unknown")
COUNTS = ("alternative", "candidate", "typed", "unflagged")
MISS = "None"  # #821's Sector grammar for a miss; the detector's MISS (turnaus_client.cpp)

# ---- the figures the classes are decided on, each from where it is recorded ------------
PHANTOM_S = 2.0          # turnaus#1789's own figure; #1781's phantoms were -0.74 and +0.98 s
SIGMA_VOTE_MM = 5.0      # #1628's lone-reading sigma, which #1773 reused for ring wires
SCORING_RADIUS_MM = 170.0  # radius 1.0 is the double's outer wire (docs/rig.md, #1773)
RING_WIRES_MM = (6.35, 15.9, 99.0, 107.0, 162.0, 170.0)  # the spec #1773 static_asserts
WEDGE_WIRE_FIRST_DEG, WEDGE_WIRE_STEP_DEG = 9.0, 18.0     # wedge wires at 9 + 18k degrees

REREAD_PX = 12.0         # #1819: casual/20's two re-reads sat 0.0 and 9.2 px from the darts
                         # they repeated; a dart's own Position is the scorer's pixel
SECTOR = re.compile(r"^([SsDT])(\d{1,2})$")  # a sector on a numbered wedge (#821's grammar)

CLASSES = (
    ("turn-total", "#1786"),
    ("phantom-takeout", "#1781"),
    ("reread-after-takeout", "#1820"),
    ("phantom-unplaced", "-"),
    ("miss-for-a-dart", "#1821"),
    ("rim-one-tip", "#1707"),
    ("corner", "#1782"),
    ("two-line-wire", "#1766"),
    ("three-line-wire", "-"),
    ("lone-beyond-sigma", "#1822"),
    ("ring-wire", "#1773"),
    ("lone-wedge-wire", "#1628"),
    ("unflagged-geometric", "#1556"),
    ("unclassified", "-"),
)
ISSUE = dict(CLASSES)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LOG_LINE = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\.(\d{3})\]\[[^\]]*\]\[[^\]]*\] - (.*)$")
SCORE_LINE = re.compile(r"^SCORE: (\S+)")
POSITION = re.compile(r"\| Position: \((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)")
SHOWN = ("SCORE: ", "UNCERTAINTY", "Geometric score", "Consensus score", "No consensus",
         "LONE-WIRE", "RING-WIRE", "BOARD: ", "I1707 RIM CARRIED")
# The detector's words for three sectors #821's grammar spells otherwise (postableSector).
POSTABLE = {"MISS": "None", "OUTER": "25", "BULL": "Bull"}


class FormatError(Exception):
    pass


# ---- the truth line ---------------------------------------------------------------------
def read_truth(text):
    """(meta, columns, rows, expectations). Rows are dicts keyed by column name."""
    lines = text.splitlines()
    head = next((i for i, l in enumerate(lines) if l.startswith(HEADER_PREFIX)), None)
    if head is None:
        raise FormatError("no '%s...' header line: this is not a Turnaus truth line" % HEADER_PREFIX)
    expectations = [l[len("# expect "):].split() for l in lines[:head] if l.startswith("# expect ")]
    meta = dict(kv.split("=", 1) for kv in lines[head].split()[5:] if "=" in kv)
    meta["version"] = lines[head].split()[4]
    if head + 1 >= len(lines) or not lines[head + 1].startswith("# "):
        raise FormatError("line %d: the header is not followed by a '# <columns>' line" % (head + 2))
    columns = lines[head + 1][2:].split()
    if len(set(columns)) != len(columns):
        raise FormatError("line %d: a column is named twice: %s" % (head + 2, " ".join(columns)))
    missing = [c for c in REQUIRED if c not in columns]
    if missing:
        raise FormatError("line %d: the columns line does not name %s; this script reads %s"
                          % (head + 2, ", ".join(missing), " ".join(REQUIRED)))
    rows = []
    for n, line in enumerate(lines[head + 2:], start=head + 3):
        if not line.strip():
            continue
        if line.startswith("#"):
            raise FormatError("line %d: a comment after the columns line" % n)
        fields = line.split(" ")
        if len(fields) != len(columns):
            raise FormatError("line %d: %d fields where the columns line names %d -- the "
                              "export and its own header disagree, and nothing is counted"
                              % (n, len(fields), len(columns)))
        row = dict(zip(columns, fields))
        row["_line"] = n
        if row["picked"] not in PICKS:
            raise FormatError("line %d: picked=%s is not one of App\\Autoscoring\\Pick's values (%s)"
                              % (n, row["picked"], " ".join(PICKS)))
        if row["path"] not in ("geometry", "vote", "-"):
            raise FormatError("line %d: path=%s is neither geometry, vote nor -" % (n, row["path"]))
        if row["flagged"] not in ("0", "1"):
            raise FormatError("line %d: flagged=%s is not 0 or 1" % (n, row["flagged"]))
        if not re.fullmatch(r"\d+", row["corrections"]):
            raise FormatError("line %d: corrections=%s is not a count" % (n, row["corrections"]))
        rows.append(row)
    return meta, columns, rows, expectations


def num(row, key):
    v = row.get(key, "-")
    if v == "-":
        return None
    try:
        return float(v)
    except ValueError:
        raise FormatError("line %d: %s=%s is not a number" % (row["_line"], key, v))


def path_of(row):
    return "unknown" if row["path"] == "-" else row["path"]


def standing(row):
    """A correction stands: the dart's last correction is not 'none' or a fix undone."""
    return row["picked"] not in ("-", "published")


def count_of(row):
    """Turnaus's BoardSession::countOf, from the truth line's own fields."""
    if row["picked"] in ("-", "published", "removed", "turn"):
        return None
    if row["flagged"] != "1":
        return "unflagged"
    return row["picked"]


def counts(rows):
    keys = ("posted", "corrected") + COUNTS + ("removed", "turn", "undone", "flagged", "false_flag")
    out = {p: dict.fromkeys(keys, 0) for p in PATHS + ("total",)}
    for row in rows:
        for p in (path_of(row), "total"):
            c = out[p]
            c["posted"] += 1
            c["corrected"] += int(row["corrections"]) > 0
            k = count_of(row)
            if k is not None:
                c[k] += 1
            c["removed"] += row["picked"] == "removed"
            c["turn"] += row["picked"] == "turn"
            c["undone"] += row["picked"] == "published"
            if row["flagged"] == "1":
                c["flagged"] += 1
                c["false_flag"] += row["picked"] in ("-", "published")
    return out


def pick_disagrees(row):
    """What Turnaus's DartCorrections::picked would decide, where the line says enough."""
    picked, corrected = row["picked"], row["corrected"]
    if picked in ("-", "turn"):
        return None
    if picked == "removed":
        return None if corrected == "-" else "removed, but corrected names %s" % corrected
    if corrected == "-":
        return "%s, but corrected names no sector" % picked
    chosen = corrected.upper()
    candidates = [] if row["candidates"] == "-" else row["candidates"].split(",")
    if chosen == row["published"].upper():
        decided = "published"
    elif row["alternative"] != "-" and chosen == row["alternative"].upper():
        decided = "alternative"
    elif chosen in (c.upper() for c in candidates):
        decided = "candidate"
    else:
        decided = "typed"
    return None if decided == picked else "picked=%s where the sectors say %s" % (picked, decided)


# ---- the board's geometry, for the one wire the account does not measure ---------------
def nearest_ring_wire_mm(radius):
    r = radius * SCORING_RADIUS_MM
    return min(abs(r - w) for w in RING_WIRES_MM)


def nearest_wedge_wire_mm(radius, angle):
    r = radius * SCORING_RADIUS_MM
    off = (angle - WEDGE_WIRE_FIRST_DEG) % WEDGE_WIRE_STEP_DEG
    deg = min(off, WEDGE_WIRE_STEP_DEG - off)
    return r * math.sin(math.radians(deg))


def vote_margins(row):
    """(ring_mm, wedge_mm, source) for a vote reading: the account's own `ring_wire_mm` and
    `lone_wire_mm` where it posted them, else the same figure from `radius`/`angle` on the
    170 mm model (#1819: casual/17's build posted neither, for any of its 526 darts, so
    #1773's and #1628's rules could never hold there). `lone_wire_mm` is a lone reading's
    measure; the model's wedge figure is used for a lone reading only, as the account's is,
    and never inside the 25's wire, where there is no wedge wire to be near."""
    ring, wedge = num(row, "ring_wire_mm"), num(row, "lone_wire_mm")
    radius, angle = num(row, "radius"), num(row, "angle")
    source = "account"
    if ring is None and radius is not None:
        ring, source = nearest_ring_wire_mm(radius), "model"
    if (wedge is None and radius is not None and angle is not None
            and radius * SCORING_RADIUS_MM > RING_WIRES_MM[1]):  # no wedge wire inside the 25
        wedge, source = nearest_wedge_wire_mm(radius, angle), "model"
    return ring, wedge, source


def crossed_wires(published, corrected):
    """Which wires a correction crossed, between two sectors on numbered wedges: `wedge`
    when the number changed, `ring` when the ring did; None when either is not one (a miss,
    25, Bull). Case-blind, as Turnaus's DartCorrections::picked is."""
    a, b = SECTOR.match(published), SECTOR.match(corrected)
    if not a or not b:
        return None
    crossed = []
    if a.group(2) != b.group(2):
        crossed.append("wedge")
    if a.group(1).upper() != b.group(1).upper():
        crossed.append("ring")
    return crossed or None


def classify(row, dart_time, ends, reread=None):
    """(class, why) for a corrected dart. `dart_time` is its SCORE line's second of the day,
    or None when it is not in the log; `ends` every END's; `reread` the earlier dart its
    SCORE Position repeats across the END before it, when one does (`reread_of`)."""
    if row["picked"] == "turn":
        k, why = classify(dict(row, picked="typed"), dart_time, ends, reread)
        return "turn-total", "a Casual turn's total was corrected, naming no dart; on its account alone it would be %s" % k
    if dart_time is not None and ends:
        nearest = min(ends, key=lambda e: abs(e - dart_time))
        if abs(nearest - dart_time) <= PHANTOM_S:
            return "phantom-takeout", "%+.3f s from an END" % (dart_time - nearest)
    if row["picked"] == "removed":
        if reread:
            return "reread-after-takeout", reread
        return "phantom-unplaced", ("removed, and no END within %.1f s of it" % PHANTOM_S if dart_time is not None
                                    else "removed, and the log does not hold its window")
    if row["published"] == MISS and row["corrected"] not in ("-", MISS):
        return "miss-for-a-dart", "published a miss, corrected to %s (agreeing %s)" % (row["corrected"], row["agreeing"])
    path = row["path"]
    agreeing = num(row, "agreeing")
    sigma, margin = num(row, "sigma_mm"), num(row, "margin_mm")
    radius, angle = num(row, "radius"), num(row, "angle")
    if path == "vote" and agreeing == 1:
        if row["published"].startswith("D"):
            return "rim-one-tip", "a lone reading published %s at radius %s" % (row["published"], row["radius"])
        if row["corrected"] == MISS:
            return "rim-one-tip", "a lone reading published %s at radius %s for a miss" % (row["published"], row["radius"])
    cameras = [] if row["cameras"] == "-" else row["cameras"].split(",")
    if path == "geometry" and sigma is not None and margin is not None and margin <= sigma:
        if radius is not None and angle is not None and radius * SCORING_RADIUS_MM > RING_WIRES_MM[1]:
            other = (nearest_wedge_wire_mm(radius, angle) if row["wire_kind"] == "ring"
                     else nearest_ring_wire_mm(radius) if row["wire_kind"] == "wedge" else None)
            if other is not None and other <= sigma:
                return "corner", "%s wire %.1f mm and the other %.1f mm, both inside %.1f mm" % (
                    row["wire_kind"], margin, other, sigma)
        if len(cameras) == 2:
            return "two-line-wire", "cameras %s, %s wire %.1f mm across a %.1f mm sigma" % (
                row["cameras"], row["wire_kind"], margin, sigma)
        if len(cameras) >= 3:
            return "three-line-wire", "cameras %s, %s wire %.1f mm across a %.1f mm sigma, flagged=%s" % (
                row["cameras"], row["wire_kind"], margin, sigma, row["flagged"])
    if path == "vote":
        ring, wedge, source = vote_margins(row)
        if agreeing != 1:
            wedge = None
        crossed = crossed_wires(row["published"], row["corrected"]) if agreeing == 1 else None
        known = [m for m in ((ring if w == "ring" else wedge) for w in crossed or ()) if m is not None]
        if known and min(known) > SIGMA_VOTE_MM:
            return "lone-beyond-sigma", "corrected across the %s wire, %.1f mm off it (%s), beyond the %.0f mm sigma" % (
                "/".join(crossed), min(known), source, SIGMA_VOTE_MM)
        near = [(m, k) for m, k in ((ring, "ring-wire"), (wedge, "lone-wedge-wire"))
                if m is not None and m <= SIGMA_VOTE_MM]
        if near:
            m, k = min(near)
            return k, "%s %.1f mm (%s) inside the %.0f mm sigma (agreeing %s)" % (
                "ring wire" if k == "ring-wire" else "wedge wire", m, source, SIGMA_VOTE_MM, row["agreeing"])
    if path == "geometry" and row["flagged"] == "0":
        return "unflagged-geometric", "a solve published unflagged and corrected"
    return "unclassified", "no class's figures hold"


# ---- the log ----------------------------------------------------------------------------
def read_log(text):
    """[(second_of_day or None, message)], seconds unwrapped across midnight."""
    out, last, wrap = [], None, 0.0
    for raw in text.splitlines():
        line = ANSI.sub("", raw)
        m = LOG_LINE.match(line)
        if not m:
            out.append((None, line))
            continue
        t = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000.0
        if last is not None and t + wrap < last - 12 * 3600:
            wrap += 24 * 3600
        last = t + wrap
        out.append((last, m.group(5)))
    return out


def ends_of(log):
    return [t for t, msg in log if t is not None and re.match(r"SCORE: END\b", msg)]


def lines_of(log, reference, published):
    """(push, score) indices for the dart whose push names `reference`: (None, None) when no
    push does, score None when no SCORE line before the push publishes `published`."""
    needle = '"reference":"%s"' % reference
    push = next((i for i, (_, msg) in enumerate(log) if msg.startswith("TURNAUS: ") and needle in msg), None)
    if push is None:
        return None, None
    for i in range(push - 1, -1, -1):
        m = SCORE_LINE.match(log[i][1])
        if m and POSTABLE.get(m.group(1), m.group(1)) == published:
            return push, i
    return push, None


def position_of(msg):
    m = POSITION.search(msg)
    return (float(m.group(1)), float(m.group(2))) if m and m.group(1) != "-1" else None


def reread_of(log, score):
    """The `why` of a re-read (#1819) for the SCORE line at `score`, or None: its Position
    is within REREAD_PX of a dart the visit before it published -- the darts between the
    END before it and the END (or file start) before that -- with no END between. That is
    a dart still in the board, published again after a takeout that left it there."""
    here = position_of(log[score][1])
    if here is None:
        return None
    end = None
    for i in range(score - 1, -1, -1):
        msg = log[i][1]
        if "Session Started" in msg:
            return None
        if re.match(r"SCORE: END\b", msg):
            end = i
            break
    if end is None:
        return None
    for i in range(end - 1, -1, -1):
        msg = log[i][1]
        if "Session Started" in msg or re.match(r"SCORE: END\b", msg):
            return None
        m = SCORE_LINE.match(msg)
        there = position_of(msg) if m else None
        if there is not None:
            d = math.hypot(here[0] - there[0], here[1] - there[1])
            if d <= REREAD_PX:
                return "%.1f px from the %s published at %s, before the END this dart follows by %.3f s" % (
                    d, m.group(1), clock(log[i][0]), log[score][0] - log[end][0])
    return None


def window_of(log, reference, published):
    """(dart_time, [lines], score) for the dart whose push names `reference`, or None."""
    push, score = lines_of(log, reference, published)
    if push is None:
        return None
    if score is None:
        return log[push][0], [log[push]], None
    start = 0
    for i in range(score - 1, -1, -1):
        if SCORE_LINE.match(log[i][1]) or "Session Started" in log[i][1]:
            start = i + 1
            break
    shown = [log[i] for i in range(start, push + 1)
             if i == push or log[i][1].startswith(SHOWN)]
    return log[score][0], shown, score


def files_of(log):
    """[(first, last)] second of each file in the joined log, split at the session banner
    the way window_of reads a file's start."""
    spans, cur = [], []
    for t, msg in log:
        if "Session Started" in msg:
            if cur:
                spans.append((cur[0], cur[-1]))
            cur = []
        elif t is not None:
            cur.append(t)
    if cur:
        spans.append((cur[0], cur[-1]))
    return spans


def posted_second(row, meta):
    """The dart's posted_at as the log counts it -- local seconds from the header day's
    midnight in the header's zone -- or None when it cannot be placed."""
    posted, zone, day = row.get("posted_at", "-"), meta.get("zone"), meta.get("day")
    if posted == "-" or not zone or not day or ZoneInfo is None:
        return None
    try:
        tz = ZoneInfo(zone)
        utc = datetime.strptime(posted, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        local = utc.astimezone(tz).replace(tzinfo=None)
        return (local - datetime.strptime(day, "%Y-%m-%d")).total_seconds()
    except Exception:  # an unreadable posted_at, day or zone, or no tz database: unplaced
        return None


def where_in_log(second, spans):
    if second is None:
        return "posted-unread"
    if not spans:
        return "before-the-log"
    if second < spans[0][0]:
        return "before-the-log"
    if second > spans[-1][1]:
        return "after-the-log"
    if any(a <= second <= b for a, b in spans):
        return "within-a-file"
    return "between-files"


def absence_of(log, row, meta):
    """(where, line) for a dart no push line names: where its posted_at falls against the
    log's files, and what the log does say about the reference."""
    spans = files_of(log)
    second = posted_second(row, meta)
    mentions = [msg for _, msg in log if row["reference"] in msg]
    where = where_in_log(second, spans)
    line = "where=%s posted=%s log=%s mentioned=%d no push line names the reference" % (
        where, clock(second) if second is not None else "-",
        "%s..%s" % (clock(spans[0][0]), clock(spans[-1][1])) if spans else "-", len(mentions))
    if mentions:
        line += "; first mention: %s" % mentions[0][:160]
    return where, len(mentions), line


def clock(t):
    t %= 24 * 3600
    return "%02d:%02d:%06.3f" % (t // 3600, (t % 3600) // 60, t % 60)


# ---- the report -------------------------------------------------------------------------
def census(truth_text, log_text, out):
    meta, columns, rows, expectations = read_truth(truth_text)
    out("I1789 HEADER %s board=%s day=%s columns=%d dart_lines=%d"
        % (meta["version"], meta.get("board", "-"), meta.get("day", "-"), len(columns), len(rows)))
    out("I1789 COLUMNS %s" % " ".join(columns))
    tally = counts(rows)
    for p in PATHS + ("total",):
        c = tally[p]
        rate = "%.3f" % (c["false_flag"] / c["flagged"]) if c["flagged"] else "-"
        out("I1789 COUNT path=%s %s false_flag_rate=%s"
            % (p, " ".join("%s=%d" % kv for kv in c.items()), rate))
    log = read_log(log_text) if log_text is not None else None
    ends = ends_of(log) if log else []
    if log is not None:
        out("I1789 LOG lines=%d ends=%d files=%d" % (len(log), len(ends), len(files_of(log))))
    classes, absent = {}, {}
    for row in rows:
        disagrees = pick_disagrees(row)
        if disagrees:
            out("I1789 PICK-DISAGREES ref=%s line=%d %s" % (row["reference"], row["_line"], disagrees))
        if not standing(row):
            continue
        found = window_of(log, row["reference"], row["published"]) if log else None
        dart_time = found[0] if found else None
        reread = reread_of(log, found[2]) if found and found[2] is not None else None
        k, why = classify(row, dart_time, ends, reread)
        classes[row["reference"]] = k
        out("I1789 DART ref=%s published=%s corrected=%s picked=%s path=%s flagged=%s class=%s issue=%s why=%s"
            % (row["reference"], row["published"], row["corrected"], row["picked"], row["path"],
               row["flagged"], k, ISSUE[k], why.replace(" ", "_")))
        if found:
            for t, msg in found[1]:
                out("    %s %s" % (clock(t) if t is not None else "--:--:--.---", msg))
        else:
            if log is None:
                absent[row["reference"]] = {}
                out("I1789 ABSENT ref=%s no log was given" % row["reference"])
            else:
                where, mentioned, line = absence_of(log, row, meta)
                absent[row["reference"]] = {"where": where, "mentioned": str(mentioned)}
                out("I1789 ABSENT ref=%s %s" % (row["reference"], line))
    out("I1789 CLASS " + " ".join("%s=%d" % (k, sum(1 for v in classes.values() if v == k))
                                  for k, _ in CLASSES))
    return tally, classes, absent, expectations


def check(tally, classes, absent, expectations, out):
    """The fixture's header says what the census must print; every difference is named."""
    bad = 0
    for e in expectations:
        if e[0] == "count":
            for kv in e[2:]:
                key, want = kv.split("=")
                got = tally[e[1]][key]
                if got != int(want):
                    bad += 1
                    out("I1789 MISMATCH count %s.%s expected %s got %d" % (e[1], key, want, got))
        elif e[0] == "class":
            got = classes.get(e[1], "not-a-corrected-dart")
            if got != e[2]:
                bad += 1
                out("I1789 MISMATCH class %s expected %s got %s" % (e[1], e[2], got))
        elif e[0] == "absent":
            if e[1] not in absent:
                bad += 1
                out("I1789 MISMATCH absent %s expected absent from the log, was found" % e[1])
                continue
            for kv in e[2:]:  # `where=<...>` / `mentioned=<n>`, as the ABSENT line prints them
                key, want = kv.split("=", 1)
                got = absent[e[1]].get(key, "-")
                if got != want:
                    bad += 1
                    out("I1789 MISMATCH absent %s.%s expected %s got %s" % (e[1], key, want, got))
        else:
            bad += 1
            out("I1789 MISMATCH an expectation this script cannot read: %s" % " ".join(e))
    named_absent = {e[1] for e in expectations if e[0] == "absent"}
    for ref in absent:
        if ref not in named_absent:
            bad += 1
            out("I1789 MISMATCH absent %s is absent from the log and the header does not say so" % ref)
    out("I1789 EXPECT %s expectations=%d mismatches=%d" % ("HELD" if bad == 0 else "FAILED", len(expectations), bad))
    return bad


# ---- the classes on hand-built accounts (no session) ------------------------------------
def self_test(out):
    """One synthetic account per class plus the boundaries either side of each figure. These
    are NOT from a session: they hold the rules where the 2026-10-10 reference set has no
    instance (#1628, #1556 -- that session's every unflagged geometric solve was right)."""
    base = dict((c, "-") for c in REQUIRED)
    base.update(picked="typed", flagged="0", corrections="1", _line=0)

    def row(**kw):
        r = dict(base)
        r.update(kw)
        return r

    cases = [
        ("phantom inside 2 s", row(path="vote", published="S2", agreeing="1"), 10.0, [12.0], "phantom-takeout"),
        ("phantom just outside 2 s", row(path="vote", published="S2", agreeing="1"), 10.0, [12.01], "unclassified"),
        ("lone double", row(path="vote", published="D16", agreeing="1", radius="0.9615"), None, [], "rim-one-tip"),
        ("lone single, corrected to a miss", row(path="vote", published="S20", corrected="None", agreeing="1",
                                                  ring_wire_mm="3.46", radius="0.562"), None, [], "rim-one-tip"),
        ("consensus double is not one tip", row(path="vote", published="D16", agreeing="2", ring_wire_mm="9"),
         None, [], "unclassified"),
        ("corner", row(path="geometry", flagged="1", published="T3", cameras="2,3", sigma_mm="5.3", margin_mm="0.6",
                       wire_kind="ring", radius="0.6346", angle="188.03"), None, [], "corner"),
        ("corner's wedge 8 mm away is a two-line call", row(path="geometry", flagged="1", published="T3",
                                                            cameras="2,3", sigma_mm="5.3", margin_mm="0.6",
                                                            wire_kind="ring", radius="0.6346", angle="184.8"),
         None, [], "two-line-wire"),
        ("two-line inside its sigma", row(path="geometry", flagged="1", published="S1", cameras="2,3",
                                          sigma_mm="5.1", margin_mm="2.1", wire_kind="wedge"), None, [], "two-line-wire"),
        ("three-line inside its sigma", row(path="geometry", flagged="1", published="S1", cameras="1,2,3",
                                            sigma_mm="5.1", margin_mm="2.1", wire_kind="wedge"), None, [], "three-line-wire"),
        ("three-line just outside its sigma", row(path="geometry", flagged="1", published="S1", cameras="1,2,3",
                                                  sigma_mm="5.1", margin_mm="5.2", wire_kind="wedge"), None, [],
         "unclassified"),
        # #1819's classes. A turn is decided before everything, even an END beside it.
        ("a turn's dart, beside an END", row(path="vote", published="S2", picked="turn", agreeing="1"), 10.0, [11.0],
         "turn-total"),
        ("removed, nothing places it", row(path="geometry", flagged="1", published="S20", picked="removed",
                                           cameras="2,3", sigma_mm="5", margin_mm="2.27", wire_kind="wedge"),
         None, [], "phantom-unplaced"),
        ("removed, repeating a dart across an END", row(path="geometry", published="S7", picked="removed"),
         20.0, [17.6], "reread-after-takeout"),
        ("a miss corrected to a score", row(path="vote", published="None", corrected="S13", agreeing="0"),
         None, [], "miss-for-a-dart"),
        ("a miss corrected to a miss is not one", row(path="vote", published="None", corrected="None", picked="published",
                                                       agreeing="0"), None, [], "unclassified"),
        ("lone, across a wedge wire 9.4 mm off (model)", row(path="vote", published="S3", corrected="S19", picked="candidate",
                                                             flagged="1", agreeing="1", radius="0.5746", angle="183.503"),
         None, [], "lone-beyond-sigma"),
        ("lone, across the treble wire 2.5 mm off (model)", row(path="vote", published="S19", corrected="T19",
                                                                picked="alternative", flagged="1", agreeing="1",
                                                                radius="0.6442", angle="193.145"),
         None, [], "ring-wire"),
        ("lone, across a wedge wire 1.8 mm off (model)", row(path="vote", published="S11", corrected="S8", agreeing="1",
                                                             radius="0.2798", angle="263.117"),
         None, [], "lone-wedge-wire"),
        ("lone, across the ring wire 21.3 mm off (model)", row(path="vote", published="S3", corrected="T3", agreeing="1",
                                                               radius="0.755", angle="186.075"),
         None, [], "lone-beyond-sigma"),
        ("a lone OUTER has no wedge wire (model)", row(path="vote", published="25", corrected="S1", agreeing="1",
                                                       ring_wire_mm="2.26", radius="0.0507", angle="195.4884"),
         None, [], "ring-wire"),
        ("lone, across a wedge wire at 5.0 mm is inside", row(path="vote", published="S11", corrected="S8", agreeing="1",
                                                              lone_wire_mm="5.0", ring_wire_mm="30"),
         None, [], "lone-wedge-wire"),
        ("lone, across a wedge wire at 5.1 mm is beyond", row(path="vote", published="S11", corrected="S8", agreeing="1",
                                                              lone_wire_mm="5.1", ring_wire_mm="30"),
         None, [], "lone-beyond-sigma"),
        ("vote ring wire", row(path="vote", published="T19", agreeing="1", lone_wire_mm="14.4", ring_wire_mm="3.06"),
         None, [], "ring-wire"),
        ("consensus ring wire", row(path="vote", published="25", agreeing="2", ring_wire_mm="0.53"), None, [], "ring-wire"),
        ("vote ring wire at 5.1 mm", row(path="vote", published="T19", agreeing="1", ring_wire_mm="5.1"),
         None, [], "unclassified"),
        ("lone wedge wire nearer than the ring", row(path="vote", published="S19", agreeing="1", lone_wire_mm="0.8",
                                                     ring_wire_mm="3.0"), None, [], "lone-wedge-wire"),
        ("consensus wedge margin is not a lone one", row(path="vote", published="S19", agreeing="2",
                                                         lone_wire_mm="0.8"), None, [], "unclassified"),
        ("unflagged geometric", row(path="geometry", published="S3", cameras="1,2,3", sigma_mm="3",
                                    margin_mm="8", wire_kind="wedge"), None, [], "unflagged-geometric"),
        ("flagged geometric clear of its sigma", row(path="geometry", flagged="1", published="S3", cameras="1,2,3",
                                                     sigma_mm="3", margin_mm="8"), None, [], "unclassified"),
    ]
    bad = 0
    for name, r, t, ends, want in cases:
        reread = "0.0 px from the S7 (self-test)" if name.startswith("removed, repeating") else None
        got, why = classify(r, t, ends, reread)
        ok = got == want
        bad += not ok
        out("%s self: %s -> %s (%s)%s" % ("PASS" if ok else "FAIL", name, got, why,
                                          "" if ok else " expected " + want))
    # #1819's re-read, on a three-line log: a dart, an END, the same dart again -- and moved
    # one pixel past REREAD_PX, and with the END struck.
    def score(t, sector, x, y):
        return "[00:57:%06.3f][INFO][SCORER] - SCORE: %s | Position: (%d,%d) | Confidence: 0.9" % (t, sector, x, y)
    end = "[00:57:20.921][INFO][SCORER] - SCORE: END | Position: (-1,-1) | Confidence: 1.0"
    for name, lines, want in [
        ("re-read at 0 px", [score(3.664, "S7", 568, 266), end, score(23.331, "S7", 568, 266)], True),
        ("re-read at 12 px", [score(3.664, "S7", 568, 266), end, score(23.331, "S7", 580, 266)], True),
        ("13 px is a new dart", [score(3.664, "S7", 568, 266), end, score(23.331, "S7", 581, 266)], False),
        ("no END between", [score(3.664, "S7", 568, 266), score(23.331, "S7", 568, 266)], False),
    ]:
        log = read_log("\n".join(lines))
        got = reread_of(log, len(log) - 1)
        ok = (got is not None) == want
        bad += not ok
        out("%s self: %s -> %s" % ("PASS" if ok else "FAIL", name, got))
    cases += [None] * 4
    # The two loud refusals the format promises, on a two-line export.
    for name, text in [
        ("a column the script needs is gone",
         "# turnaus truth line v1 board=device/1 day=2026-10-10 zone=Europe/Helsinki\n# reference published\nX S1\n"),
        ("a field count that is not the columns line's",
         "# turnaus truth line v1 board=device/1 day=2026-10-10 zone=Europe/Helsinki\n# " + " ".join(REQUIRED)
         + "\n" + " ".join(["-"] * (len(REQUIRED) - 1)) + "\n"),
    ]:
        try:
            read_truth(text)
            bad += 1
            out("FAIL self: %s was read without a word" % name)
        except FormatError as e:
            out("PASS self: %s is refused: %s" % (name, e))
    out("I1789 SELF-TEST cases=%d failures=%d" % (len(cases) + 2, bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--truth")
    ap.add_argument("--log")
    ap.add_argument("--expect", action="store_true", help="compare against the export's '# expect' lines")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    out = lambda s: print(s, flush=True)  # noqa: E731
    if a.self_test:
        return 1 if self_test(out) else 0
    if not a.truth:
        ap.error("--truth is required")
    with open(a.truth, encoding="utf-8") as f:
        truth = f.read()
    log = None
    if a.log:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            log = f.read()
    try:
        result = census(truth, log, out)
    except FormatError as e:
        out("I1789 FORMAT-REFUSED %s" % e)
        return 3
    if a.expect:
        return 1 if check(*result, out) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

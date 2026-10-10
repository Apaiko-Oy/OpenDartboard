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
        one per dart with a correction standing (picked is not `-` / `published`), then
        that dart's log window: its SCORE line and the UNCERTAINTY / Geometric score /
        Consensus / No consensus / LONE-WIRE / RING-WIRE / BOARD / I1707 RIM CARRIED lines
        between the previous SCORE line and its push, and its push line; or
    I1789 ABSENT ref=  the reference is not in the log. Never silently dropped.
    I1789 PICK-DISAGREES ref=  `picked` is not what Turnaus's `DartCorrections::picked`
        would decide from `published`, `alternative`, `candidates` and `corrected`.
    I1789 CLASS <class>=<n> ...  the fault-class tally.

THE FAULT CLASSES, decided from the account (the truth line's own fields) in this order,
the first that holds winning; the one class that needs the log is the takeout's, because
the truth line carries no END:

  phantom-takeout   #1781  the dart's SCORE line is within PHANTOM_S of a `SCORE: END` in
                           the log (live 2026-10-10: D11 0.74 s before, S2 0.98 s after)
  rim-one-tip       #1707  a lone vote reading (agreeing 1) that published a double, or was
                           corrected to a miss (`None`): one camera's tip on a rim dart,
                           at the rim or the barrel over the board (live 2026-10-10: D16,
                           D3, D18, D18 at 0.96-0.999, and a miss read S20 at 0.562)
  corner            #1782  a geometric solve within its own sigma of a ring wire AND a wedge
                           wire: the flag offers the nearest wire's alternative only (live
                           16:38:53: T3 for S19, 0.6 mm and 1.8 mm across 5.3 mm)
  two-line-wire     #1766  a two-camera geometric solve within its own sigma of the wire
                           it names (live 16:49:15: S1 for S20, 2.1 mm across 5.1 mm)
  ring-wire         #1773  a vote reading within SIGMA_VOTE_MM of a ring wire, lone or
                           consensus (live: T19 at 3.1 mm, OUTER at 2.3 and 0.5 mm)
  lone-wedge-wire   #1628  a lone vote reading within SIGMA_VOTE_MM of a wedge wire (the
                           nearer of the two margins decides between this and ring-wire)
  unflagged-geometric #1556 a geometric solve that was not flagged and was corrected
  unclassified             the honest default

Stdlib only; it runs in $OD_IMAGE's python 3.11 (the box that runs the suite has no host
python3). Exit 0 when it ran (and, with --expect, every expectation held), 1 when an
expectation failed, 3 when the export is not the format this reads.
"""

import argparse
import math
import re
import sys

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

CLASSES = (
    ("phantom-takeout", "#1781"),
    ("rim-one-tip", "#1707"),
    ("corner", "#1782"),
    ("two-line-wire", "#1766"),
    ("ring-wire", "#1773"),
    ("lone-wedge-wire", "#1628"),
    ("unflagged-geometric", "#1556"),
    ("unclassified", "-"),
)
ISSUE = dict(CLASSES)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LOG_LINE = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\.(\d{3})\]\[[^\]]*\]\[[^\]]*\] - (.*)$")
SCORE_LINE = re.compile(r"^SCORE: (\S+)")
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


def classify(row, dart_time, ends):
    """(class, why) for a corrected dart. `dart_time` is its SCORE line's second of the day,
    or None when it is not in the log; `ends` every END's."""
    if dart_time is not None and ends:
        nearest = min(ends, key=lambda e: abs(e - dart_time))
        if abs(nearest - dart_time) <= PHANTOM_S:
            return "phantom-takeout", "%+.3f s from an END" % (dart_time - nearest)
    path = row["path"]
    agreeing = num(row, "agreeing")
    sigma, margin = num(row, "sigma_mm"), num(row, "margin_mm")
    radius, angle = num(row, "radius"), num(row, "angle")
    if path == "vote" and agreeing == 1:
        if row["published"].startswith("D"):
            return "rim-one-tip", "a lone reading published %s at radius %s" % (row["published"], row["radius"])
        if row["corrected"] == MISS:
            return "rim-one-tip", "a lone reading published %s at radius %s for a miss" % (row["published"], row["radius"])
    if path == "geometry" and sigma is not None and margin is not None and margin <= sigma:
        if radius is not None and angle is not None and radius * SCORING_RADIUS_MM > RING_WIRES_MM[1]:
            other = (nearest_wedge_wire_mm(radius, angle) if row["wire_kind"] == "ring"
                     else nearest_ring_wire_mm(radius) if row["wire_kind"] == "wedge" else None)
            if other is not None and other <= sigma:
                return "corner", "%s wire %.1f mm and the other %.1f mm, both inside %.1f mm" % (
                    row["wire_kind"], margin, other, sigma)
        cameras = [] if row["cameras"] == "-" else row["cameras"].split(",")
        if len(cameras) == 2:
            return "two-line-wire", "cameras %s, %s wire %.1f mm across a %.1f mm sigma" % (
                row["cameras"], row["wire_kind"], margin, sigma)
    if path == "vote":
        ring, wedge = num(row, "ring_wire_mm"), num(row, "lone_wire_mm") if agreeing == 1 else None
        near = [(m, k) for m, k in ((ring, "ring-wire"), (wedge, "lone-wedge-wire"))
                if m is not None and m <= SIGMA_VOTE_MM]
        if near:
            m, k = min(near)
            return k, "%s %.1f mm inside the %.0f mm sigma (agreeing %s)" % (
                "ring wire" if k == "ring-wire" else "wedge wire", m, SIGMA_VOTE_MM, row["agreeing"])
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


def window_of(log, reference, published):
    """(dart_time, [lines]) for the dart whose push names `reference`, or None."""
    needle = '"reference":"%s"' % reference
    push = next((i for i, (_, msg) in enumerate(log) if msg.startswith("TURNAUS: ") and needle in msg), None)
    if push is None:
        return None
    score = None
    for i in range(push - 1, -1, -1):
        m = SCORE_LINE.match(log[i][1])
        if m and POSTABLE.get(m.group(1), m.group(1)) == published:
            score = i
            break
    if score is None:
        return log[push][0], [log[push]]
    start = 0
    for i in range(score - 1, -1, -1):
        if SCORE_LINE.match(log[i][1]) or "Session Started" in log[i][1]:
            start = i + 1
            break
    shown = [log[i] for i in range(start, push + 1)
             if i == push or log[i][1].startswith(SHOWN)]
    return log[score][0], shown


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
        out("I1789 LOG lines=%d ends=%d" % (len(log), len(ends)))
    classes, absent = {}, []
    for row in rows:
        disagrees = pick_disagrees(row)
        if disagrees:
            out("I1789 PICK-DISAGREES ref=%s line=%d %s" % (row["reference"], row["_line"], disagrees))
        if not standing(row):
            continue
        found = window_of(log, row["reference"], row["published"]) if log else None
        dart_time = found[0] if found else None
        k, why = classify(row, dart_time, ends)
        classes[row["reference"]] = k
        out("I1789 DART ref=%s published=%s corrected=%s picked=%s path=%s flagged=%s class=%s issue=%s why=%s"
            % (row["reference"], row["published"], row["corrected"], row["picked"], row["path"],
               row["flagged"], k, ISSUE[k], why.replace(" ", "_")))
        if found:
            for t, msg in found[1]:
                out("    %s %s" % (clock(t) if t is not None else "--:--:--.---", msg))
        else:
            absent.append(row["reference"])
            out("I1789 ABSENT ref=%s %s" % (row["reference"], "no log was given" if log is None
                                             else "the reference is not in the log"))
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
                                            sigma_mm="5.1", margin_mm="2.1", wire_kind="wedge"), None, [], "unclassified"),
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
        got, why = classify(r, t, ends)
        ok = got == want
        bad += not ok
        out("%s self: %s -> %s (%s)%s" % ("PASS" if ok else "FAIL", name, got, why,
                                          "" if ok else " expected " + want))
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

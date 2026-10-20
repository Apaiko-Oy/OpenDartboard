"""turnaus#1911: why each camera offered no line, counted per dart, by camera, radius and reason.

    python3 i1911_census.py --log <log> [--log <log> ...] [--label <name>]
    python3 i1911_census.py --self-test

WHAT IT READS. Either kind of log the detector writes, a bakeoff replay's `out` text or a
board's `--log-file` (a Pi's own, or the day Turnaus serves at
`/api/v1/operator/boards/<kind>/<id>/<day>/log`, #1787):

  * `I1911 REFUSALS` lines (this issue's, one per published dart, every camera's reason with
    its straightness rms, its elongation and the sigma factor an admitted line carries, and
    the dart's published board radius). A log holding any is read from them alone.
  * otherwise the stories every board since #1555 already writes: one `PATH:` line per dart
    (the geometric entry publishes, or DEGRADED with the solver's exclusions in its words),
    the `LINES:` line after it when a camera's line was not used, and the dart's radius from
    the `board_radius` of the `TURNAUS:` push that posted it (joined to the dart by its
    published sector, the latest unposted one before the push, i1789's join). A camera the
    stories do not name was a used line.

Reasons are the refusal's own first words: `not-straight` (the 2.5 px gate), `not-a-shaft`
(the 3.0 elongation gate), `rim-only` (#1689), `no-figure`, `no-frame`, `direction-unusable`,
`other` (any other axis refusal: departure, clean, support floor), and from the stories only,
`solver` (an axis the solve itself excluded: inconsistent, a fit refused). A NEAR-PARALLEL or
INCONSISTENT dart's outcome is counted beside the reasons, since no camera is named for it.

WHAT IT PRINTS, per --label (one log, or several pooled):

    I1911 DARTS     darts, geometry, vote, r>0.90, vote at r>0.90, and the vote's outcomes
    I1911 REASON    reason x camera, refusals counted per camera per dart
    I1911 RADIUS    reason x radius bin, the same refusals
    I1911 STRAIGHT  the not-straight residuals: n, median, and how many a 3.0 and a 4.0 px
                    gate would admit (the figure OD_STRAIGHT_PX moves)

A reporter: it asserts nothing about the numbers. --self-test holds the parser on planted
lines of both kinds and exits non-zero on any difference.
"""

import argparse
import json
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
# A board's log stamps each line `[HH:MM:SS.mmm][LEVEL][module] - `; a replay's (the
# bakeoff's `<run>.txt`) carries `[LEVEL][module] - ` alone. Either is a message.
LOG_LINE = re.compile(r"^(?:\[[^\]]*\])+ - (.*)$")
SCORE_LINE = re.compile(r"^SCORE: (\S+)")
POSTABLE = {"MISS": "None", "OUTER": "25", "BULL": "Bull"}  # i1789's, the push's spelling
REFUSALS = re.compile(r"I1911 REFUSALS window=(-?\d+) path=(\S+) score=(\S+) r=(\S+) gate=(\S+)((?: cam\d+=\S+)*)")
CAMSLOT = re.compile(r"cam(\d+)=([a-z+-]+),rms=([-0-9.]+),elong=([-0-9.]+),scale=([-0-9.]+)")
CAMSTORY = re.compile(r"cam (\d): ")
RMS_WORDS = re.compile(r"scatters ([0-9.]+) px RMS")
ELONG_WORDS = re.compile(r"px is ([0-9.]+), under the")

# Board radius, 1.0 the double's outer wire (170 mm): the treble 99-107 mm and the double
# 162-170 mm of the standard board (perspective_processing::DartboardSpec), with the issue's
# own 0.90 split between the outer single and the run-in to the double.
BINS = [("<0.58", 0.0, 99.0 / 170), ("treble", 99.0 / 170, 107.0 / 170), ("0.63-0.90", 107.0 / 170, 0.90),
        ("0.90-0.95", 0.90, 162.0 / 170), ("double", 162.0 / 170, 1.0), (">1.00", 1.0, 99.0)]
REASONS = ["not-straight", "not-a-shaft", "rim-only", "no-figure", "no-frame", "direction-unusable",
           "other", "solver"]


def radius_bin(r):
    if r is None:
        return "unknown"
    for name, lo, hi in BINS:
        if lo <= r < hi:
            return name
    return "unknown"


def reason_of_story(text):
    """(reason, rms, elongation) of one camera's exclusion in the solver's words."""
    if not text.startswith("no usable axis: "):
        return "solver", None, None
    why = text[len("no usable axis: "):]
    rms = RMS_WORDS.search(why)
    elong = ELONG_WORDS.search(why)
    rms = float(rms.group(1)) if rms else None
    elong = float(elong.group(1)) if elong else None
    for words, reason in (("rim only", "rim-only"), ("no frame captured", "no-frame")):
        if why.startswith(words):
            return reason, rms, elong
    named = [r for w, r in (("not a shaft", "not-a-shaft"), ("not straight", "not-straight"),
                            ("direction unusable", "direction-unusable"), ("no fresh figure", "no-figure"))
             if w in why]
    return ("+".join(named) if named else "other"), rms, elong


def cameras_of_story(text):
    """{camera: exclusion text} from '(cam 1: ...; cam 2: ...)' inside a PATH or LINES line."""
    marks = list(CAMSTORY.finditer(text))
    out = {}
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out[int(m.group(1))] = text[m.end():end].rstrip(" ;)")
    return out


def messages(text):
    for raw in text.splitlines():
        m = LOG_LINE.match(ANSI.sub("", raw))
        if m:
            yield m.group(1)


def darts_from_census(text):
    darts = []
    for msg in messages(text):
        m = REFUSALS.search(msg)
        if not m or m.group(3) == "END":
            continue  # an END (a takeout) is not a dart, and the binary wrote one for it
        cams = {}
        for c in CAMSLOT.finditer(m.group(6)):
            cams[int(c.group(1))] = (c.group(2), float(c.group(3)), float(c.group(4)))
        darts.append({"path": m.group(2), "r": None if m.group(4) == "-" else float(m.group(4)),
                      "outcome": None, "cams": {k: v for k, v in cams.items() if v[0] != "ok"},
                      "cameras": len(cams)})
    return darts


def darts_from_stories(text, cameras=3):
    darts, pending = [], None
    for msg in messages(text):
        if msg.startswith("PATH: "):
            pending = None
            d = {"path": "geometry", "r": None, "outcome": None, "cams": {}, "cameras": cameras,
                 "score": None, "posted": False}
            if msg.startswith("PATH: DEGRADED"):
                d["path"] = "vote"
                o = re.search(r"no geometric entry \(([A-Z-]+):", msg)
                d["outcome"] = o.group(1) if o else "UNNAMED"
            elif not msg.startswith("PATH: the geometric entry publishes"):
                d["path"] = "vote"
                d["outcome"] = "OTHER"
            for cam, why in cameras_of_story(msg).items():
                reason, rms, elong = reason_of_story(why)
                d["cams"][cam] = (reason, rms, elong)
            darts.append(d)
            pending = d
        elif msg.startswith("LINES: ") and pending is not None:
            for cam, why in cameras_of_story(msg).items():
                reason, rms, elong = reason_of_story(why)
                pending["cams"][cam] = (reason, rms, elong)
        elif SCORE_LINE.match(msg) and pending is not None and pending["score"] is None:
            s = SCORE_LINE.match(msg).group(1)
            pending["score"] = POSTABLE.get(s, s)
        elif msg.startswith("TURNAUS: ") and '"reference"' in msg:
            body = msg[msg.index("{"):] if "{" in msg else ""
            try:
                push = json.loads(body)
            except ValueError:
                continue
            for d in reversed(darts):
                if not d["posted"] and d["score"] == push.get("sector"):
                    d["posted"] = True
                    r = push.get("board_radius")
                    d["r"] = float(r) if isinstance(r, (int, float)) and r >= 0 else None
                    break
    return darts


def darts_of(text):
    census = darts_from_census(text)
    return (census, "I1911 REFUSALS") if census else (darts_from_stories(text), "PATH/LINES stories")


def report(label, darts, source, out):
    total = len(darts)
    vote = [d for d in darts if d["path"] != "geometry"]
    far = [d for d in darts if d["r"] is not None and d["r"] > 0.90]
    far_vote = [d for d in far if d["path"] != "geometry"]
    outcomes = {}
    for d in vote:
        if d["outcome"]:
            outcomes[d["outcome"]] = outcomes.get(d["outcome"], 0) + 1
    cams = sorted({c for d in darts for c in range(1, d["cameras"] + 1)})
    out.write("I1911 DARTS label=%s source=%s darts=%d geometry=%d vote=%d (%.1f%%) r>0.90=%d vote_at_r>0.90=%d "
              "(%s) radius_unknown=%d outcomes=%s\n" % (
                  label, source.replace(" ", "_"), total, total - len(vote), len(vote),
                  100.0 * len(vote) / total if total else 0.0, len(far), len(far_vote),
                  "%.0f%%" % (100.0 * len(far_vote) / len(far)) if far else "-",
                  sum(1 for d in darts if d["r"] is None),
                  ",".join("%s:%d" % kv for kv in sorted(outcomes.items())) or "-"))
    by_cam, by_bin, straight = {}, {}, []
    for d in darts:
        b = radius_bin(d["r"])
        for cam, (reason, rms, _elong) in d["cams"].items():
            for r in reason.split("+"):
                by_cam.setdefault(r, {}).setdefault(cam, 0)
                by_cam[r][cam] += 1
                by_bin.setdefault(r, {}).setdefault(b, 0)
                by_bin[r][b] += 1
            if "not-straight" in reason and rms is not None:
                straight.append(rms)
    bins = [n for n, _, _ in BINS] + ["unknown"]
    out.write("I1911 REASON label=%s | reason | %s | all | of which on vote darts\n" % (
        label, " | ".join("cam %d" % c for c in cams)))
    for r in REASONS + sorted(set(by_cam) - set(REASONS)):
        if r not in by_cam:
            continue
        on_vote = sum(1 for d in vote for (rr, _, _) in d["cams"].values() if r in rr.split("+"))
        out.write("I1911 REASON label=%s | %s | %s | %d | %d\n" % (
            label, r, " | ".join(str(by_cam[r].get(c, 0)) for c in cams), sum(by_cam[r].values()), on_vote))
    out.write("I1911 RADIUS label=%s | reason | %s\n" % (label, " | ".join(bins)))
    for r in REASONS + sorted(set(by_bin) - set(REASONS)):
        if r not in by_bin:
            continue
        out.write("I1911 RADIUS label=%s | %s | %s\n" % (label, r, " | ".join(str(by_bin[r].get(b, 0)) for b in bins)))
    straight.sort()
    med = straight[len(straight) // 2] if straight else None

    def reach(gate):
        """Vote darts short of two lines that a `gate` px straightness gate would give two:
        the cameras already used, plus every refusal that was not-straight ALONE at or
        under the gate. An upper bound: an admitted line can still fail the solve."""
        n = 0
        for d in vote:
            used = d["cameras"] - len(d["cams"])
            if used >= 2:
                continue
            more = sum(1 for (r, rms, _e) in d["cams"].values()
                       if r == "not-straight" and rms is not None and rms <= gate)
            n += 1 if used + more >= 2 else 0
        return n

    out.write("I1911 STRAIGHT label=%s n=%d median=%s min=%s max=%s le3.0=%d le4.0=%d gt4.0=%d "
              "vote_darts_given_two_lines_at_3.0=%d at_4.0=%d\n" % (
                  label, len(straight), "%.2f" % med if med is not None else "-",
                  "%.2f" % straight[0] if straight else "-", "%.2f" % straight[-1] if straight else "-",
                  sum(1 for v in straight if v <= 3.0), sum(1 for v in straight if v <= 4.0),
                  sum(1 for v in straight if v > 4.0), reach(3.0), reach(4.0)))


def self_test(out):
    bad = []

    def expect(what, got, want):
        ok = got == want
        out.write("%s %s: got %r\n" % ("OK  " if ok else "FAIL", what, got))
        if not ok:
            bad.append(what)

    story = ("[14:41:36.285][INFO][SCORE_PROCESSING] - PATH: DEGRADED -- no geometric entry (TOO-FEW-CONSTRAINTS: "
             "no entry: 1 usable constraint(s) of 3 cameras, and one line is anywhere along itself (cam 1: no usable "
             "axis: not straight: the kept centreline scatters 19.47 px RMS about the line against a 2.5 px gate -- a "
             "bent silhouette, a shadow lobe, or two objects one line does not explain; cam 2: no usable axis: rim "
             "only: this camera's fresh change clears the floor out to the rim but not in the scoring area, so it votes "
             "the arrival and offers no figure to fit (#1689))), so the string vote publishes this dart\n"
             "[14:41:36.300][INFO][SCORER] - SCORE: D16 | Position: (1,2) | Confidence: 0.7 | Camera: 0\n"
             "[14:41:37.019][INFO][SCORE_PROCESSING] - PATH: the geometric entry publishes (SOLVED)\n"
             "[14:41:37.019][INFO][SCORE_PROCESSING] - LINES: 2 of 3 cameras' lines solved this dart (cam 3: no usable "
             "axis: not a shaft: extent 30.0 px over median width 12.0 px is 2.50, under the 3.0 elongation gate (a "
             "near-end-on dart or a blob, not a line of evidence))\n"
             "[14:41:37.100][INFO][SCORER] - SCORE: MISS | Position: (1,2) | Confidence: 0.9 | Camera: 0\n"
             "[14:41:39.000][INFO][TURNAUSCLIENT] - TURNAUS: COUNTED {\"board_radius\":0.9615,\"reference\":\"A\","
             "\"sector\":\"D16\"}\n"
             "[14:41:39.100][INFO][TURNAUSCLIENT] - TURNAUS: COUNTED {\"board_radius\":1.08,\"reference\":\"B\","
             "\"sector\":\"None\"}\n")
    darts, source = darts_of(story)
    expect("stories: source", source, "PATH/LINES stories")
    expect("stories: two darts, vote then geometry", [d["path"] for d in darts], ["vote", "geometry"])
    expect("stories: the vote dart's cameras", darts[0]["cams"],
           {1: ("not-straight", 19.47, None), 2: ("rim-only", None, None)})
    expect("stories: LINES names the geometric dart's refused camera", darts[1]["cams"],
           {3: ("not-a-shaft", None, 2.5)})
    expect("stories: radius joined by sector, MISS as None", [d["r"] for d in darts], [0.9615, 1.08])
    expect("stories: outcome", darts[0]["outcome"], "TOO-FEW-CONSTRAINTS")
    expect("bins", [radius_bin(0.3), radius_bin(0.6), radius_bin(0.92), radius_bin(0.97), radius_bin(None)],
           ["<0.58", "treble", "0.90-0.95", "double", "unknown"])
    census = (story + "[14:41:36.290][INFO][GEOMETRYDETECTOR] - I1911 REFUSALS window=17 path=vote score=D16 "
              "r=0.9615 gate=2.50 cam1=not-straight,rms=19.47,elong=8.10,scale=1.00 cam2=rim-only,rms=0.00,"
              "elong=0.00,scale=1.00 cam3=ok,rms=1.20,elong=14.00,scale=1.00\n")
    darts, source = darts_of(census)
    bare, _ = darts_of("[INFO][GEOMETRYDETECTOR] - I1911 REFUSALS window=1 path=geometry score=T13 r=0.5821 "
                       "gate=2.50 cam1=ok,rms=0.28,elong=8.29,scale=1.00\n")
    ends, _ = darts_of("[INFO][GEOMETRYDETECTOR] - I1911 REFUSALS window=-1 path=vote score=END r=- gate=2.50 "
                       "cam1=other,rms=0.00,elong=0.00,scale=1.00\n")
    expect("census: an END is not a dart", len(ends), 0)
    expect("census: a replay's unstamped line is read", [(d["path"], d["r"]) for d in bare], [("geometry", 0.5821)])
    expect("census: a log with the line is read from it alone", (source, len(darts)), ("I1911 REFUSALS", 1))
    expect("census: cameras", darts[0]["cams"], {1: ("not-straight", 19.47, 8.1), 2: ("rim-only", 0.0, 0.0)})
    expect("census: radius", darts[0]["r"], 0.9615)
    out.write("SELF-TEST %s (%d failure(s))\n" % ("PASSED" if not bad else "FAILED", len(bad)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--log", action="append", default=[])
    ap.add_argument("--label", default=None)
    ap.add_argument("--each", action="store_true", help="one report per --log as well as the pool")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test(sys.stdout)
    if not a.log:
        ap.error("--log is required")
    pooled, sources = [], set()
    for path in a.log:
        with open(path, errors="replace") as f:
            darts, source = darts_of(f.read())
        sources.add(source)
        if a.each:
            report(path.rsplit("/", 1)[-1], darts, source, sys.stdout)
        pooled += darts
    report(a.label or "pool", pooled, "+".join(sorted(sources)), sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())

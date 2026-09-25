"""#1618: why a correctly calibrated third camera made rig-20260922 dev worse, per dart.

    python3 i1618_census.py --log <run.txt> --annotations <fixture.csv> --label <name>
        [--no-arrival 1.1]

One detector run made with OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 (i1555_inside.sh's shape).
It prints three things, and decides nothing -- the harness asserts on them:

  I1618 DART    one line per annotated arrival: which window the matcher gave it (the
                SAME matcher i1511/i1555 use, imported, so no two censuses disagree about
                which detection was which dart), what published, by which path, and per
                camera what that camera brought -- whether its axis was valid, its angle
                off the hand-annotated shaft, how far the annotated tip lies off its axis,
                and whether the entry solver used it. This is the side-by-side the issue
                asks for: run it over the dev and the opening logs and read camera 1.

  I1618 ECHO    an ADVANCING window opened sooner after the previous advancing window of
                the same visit than any two real arrivals of this fixture are apart. The
                bound is read off the annotations (every row carries "arrives fNNN"):
                half the shortest gap between consecutive darts of one visit. A window
                that close behind another is the same throw called a second time.

  I1618 CAM1    camera 1's valid axes against the annotation, median and max -- whether
                camera 1's own evidence is what went wrong.

The frame numbers in the annotations and the window cycles in the log are different
clocks (a window's cycle is frames read since calibration ended), so the echo bound is a
DURATION taken from the annotations and compared with a duration in the log -- one frame
is one cycle for a file source -- never an index against an index.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402  (the matcher, #1504's alignment)
import i1555_census as path_census  # noqa: E402  (the publish blocks)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
CAM_RE = re.compile(r"I1512CAM window=(\d+) cam=(\d) usable=(\d) .* excl=(.*)")
ENTRY_RE = re.compile(r"I1512ENTRY window=(\d+) outcome=(\S+) .*pair=(\S+) score=(\S+) .*usable=(\d)")
OPENED_RE = re.compile(r"I1511AXIS window=(\d+) opened=(\d+) ")
END_RE = re.compile(r"SCORE:\s+END\b")
ARRIVES_RE = re.compile(r"arrives f(\d+)")


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else float("nan")


def shortest_gap(path):
    """Frames between the closest two consecutive arrivals of one visit, from the notes."""
    arrive = {}
    for raw in open(path, errors="replace"):
        parts = raw.rstrip("\n").split(",")
        if len(parts) < 13 or parts[0] == "fixture":
            continue
        m = ARRIVES_RE.search(",".join(parts[12:]))
        if m:
            arrive[(int(parts[1]), int(parts[2]))] = int(m.group(1))
    gaps = []
    for (v, d), f in arrive.items():
        if (v, d + 1) in arrive:
            gaps.append(arrive[(v, d + 1)] - f)
    return min(gaps) if gaps else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--label", default="run")
    ap.add_argument("--no-arrival", default="")
    args = ap.parse_args()

    no_arrival = set()
    for token in args.no_arrival.split(","):
        if token.strip():
            v, d = token.strip().split(".")
            no_arrival.add((int(v), int(d)))

    annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    blocks, _ = path_census.read_publish_blocks(args.log)
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, blocks):
        ev.pub = block

    cams, entries = {}, {}
    opened = []  # (window, opened cycle) of every ADVANCING window, END markers as None
    for raw in open(args.log, errors="replace"):
        line = ANSI.sub("", raw)
        m = CAM_RE.search(line)
        if m:
            cams[(int(m.group(1)), int(m.group(2)))] = (m.group(3) == "1", m.group(4).strip())
            continue
        m = ENTRY_RE.search(line)
        if m:
            entries[int(m.group(1))] = m.groups()
            continue
        m = OPENED_RE.search(line)
        if m:
            w, c = int(m.group(1)), int(m.group(2))
            if not opened or opened[-1] is None or opened[-1][0] != w:
                opened.append((w, c))
            continue
        if END_RE.search(line):
            opened.append(None)

    assignment = axis_census.assign_events(visits, annots, no_arrival)
    by_key = {key: pos for pos, key in assignment["assigned"].items()}
    cam1_ang, cam1_perp = [], []
    for key in sorted(annots):
        if key in no_arrival:
            continue
        rows = annots[key]
        thrown = list(rows.values())[0]["thrown"]
        if key not in by_key:
            print("I1618 DART %s v%d.%d thrown=%s UNDETECTED" % (args.label, key[0], key[1], thrown))
            continue
        v, ei = by_key[key]
        ev = visits[v][ei]
        pub = getattr(ev, "pub", None) or {}
        entry = entries.get(ev.window)
        s = ("I1618 DART %s v%d.%d thrown=%s window=%d published=%s path=%s vote=%s geo=%s"
             % (args.label, key[0], key[1], thrown, ev.window, pub.get("score", "?"),
                pub.get("path", "?"), pub.get("vote", "?"), pub.get("geo", "?")))
        if entry:
            s += " entry=%s/pair=%s/usable=%s" % (entry[1], entry[2], entry[4])
        for cam in (1, 2, 3):
            obs = ev.cams.get(cam)
            ann = rows.get(cam)
            used = cams.get((ev.window, cam))
            word = "cam%d:" % cam
            if obs is None:
                word += "none"
            elif not obs["valid"]:
                word += "refused(%s)" % obs["refusal"].split(":")[0]
            else:
                word += "valid"
                if ann:
                    (p1, p2) = ann["line"]
                    ang = axis_census.angle_diff(axis_census.line_angle(p1, p2), obs["angle"])
                    word += ",dAng=%.1f" % ang
                    if ann["tip"]:
                        perp = axis_census.perp_distance(ann["tip"], obs["p"], obs["d"])
                        word += ",tipPerp=%.1f" % perp
                        if cam == 1:
                            cam1_ang.append(ang)
                            cam1_perp.append(perp)
            if used is not None:
                word += ",solver=%s" % ("used" if used[0] else "no")
            s += " | " + word
        print(s)
    for (v, ei) in assignment["unmatched"]:
        ev = visits[v][ei]
        pub = getattr(ev, "pub", None) or {}
        print("I1618 UNCLAIMED %s v%d#%d window=%d published=%s"
              % (args.label, v + 1, ei + 1, ev.window, pub.get("score", "?")))

    gap = shortest_gap(args.annotations)
    bound = gap // 2 if gap else 30
    echoes = []
    for prev, cur in zip(opened, opened[1:]):
        if prev is None or cur is None:
            continue
        if cur[1] - prev[1] < bound:
            echoes.append((cur[0], cur[1], cur[1] - prev[1], prev[0]))
    for w, c, g, pw in echoes:
        print("I1618 ECHO %s window=%d opened=%d gap=%d after window=%d" % (args.label, w, c, g, pw))
    print("I1618 ECHOES %s n=%d bound=%d shortest_arrival_gap=%s max_gap=%s"
          % (args.label, len(echoes), bound, gap, max([e[2] for e in echoes]) if echoes else "-"))
    print("I1618 CAM1 %s valid=%d median_dAng=%.2f max_dAng=%.2f median_tipPerp=%.1f max_tipPerp=%.1f"
          % (args.label, len(cam1_ang), med(cam1_ang), max(cam1_ang) if cam1_ang else float("nan"),
             med(cam1_perp), max(cam1_perp) if cam1_perp else float("nan")))
    return 0


if __name__ == "__main__":
    sys.exit(main())

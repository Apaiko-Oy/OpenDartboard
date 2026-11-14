#!/usr/bin/env python3
"""turnaus#1994: what any fusion of the cameras' evidence could gain, counted before the switch.

Two halves, never summed.

  i1994_census.py --bakeoff <runs/.../1555>
      The fixtures. For every dart of the six graded replays (the pin is left out: it is the
      same footage with the geometry switched off), read from the bakeoff's own census and
      log lines:
        * each camera's own reading -- its tip's score string (I1628LONE) -- against the throw;
        * which cameras gave the solve a line (I1512CAM usable/excluded) and which placed a
          tip inside the scoring area without giving a line;
        * the dart's EVIDENCE CLASS, which is what a fused solve could add to today's:
            L3      three lines solved it: no camera is left out, a tip constraint adds nothing
            L2+T    two lines solved it and the third camera placed a tip on the board
            L2      two lines, and the third camera placed no tip on the board
            L1+T    one line (no solve today) and another camera's tip on the board
            L1      one line and no other camera's tip
            T2+     no line, two or more cameras' tips on the board
            T1      no line, one tip        NONE   nothing placed
      `I1994 WRONG` names every wrong dart, `I1994 CLASS` counts right and wrong per class,
      `I1994 BOUND` states the two bounds: ORACLE (some camera's own reading held the thrown
      cell -- the most any chooser could gain) and REACH (the dart is in a class where the
      fused solve has a constraint today's solve lacks: L2+T, L1+T, T2+).
      `I1994 TIPS` is the placed tips' distance from the solved entry, which is what a tip
      constraint's sigma has to be read from.

  i1994_census.py --live LOG TRUTH [LOG TRUTH ...]
      The live sessions. A shipped log carries NO camera's own reading (I1628LONE and
      I1512CAM are behind the census pin), so the oracle bound cannot be computed live. What
      it does carry is each camera's axis refusal (the PATH: DEGRADED sentence and the
      LINES sentence), so the evidence class can be recovered -- with "the camera placed a
      tip" inferred from "its refusal is not rim-only / no-figure" (#1689: those two offer
      no figure). `I1994 LIVE-CLASS` counts COUNTED darts per path and class, standing and
      corrected (a correction to a sector; removed darts and Casual turn totals are no
      misreading). REACH is the corrected darts in L2+T / L1+T / T2+; the same classes'
      standing darts are what the switch can also move.

  i1994_census.py --self-test
"""
import json
import os
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
RUNS = ["r18-dev", "r18-open", "r22-dev", "r22-open", "r29-dev", "r29-open"]
SCORING_MM = 170.0
REACH = ("L2+T", "L1+T", "T2+")
ORDER = ["L3", "L2+T", "L2", "L1+T", "L1", "T2+", "T1", "NONE"]


def kv(line):
    return dict(m.groups() for m in re.finditer(r"(\w+)=(\S+)", line))


def class_of(lines, spare_tips):
    if lines >= 3:
        return "L3"
    if lines == 2:
        return "L2+T" if spare_tips else "L2"
    if lines == 1:
        return "L1+T" if spare_tips else "L1"
    if spare_tips >= 2:
        return "T2+"
    return "T1" if spare_tips == 1 else "NONE"


def windows_of(out_path):
    """window -> {entry, cams{n: fields}, lone{n: reading}} from one replay's log."""
    wins = {}
    for raw in open(out_path, errors="replace"):
        line = ANSI.sub("", raw)
        m = re.search(r"(I1512ENTRY|I1512CAM|I1628LONE) window=(\d+) ", line)
        if not m:
            continue
        w = wins.setdefault(int(m.group(2)), {"entry": {}, "cams": {}, "lone": {}})
        body = line[m.start():]
        if m.group(1) == "I1512ENTRY":
            w["entry"] = kv(body.split(" story=")[0])
        elif m.group(1) == "I1512CAM":
            f = kv(body.split(" excl=")[0])
            f["excl"] = body.split(" excl=")[1].strip() if " excl=" in body else "-"
            w["cams"][int(f["cam"])] = f
        else:
            for c in re.finditer(r"cam(\d)=([^/\s]+)/", body):
                w["lone"][int(c.group(1))] = c.group(2)
    return wins


def evidence_of(w):
    lines, spare, tips = [], [], []
    for n, f in sorted(w["cams"].items()):
        line = f.get("usable") == "1" and f.get("excluded") == "0"
        on_board = f.get("tipPlaced") == "1" and float(f.get("tipR", "0")) <= SCORING_MM
        if line:
            lines.append(n)
        elif on_board:
            spare.append(n)
        if on_board:
            tips.append(n)
    # A near-parallel pair is two usable lines and no solve: the solve had no entry.
    solved = w["entry"].get("solved") == "1"
    return lines, spare, tips, solved


def bakeoff(root):
    total = {c: [0, 0] for c in ORDER}
    oracle = reach = wrong_n = darts = dissent_right = 0
    tipd = []
    for run in RUNS:
        census, out = os.path.join(root, "census-%s.txt" % run), os.path.join(root, "%s.out" % run)
        wins = windows_of(out)
        pairs, win_of = {}, {}
        for line in open(census):
            if line.startswith("I1555 PAIR "):
                m = re.match(r"I1555 PAIR (v\S+) thrown=(\S+) .*published=(\S+) (\S+) path=(\S+)", line)
                if m:
                    pairs[m.group(1)] = m.groups()[1:]
            elif "CAMERA-DART" in line:
                f = kv(line)
                if "win" in f and "dart" in f:
                    win_of[f["dart"]] = int(f["win"])
        for dart, (thrown, published, verdict, path) in pairs.items():
            w = wins.get(win_of.get(dart, -1))
            if w is None:
                print("I1994 UNJOINED run=%s dart=%s (no window in the log)" % (run, dart))
                continue
            lines, spare, tips, solved = evidence_of(w)
            n_lines = len(lines) if solved else min(len(lines), 1 if len(lines) < 2 else 0)
            cls = class_of(len(lines) if solved else (1 if len(lines) == 1 else 0),
                           len(spare) if solved or len(lines) == 1 else len(tips))
            right = verdict == "exact"
            darts += 1
            total[cls][0 if right else 1] += 1
            readers = {n: s for n, s in sorted(w["lone"].items()) if s != "-"}
            held = [n for n, s in readers.items() if s == thrown]
            if solved:
                for n, f in w["cams"].items():
                    if f.get("tipPlaced") == "1" and float(f["tipR"]) <= SCORING_MM:
                        tipd.append((float(f["tipDist"]), f.get("usable") == "1"))
            if right:
                if any(s != thrown for s in readers.values()):
                    dissent_right += 1
                continue
            wrong_n += 1
            oracle += 1 if held else 0
            reach += 1 if cls in REACH else 0
            e = w["entry"]
            print("I1994 WRONG run=%s dart=%s thrown=%s published=%s path=%s class=%s lines=%s spare_tips=%s "
                  "readings=%s held_by=%s solve=%s boundary_mm=%s resid_mm=%s tipR=%s refusals=%s" % (
                      run, dart, thrown, published, path, cls,
                      ",".join(map(str, lines)) or "-", ",".join(map(str, spare)) or "-",
                      ",".join("cam%d:%s" % (n, s) for n, s in readers.items()) or "-",
                      ",".join("cam%d" % n for n in held) or "none",
                      e.get("score", "-") if solved else "none", e.get("boundary", "-") if solved else "-",
                      ",".join("cam%d:%s" % (n, f["resid_mm"]) for n, f in sorted(w["cams"].items())
                               if f.get("usable") == "1") or "-",
                      ",".join("cam%d:%s" % (n, f["tipR"]) for n, f in sorted(w["cams"].items())
                               if f.get("tipPlaced") == "1") or "-",
                      ",".join("cam%d:%s" % (n, short(f["excl"])) for n, f in sorted(w["cams"].items())
                               if f["excl"] != "-") or "-"))
            del n_lines
    for c in ORDER:
        print("I1994 CLASS class=%s right=%d wrong=%d reach=%d" % (c, total[c][0], total[c][1], 1 if c in REACH else 0))
    print("I1994 BOUND fixtures darts=%d wrong=%d oracle=%d reach=%d reach_standing=%d right_with_a_dissenting_reading=%d" % (
        darts, wrong_n, oracle, reach, sum(total[c][0] for c in REACH), dissent_right))
    for label, pick in (("with-line", True), ("no-line", False)):
        d = sorted(x for x, has in tipd if has == pick)
        if d:
            print("I1994 TIPS on_board=%s n=%d median=%.1f p75=%.1f p90=%.1f max=%.1f within15=%d" % (
                label, len(d), d[len(d) // 2], d[(3 * len(d)) // 4], d[(9 * len(d)) // 10], d[-1],
                sum(1 for x in d if x <= 15.0)))
    return 0


def short(excl):
    for word, tag in (("rim only", "rim-only"), ("not a shaft", "not-a-shaft"), ("not straight", "not-straight"),
                      ("rescued axis", "rescued-held"), ("inconsistent", "inconsistent")):
        if word in excl:
            m = re.search(r"scatters ([0-9.]+) px", excl)
            return tag + ("(%s px)" % m.group(1) if m and tag == "not-straight" else "")
    return "other"


CAM = re.compile(r"cam (\d): ")


def refused(sentence):
    """camera -> True if it could still have placed a tip, from a PATH/LINES sentence."""
    out = {}
    starts = [(m.start(), int(m.group(1))) for m in CAM.finditer(sentence)]
    for k, (pos, cam) in enumerate(starts):
        seg = sentence[pos:starts[k + 1][0] if k + 1 < len(starts) else len(sentence)]
        out[cam] = not ("rim only" in seg or "no fresh figure" in seg or "no frame" in seg or
                        "no accepted board fit" in seg or "rotation unresolved" in seg)
    return out


def live_darts(log_path):
    darts, pending, cursor = [], None, 0
    for raw in open(log_path, errors="replace"):
        line = ANSI.sub("", raw)
        if "SCORE_PROCESSING] - PATH:" in line:
            pending = {"degraded": "PATH: DEGRADED" in line, "near_parallel": "NEAR-PARALLEL" in line,
                       "refused": refused(line) if "PATH: DEGRADED" in line else {}, "lines_said": False}
        elif pending is not None and "SCORE_PROCESSING] - LINES:" in line:
            pending["refused"] = refused(line)
            pending["lines_said"] = True
        elif "[SCORER] - SCORE:" in line:
            m = re.search(r"SCORE: (\S+) \|", line)
            if m and m.group(1) != "END" and pending is not None:
                pending["score"] = m.group(1)
                darts.append(pending)
            pending = None
        elif "TURNAUS: COUNTED {" in line or "TURNAUS: DROPPED {" in line:
            try:
                body = json.loads(line[line.index("{"):])
            except ValueError:
                continue
            while cursor < len(darts):
                d = darts[cursor]
                cursor += 1
                if ("None" if d.get("score") == "MISS" else d.get("score")) == body.get("sector"):
                    d["reference"] = body.get("reference")
                    d["cameras_used"] = body.get("cameras_used") or []
                    break
    return darts


def live_class(d, cameras=3):
    refusedc = d["refused"]
    tip_cams = sum(1 for ok in refusedc.values() if ok)
    if not d["degraded"]:
        lines = cameras - len(refusedc) if d["lines_said"] else len(d.get("cameras_used") or []) or cameras
        return class_of(lines, tip_cams)
    lines = cameras - len(refusedc)
    if d["near_parallel"] or lines >= 2:
        # Two lines that would not cross: no entry today, so the lines are one direction
        # and every tip on the board is new evidence.
        return "L1+T" if tip_cams else "L1"
    return class_of(lines, tip_cams)


def truth_rows(path):
    cols, rows = None, {}
    for line in open(path):
        if line.startswith("# reference"):
            cols = line[2:].split()
        elif line.startswith("#") or not line.strip():
            continue
        elif cols:
            f = line.split()
            if len(f) == len(cols):
                rows[f[0]] = dict(zip(cols, f))
    return rows


def live(pairs):
    pooled = {}
    for log_path, truth_path in pairs:
        truth, day, unjoined = truth_rows(truth_path), {}, 0
        for d in live_darts(log_path):
            row = truth.get(d.get("reference"))
            if row is None:
                unjoined += 1
                continue
            if row["outcome"] != "COUNTED" or row["path"] not in ("geometry", "vote") or row.get("inferred", "0") == "1":
                continue
            cls = live_class(d)
            misread = row["picked"] in ("alternative", "candidate", "typed") and row["corrected"] not in ("-", "None", row["published"])
            other = row["picked"] not in ("-", "published") and not misread
            key = (row["path"], cls)
            cell = day.setdefault(key, [0, 0, 0, 0])
            cell[1 if misread else (2 if other else 0)] += 1
            if misread and row["picked"] in ("alternative", "candidate"):
                cell[3] += 1
            if misread:
                print("I1994 LIVE-WRONG log=%s ref=%s published=%s corrected=%s picked=%s path=%s class=%s agreeing=%s cameras=%s margin_mm=%s sigma_mm=%s" % (
                    os.path.relpath(log_path), row["reference"], row["published"], row["corrected"], row["picked"],
                    row["path"], cls, row["agreeing"], row["cameras"], row["margin_mm"], row["sigma_mm"]))
        for (path, cls), c in sorted(day.items(), key=lambda kv_: (kv_[0][0], ORDER.index(kv_[0][1]))):
            print("I1994 LIVE-CLASS log=%s path=%s class=%s standing=%d corrected=%d corrected_to_an_offered_cell=%d removed_or_turn=%d" % (
                os.path.relpath(log_path), path, cls, c[0], c[1], c[3], c[2]))
            p = pooled.setdefault((path, cls), [0, 0, 0, 0])
            for i in range(4):
                p[i] += c[i]
        print("I1994 LIVE-DAY log=%s joined=%d unjoined=%d" % (os.path.relpath(log_path), sum(sum(c[:3]) for c in day.values()), unjoined))
    for (path, cls), c in sorted(pooled.items(), key=lambda kv_: (kv_[0][0], ORDER.index(kv_[0][1]))):
        print("I1994 LIVE-POOLED path=%s class=%s standing=%d corrected=%d corrected_to_an_offered_cell=%d removed_or_turn=%d reach=%d" % (
            path, cls, c[0], c[1], c[3], c[2], 1 if cls in REACH else 0))
    tot = [sum(c[i] for c in pooled.values()) for i in range(4)]
    rch = [sum(c[i] for (p, k), c in pooled.items() if k in REACH) for i in range(4)]
    print("I1994 BOUND live counted=%d standing=%d corrected=%d reach_corrected=%d reach_corrected_to_an_offered_cell=%d "
          "reach_standing=%d oracle=not-computable (a shipped log carries no camera's own reading)" % (
              tot[0] + tot[1] + tot[2], tot[0], tot[1], rch[1], rch[3], rch[0]))
    return 0


def self_test():
    bad = 0

    def check(name, got, want):
        nonlocal bad
        ok = got == want
        bad += 0 if ok else 1
        print("%s  %s%s" % ("OK  " if ok else "FAIL", name, "" if ok else " -- got %r, want %r" % (got, want)))

    check("class: three lines", class_of(3, 0), "L3")
    check("class: two lines and a spare tip", class_of(2, 1), "L2+T")
    check("class: two lines, no tip", class_of(2, 0), "L2")
    check("class: one line and a tip", class_of(1, 2), "L1+T")
    check("class: no line, two tips", class_of(0, 2), "T2+")
    check("class: a lone tip is never in reach", class_of(0, 1) in REACH, False)
    check("refused: rim only offers no tip",
          refused("(cam 1: no usable axis: rim only: x; cam 3: no usable axis: not straight: y)"), {1: False, 3: True})
    d = {"degraded": True, "near_parallel": False, "lines_said": False,
         "refused": {1: True, 2: False}}
    check("live: one line, one refused camera with a tip", live_class(d), "L1+T")
    d = {"degraded": False, "near_parallel": False, "lines_said": True, "refused": {2: False}}
    check("live: a two-line solve whose third camera is rim-only", live_class(d), "L2")
    d = {"degraded": False, "near_parallel": False, "lines_said": False, "refused": {}, "cameras_used": [1, 2, 3]}
    check("live: a three-line solve", live_class(d), "L3")
    w = {"entry": {"solved": "1"}, "lone": {}, "cams": {
        1: {"usable": "1", "excluded": "0", "tipPlaced": "1", "tipR": "50.0"},
        2: {"usable": "0", "excluded": "0", "tipPlaced": "1", "tipR": "290.0"},
        3: {"usable": "1", "excluded": "0", "tipPlaced": "0", "tipR": "0.0"}}}
    lines, spare, tips, solved = evidence_of(w)
    check("fixture: a tip past the doubles' wire is not on the board", (lines, spare, tips), ([1, 3], [], [1]))
    print("I1994 SELF-TEST %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if a[:1] == ["--self-test"]:
        return self_test()
    if a[:1] == ["--bakeoff"] and len(a) == 2:
        return bakeoff(a[1])
    if a[:1] == ["--live"] and len(a) >= 3 and len(a) % 2 == 1:
        return live(list(zip(a[1::2], a[2::2])))
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

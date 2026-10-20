#!/usr/bin/env python3
"""turnaus#1908: the live half of OD_VOTE_WEIGHT=fit's census.

Joins a board's session log to its Turnaus truth line (the operator API's /truth-line,
one row per published dart with the maintainer's correction) and, for every COUNTED dart
the string vote published, says what the weighting COULD have done to it.

What a shipped log carries, measured on v0.1.18 (2026-10-15..20): the PATH: DEGRADED
sentence names each camera that offered no usable axis and why ("not straight: ... 19.47 px
RMS", "rim only", "not a shaft"), so each camera's weight is recoverable; a camera the
sentence does not name offered a usable line. What it does NOT carry is each camera's own
score string: the vote line says "Consensus score: S3 from 2 cameras" or "No consensus,
using single camera score: S19 from camera 1", and the dissenting camera's reading -- and
whether it voted at all, or read a MISS -- is nowhere in the log (I1628LONE, which has
them, is behind the OD_GEO_SCORE census pin). So the weighted winner's string cannot be
replayed and this census cannot say whether it matches the corrected truth. It counts the
darts on which an overturn is STRUCTURALLY POSSIBLE under weighVote's rule (a dissenter
heavier than the published group by the tie margin, reaching the floor), split by whether
the published dart was right or corrected -- the upper bound on the gain and on the risk.

Weights are score_processing::voteWeightOf's: ok 1.00; not straight gate/rms in
[0.25, 0.90]; rim only 0.25; any other refusal 0.50; ring set and sectors R taken as
complete and 1.000 (every live calibration printed sectors R=1.000).

  i1908_vote_census.py LOG TRUTH [LOG TRUTH ...]
"""
import json
import re
import sys

GATE = 2.5
TIE = 0.10
FLOOR = 0.50
ANSI = re.compile(r"\x1b\[[0-9;]*m")
CAM = re.compile(r"cam (\d): ")


def weight_of(reason, rms):
    if reason == "ok":
        return 1.0
    if reason == "rim-only":
        return 0.25
    if "not-straight" in reason and rms > 0:
        return min(0.90, max(0.25, GATE / rms))
    return 0.5


def reasons_of(path_line, cameras=3):
    """Each camera's (reason, rms) from a PATH: DEGRADED sentence."""
    out = {c: ("ok", 0.0) for c in range(1, cameras + 1)}
    starts = [(m.start(), int(m.group(1))) for m in CAM.finditer(path_line)]
    for k, (pos, cam) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(path_line)
        seg = path_line[pos:end]
        if "no usable axis" not in seg and "no frame" not in seg:
            continue
        words = []
        if "rim only" in seg:
            words = ["rim-only"]
        else:
            if "not a shaft" in seg:
                words.append("not-a-shaft")
            if "not straight" in seg:
                words.append("not-straight")
            if "direction unusable" in seg:
                words.append("direction-unusable")
            if "no fresh figure" in seg or "no frame" in seg:
                words.append("no-figure")
        reason = "+".join(words) if words else "other"
        m = re.search(r"scatters ([0-9.]+) px", seg)
        out[cam] = (reason, float(m.group(1)) if m else 0.0)
    return out


def may_vote(reason):
    # rim-only and no-figure cameras offer no tip (#1689), so they cannot vote.
    return reason not in ("rim-only", "no-figure")


def overturn_possible(weights, reasons, published, agreeing):
    """Whether weighVote could replace the majority's string, given only the weights."""
    voters = [c for c in weights if may_vote(reasons[c][0])]
    if agreeing >= 3 or published not in weights:
        return False
    if agreeing == 1:
        return any(c != published and weights[c] - weights[published] >= TIE and weights[c] >= FLOOR
                   for c in voters)
    others = [c for c in voters if c != published]
    for q in others:  # q is the published camera's partner, r the dissenter
        for r in others:
            if r != q and weights[r] - (weights[published] + weights[q]) >= TIE and weights[r] >= FLOOR:
                return True
    return False


def darts_of(log_path):
    """The log's scored darts in order, each with its path, reasons and vote line."""
    darts, pending, cursor = [], None, 0
    for raw in open(log_path, errors="replace"):
        line = ANSI.sub("", raw)
        if "SCORE_PROCESSING] - PATH:" in line:
            pending = {"path_line": line, "degraded": "PATH: DEGRADED" in line, "agreeing": 0, "camera": -1}
        elif pending is not None and "Consensus score:" in line:
            m = re.search(r"Consensus score: (\S+) from (\d+) cameras", line)
            pending["agreeing"] = int(m.group(2))
        elif pending is not None and "No consensus, using single camera score:" in line:
            m = re.search(r"single camera score: (\S+) from camera (\d+)", line)
            pending["agreeing"] = 1
            pending["camera"] = int(m.group(2))
        elif "[SCORER] - SCORE:" in line:
            m = re.search(r"SCORE: (\S+) \|.*Camera: (-?\d+)", line)
            if m and m.group(1) != "END" and pending is not None:
                pending["score"] = m.group(1)
                if pending["camera"] < 0:
                    pending["camera"] = int(m.group(2))
                darts.append(pending)
            pending = None
        elif "TURNAUS: COUNTED {" in line or "TURNAUS: DROPPED {" in line:
            # In order: each post is the oldest scored dart not yet posted. Turnaus names a
            # MISS "None"; a dart whose score disagrees with the post's is skipped unjoined.
            body = json.loads(line[line.index("{"):])
            while cursor < len(darts):
                d = darts[cursor]
                cursor += 1
                if ("None" if d.get("score") == "MISS" else d.get("score")) == body.get("sector"):
                    d["reference"] = body.get("reference")
                    break
    return darts


def truth_of(path):
    rows = {}
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        f = line.split()
        if len(f) < 21:
            continue
        rows[f[0]] = {"published": f[1], "corrected": f[3], "path": f[5], "outcome": f[20]}
    return rows


def census(pairs):
    total = {"vote": 0, "right": 0, "wrong": 0, "possible_right": 0, "possible_wrong": 0, "unjoined": 0}
    for log_path, truth_path in pairs:
        truth = truth_of(truth_path)
        darts = darts_of(log_path)
        day = {k: 0 for k in total}
        for d in darts:
            if not d["degraded"]:
                continue
            row = truth.get(d.get("reference"))
            if row is None:
                day["unjoined"] += 1
                continue
            if row["outcome"] != "COUNTED" or row["path"] != "vote":
                continue
            reasons = reasons_of(d["path_line"])
            weights = {c: weight_of(*reasons[c]) for c in reasons}
            published = d["camera"] + 1
            wrong = row["corrected"] not in ("-", row["published"])
            possible = overturn_possible(weights, reasons, published, d["agreeing"])
            day["vote"] += 1
            day["wrong" if wrong else "right"] += 1
            if possible:
                day["possible_wrong" if wrong else "possible_right"] += 1
            if wrong or possible:
                print("I1908LIVE ref=%s published=%s corrected=%s agreeing=%d cam=%d possible=%d weights=%s" % (
                    d.get("reference"), row["published"], row["corrected"], d["agreeing"], published,
                    1 if possible else 0,
                    ",".join("cam%d=%s/%.2f" % (c, reasons[c][0], weights[c]) for c in sorted(weights))))
        print("I1908LIVE-DAY log=%s vote_counted=%d right=%d corrected=%d overturn_possible_on_right=%d "
              "overturn_possible_on_corrected=%d unjoined_degraded=%d" % (
                  log_path, day["vote"], day["right"], day["wrong"], day["possible_right"],
                  day["possible_wrong"], day["unjoined"]))
        for k in total:
            total[k] += day[k]
    print("I1908LIVE-POOLED vote_counted=%d right=%d corrected=%d overturn_possible_on_right=%d "
          "overturn_possible_on_corrected=%d unjoined_degraded=%d" % (
              total["vote"], total["right"], total["wrong"], total["possible_right"], total["possible_wrong"],
              total["unjoined"]))


def self_test():
    line = ("PATH: DEGRADED -- no geometric entry (TOO-FEW-CONSTRAINTS: no entry: 1 usable constraint(s) of 3 "
            "cameras, and one line is anywhere along itself (cam 1: no usable axis: not straight: the kept "
            "centreline scatters 5.00 px RMS about the line; cam 2: no usable axis: rim only: this camera's "
            "fresh change clears the floor))")
    r = reasons_of(line)
    ok = r[1] == ("not-straight", 5.0) and r[2][0] == "rim-only" and r[3] == ("ok", 0.0)
    w = {c: weight_of(*r[c]) for c in r}
    ok = ok and abs(w[1] - 0.5) < 1e-9 and w[2] == 0.25 and w[3] == 1.0
    # cam1 (0.50) published alone; cam3 (1.00) could overturn it; a 2-camera group of 1.00+0.50
    # cannot be outweighed by anything.
    ok = ok and overturn_possible(w, r, 1, 1) and not overturn_possible(w, r, 3, 2)
    print("I1908LIVE self-test", "OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    args = sys.argv[1:]
    if not args or len(args) % 2:
        sys.exit(__doc__)
    census(list(zip(args[0::2], args[1::2])))

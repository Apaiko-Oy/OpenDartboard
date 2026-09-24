"""#1510 Phase 2: the paint-containment census -- the model's answer beside the
published one, per camera, per dart, judged against the fixture's ground truth.

Reads one detector run made with OD_MODEL_SCORE=on and prints, for every dart, what
each camera's existing per-ring machinery said (the `existing=` half of the I1510P2
shadow line score_processing prints), what the fitted board model said about the SAME
tip pixel (`model=`), and what was actually thrown (the ground-truth table). This is
the census the issue demands BEFORE any wiring: its numbers -- not an argument -- say
whether generating the published score from the model is safe.

    python3 i1510p2_census.py --log <run.txt> --truth <table.md> --fixture <name>

A reporter in i1484_confidence_census.py's mould: it decides nothing about the
numbers. Exit status is about whether the run could be READ: 0 when it was parsed and
held at least one shadow line, 2 when there was nothing to census -- which a caller
must not mistake for "the model agreed".

Alignment is the ground-truth file's own rule: per visit, k-th detected against k-th
thrown, never a global sequence. A visit where the detector found fewer darts than
were thrown is a detection failure, not a scoring one, and is counted apart.
"""

import argparse
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
SCORE_RE = re.compile(
    r"SCORE:\s+(\S+)\s+\|\s+Position:\s+\((-?\d+),(-?\d+)\)\s+\|\s+"
    r"Confidence:\s+([0-9.]+)\s+\|\s+Camera:\s+(-?\d+)"
)
MODEL_RE = re.compile(
    r"I1510P2 MODEL cam=(\d+) tip=\(([-0-9.]+),([-0-9.]+)\) existing=(\S+) model=(\S+) "
    r"agree=(\d) mm=\(([-0-9.]+),([-0-9.]+)\) r=([-0-9.]+) ringB=([-0-9.]+) "
    r"wedgeB=([-0-9.]+) boundary=([-0-9.]+) fit=(\S+) R=([-0-9.]+) margin=([-+0-9.]+) anchor=(\S+)"
)
THROW_RE = re.compile(r"^(?:\*{1,2})?(miss|BULL|DBULL|[TDS]?\d{1,2})(?:\*{1,2})?$", re.IGNORECASE)


def normalise(token):
    """A ground-truth token in the published vocabulary: 8 -> S8, t9 -> T9, miss -> MISS."""
    t = token.strip().upper()
    if t == "MISS":
        return "MISS"
    if t in ("BULL", "DBULL"):
        return "BULL"
    if t and t[0] in "SDT":
        return t
    return "S" + t


def read_truth(path):
    """The first | visit | table in the file, one list of normalised throws per visit."""
    visits = []
    in_table = False
    for raw in open(path, "r", errors="replace"):
        cells = [c.strip() for c in raw.strip().strip("|").split("|")] if "|" in raw else []
        if len(cells) >= 4 and cells[0].lower() == "visit":
            in_table = True
            continue
        if in_table:
            if len(cells) < 4 or not cells[0].isdigit():
                if visits:
                    break
                continue
            throws = []
            for cell in cells[1:4]:
                m = THROW_RE.match(cell)
                if m:
                    throws.append(normalise(m.group(1)))
            visits.append(throws)
    return visits


class Dart(object):
    def __init__(self, score, confidence, camera):
        self.score = score
        self.confidence = confidence
        self.camera = camera  # 0-based, -1 for a MISS nobody voted on
        self.cams = {}        # camera (1-based) -> dict of the shadow line's fields


def read_run(path):
    visits = [[]]
    pending = {}
    shadow_lines = 0
    for raw in open(path, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = MODEL_RE.search(line)
        if m:
            shadow_lines += 1
            pending[int(m.group(1))] = {
                "tip": (float(m.group(2)), float(m.group(3))),
                "existing": m.group(4),
                "model": m.group(5),
                "agree": m.group(6) == "1",
                "mm": (float(m.group(7)), float(m.group(8))),
                "r": float(m.group(9)),
                "ringB": float(m.group(10)),
                "wedgeB": float(m.group(11)),
                "boundary": float(m.group(12)),
                "fit": m.group(13),
                "R": float(m.group(14)),
                "margin": float(m.group(15)),
                "anchor": m.group(16),
            }
            continue
        m = SCORE_RE.search(line)
        if m:
            score = m.group(1)
            if score == "END":
                pending = {}
                if visits[-1]:
                    visits.append([])
                continue
            dart = Dart(score, float(m.group(4)), int(m.group(5)))
            dart.cams = pending
            pending = {}
            visits[-1].append(dart)
    if visits and not visits[-1]:
        visits.pop()
    return visits, shadow_lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--truth", required=True)
    ap.add_argument("--fixture", required=True)
    args = ap.parse_args()

    truth = read_truth(args.truth)
    visits, shadow_lines = read_run(args.log)
    if shadow_lines == 0:
        print("I1510P2 CENSUS %s: no I1510P2 shadow lines in %s -- was the run made "
              "with OD_MODEL_SCORE=on?" % (args.fixture, args.log))
        return 2
    if not visits:
        print("I1510P2 CENSUS %s: no darts in %s" % (args.fixture, args.log))
        return 2

    print("I1510P2 CENSUS fixture=%s visits=%d truth_visits=%d shadow_lines=%d"
          % (args.fixture, len(visits), len(truth), shadow_lines))

    # ---- every dart, spelled out --------------------------------------------------------
    per_cam = {}   # cam -> counters over every shadow line of a published dart
    pub = {"darts": 0, "existing_right": 0, "model_right": 0, "both_right": 0,
           "model_rescues": [], "model_breaks": [], "unjudged": 0}
    agree_boundaries, disagree_boundaries = [], []
    for v, darts in enumerate(visits):
        thrown = truth[v] if v < len(truth) else []
        for k, dart in enumerate(darts):
            want = thrown[k] if k < len(thrown) else None
            win = dart.cams.get(dart.camera + 1) if dart.camera >= 0 else None
            model_win = win["model"] if win else None
            parts = []
            for cam in sorted(dart.cams):
                c = dart.cams[cam]
                stat = per_cam.setdefault(cam, {"seen": 0, "same": 0, "ring_same_wedge_diff": 0,
                                                "ring_diff": 0, "no_model": 0})
                stat["seen"] += 1
                if c["model"] in ("NONE",) or c["model"].endswith("?"):
                    stat["no_model"] += 1
                elif c["model"] == c["existing"]:
                    stat["same"] += 1
                    agree_boundaries.append(c["boundary"])
                else:
                    disagree_boundaries.append(c["boundary"])
                    same_ring = c["model"][:1] == c["existing"][:1] and c["existing"][:1] in "SDT"
                    stat["ring_same_wedge_diff" if same_ring else "ring_diff"] += 1
                parts.append("cam%d ex=%s model=%s b=%.1f%s" % (
                    cam, c["existing"], c["model"], c["boundary"],
                    "" if c["fit"] == "ACCEPTED" else " FIT-" + c["fit"]))
            print("I1510P2 DART v%d.%d published=%s@%.1f cam%s truth=%s | %s" % (
                v + 1, k + 1, dart.score, dart.confidence,
                dart.camera + 1 if dart.camera >= 0 else "-", want or "?",
                " | ".join(parts) if parts else "no shadow lines"))
            if want is None:
                pub["unjudged"] += 1
                continue
            pub["darts"] += 1
            ex_right = dart.score == want
            mo_right = model_win == want
            pub["existing_right"] += ex_right
            pub["model_right"] += mo_right
            pub["both_right"] += ex_right and mo_right
            if mo_right and not ex_right:
                pub["model_rescues"].append("v%d.%d %s->%s" % (v + 1, k + 1, dart.score, model_win))
            if ex_right and not mo_right:
                pub["model_breaks"].append("v%d.%d %s->%s" % (v + 1, k + 1, dart.score, model_win))

    # ---- the summary the wiring decision reads ------------------------------------------
    for cam in sorted(per_cam):
        s = per_cam[cam]
        print("I1510P2 CAM cam=%d seen=%d model_eq_existing=%d wedge_diff=%d ring_diff=%d no_model=%d"
              % (cam, s["seen"], s["same"], s["ring_same_wedge_diff"], s["ring_diff"], s["no_model"]))

    def med(xs):
        return sorted(xs)[len(xs) // 2] if xs else float("nan")

    print("I1510P2 BOUNDARY agreements n=%d median=%.1f min=%.1f | disagreements n=%d median=%.1f min=%.1f"
          % (len(agree_boundaries), med(agree_boundaries),
             min(agree_boundaries) if agree_boundaries else float("nan"),
             len(disagree_boundaries), med(disagree_boundaries),
             min(disagree_boundaries) if disagree_boundaries else float("nan")))
    print("I1510P2 TRUTH darts=%d existing_right=%d model_right=%d both=%d unjudged=%d"
          % (pub["darts"], pub["existing_right"], pub["model_right"], pub["both_right"],
             pub["unjudged"]))
    if pub["model_rescues"]:
        print("I1510P2 TRUTH model right where published wrong: " + ", ".join(pub["model_rescues"]))
    if pub["model_breaks"]:
        print("I1510P2 TRUTH published right where model wrong: " + ", ".join(pub["model_breaks"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

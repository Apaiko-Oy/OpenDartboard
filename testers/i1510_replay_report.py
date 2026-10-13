"""Report the selected fixture, never silently align against an older recording.

Requires OD_CAPSEAM=1 and an EOF replay. Visit membership is determined by source
time, not score strings or the detector's END count. Incomplete/extra-dart visits
are explicitly unaligned: this reporter does not guess which throw was missing.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

ANSI = re.compile(r"\x1b\[[0-9;]*m")
POSITION = re.compile(r"CAPSEAM .*?pos_ms=\[([^]]+)\]")
SCORE = re.compile(r"SCORE:\s+(\S+)\s+\|\s+Position:")


def read_events(text):
    events, position, eof = [], None, False
    for raw in text.splitlines():
        line = ANSI.sub("", raw)
        found = POSITION.search(line)
        if found:
            # The manifest's visual windows use camera 1's original timebase.
            first = found[1].split(",")[0]
            next_position = float(first) if first != "-" else None
            if next_position is not None:
                if not math.isfinite(next_position) or next_position < 0:
                    raise ValueError("invalid camera-1 source time")
                if position is not None and next_position < position:
                    raise ValueError("source time went backwards; use a fresh log for each replay")
            position = next_position
        found = SCORE.search(line)
        if found and found[1] != "END":
            if eof:
                raise ValueError("score after EOF; use a fresh log for each replay")
            if position is None:
                raise ValueError("score has no camera-1 source time; replay with OD_CAPSEAM=1")
            events.append({"time_ms": position, "score": found[1]})
        eof |= "END OF FOOTAGE: every file source has reached its end" in line
    if not eof:
        raise ValueError("replay did not reach EOF; no full-recording accuracy claim is possible")
    return events


def summarize(truth, events):
    first = truth["first_complete_visit"]
    expected_visits = truth["visits"][first - 1:]
    windows = truth["visit_windows_ms"]
    if len(windows) != len(expected_visits):
        raise ValueError("each covered visit needs an independent source-time window")
    previous = -1
    for start, end in windows:
        if start < previous or start >= end:
            raise ValueError("visit windows must be ordered, non-overlapping and nonempty")
        previous = end
    if "throw_dividers_ms" in truth:
        return summarize_throws(truth, events, expected_visits, windows)
    rows, used, correct, unaligned = [], set(), 0, []
    for visit, (window, expected) in enumerate(zip(windows, expected_visits), first):
        indexes = [i for i, e in enumerate(events) if window[0] <= e["time_ms"] < window[1]]
        used.update(indexes)
        observed = [events[i]["score"] for i in indexes]
        aligned = len(observed) == len(expected)
        right = sum(a == b for a, b in zip(expected, observed)) if aligned else None
        if aligned:
            correct += right
        else:
            unaligned.append(visit)
        rows.append({"visit": visit, "expected": expected, "observed": observed,
                     "times_ms": [events[i]["time_ms"] for i in indexes],
                     "correct": right, "alignment": "ordered within time window" if aligned else
                     "requires independent throw-level alignment; no padding or score-based matching"})
    outside = [e for i, e in enumerate(events) if i not in used]
    return {"fixture": truth["fixture"], "first_complete_visit": first,
            "expected_darts": sum(map(len, expected_visits)), "detected_in_coverage": len(used),
            "verified_correct_in_aligned_visits": correct,
            "correct_total": correct if not unaligned and not outside else None,
            "alignment_note": "Equal-count visits use temporal order, not independent per-throw labels; inspect for compensating missed/false events.",
            "unaligned_visits": unaligned, "outside_coverage": outside, "visits": rows}


def summarize_throws(truth, events, expected_visits, windows):
    dividers = truth["throw_dividers_ms"]
    if len(dividers) != len(windows):
        raise ValueError("each covered visit needs independently reviewed throw dividers")
    if any(a["time_ms"] > b["time_ms"] for a, b in zip(events, events[1:])):
        raise ValueError("events must be in publication order")
    rows, used = [], set()
    correct = clean = missing = wrong = extra = 0
    for visit, (window, cuts, expected) in enumerate(zip(windows, dividers, expected_visits),
                                                   truth["first_complete_visit"]):
        bounds = [window[0], *cuts, window[1]]
        if len(bounds) != len(expected) + 1 or any(a >= b for a, b in zip(bounds, bounds[1:])):
            raise ValueError("throw dividers must be ordered and inside the visit")
        darts = []
        for throw, (start, end, target) in enumerate(zip(bounds, bounds[1:], expected), 1):
            indexes = [i for i, e in enumerate(events) if start <= e["time_ms"] < end]
            used.update(indexes)
            observed = [events[i] for i in indexes]
            first = observed[0]["score"] if observed else None
            right = first == target
            correct += right
            clean += right and len(observed) == 1
            missing += not observed
            wrong += bool(observed) and not right
            extra += max(0, len(observed) - 1)
            darts.append({"throw": throw, "expected": target, "first_score": first,
                          "correct": right, "clean_correct": right and len(observed) == 1,
                          "events": observed})
        rows.append({"visit": visit, "darts": darts})
    outside = [e for i, e in enumerate(events) if i not in used]
    return {"fixture": truth["fixture"], "expected_darts": sum(map(len, expected_visits)),
            "published_events": len(events), "correct_first_scores": correct,
            "clean_correct_darts": clean, "wrong_first_scores": wrong,
            "missed_darts": missing, "extra_events": extra,
            "correct_total": correct if not outside else None,
            "alignment_note": truth.get("throw_window_source", "independent throw windows"),
            "outside_coverage": outside, "visits": rows}


def verify_sources(truth, directory):
    for camera in truth["cameras"]:
        path = Path(directory) / camera["file"]
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != camera["sha256"]:
            raise ValueError(f"fixture mismatch: {path}; refusing to use this ground truth")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--truth", required=True, type=Path)
    parser.add_argument("--sources", required=True, type=Path, help="original clips, not frozen-start derivatives")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    truth = json.loads(args.truth.read_text(encoding="utf-8"))
    verify_sources(truth, args.sources)
    if args.log:
        report = summarize(truth, read_events(args.log.read_text(encoding="utf-8", errors="replace")))
        print(json.dumps(report, indent=2))
    else:
        print(f"Verified {truth['fixture']}: {len(truth['cameras'])} source hashes match")


if __name__ == "__main__":
    main()

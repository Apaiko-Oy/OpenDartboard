#!/usr/bin/env python3
"""Capture-skew instrument for OpenDartboard (issue #813).

One command. It runs the detector for a stated length against stated inputs,
records every capture cycle, and prints a fixed-shape report so two runs on two
platforms can be put side by side without transforming either.

    python3 tools/skew/measure_skew.py --minutes 10 --cams /dev/video0,/dev/video2,/dev/video4
    python3 tools/skew/measure_skew.py --cycles 1100 --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4
    python3 tools/skew/measure_skew.py --reduce-only skew-runs/<run>/cycles.csv

Requires the instrumented build (OD_SKEW_LOG in src/utils/camera.hpp).
Python 3.8+, standard library only.
"""

import argparse
import datetime
import json
import os
import platform
import subprocess
import sys
import time

REPORT_VERSION = "capture-skew report v1"


# ---------------------------------------------------------------- host load

def read_proc_stat():
    """(busy_jiffies, total_jiffies) for the whole host, or None off Linux."""
    try:
        with open("/proc/stat") as handle:
            fields = handle.readline().split()
    except OSError:
        return None
    if not fields or fields[0] != "cpu":
        return None
    values = [int(v) for v in fields[1:]]
    idle = values[3] + (values[4] if len(values) > 4 else 0)
    total = sum(values)
    return total - idle, total


def busy_percent(before, after):
    if not before or not after:
        return None
    busy = after[0] - before[0]
    total = after[1] - before[1]
    if total <= 0:
        return None
    return 100.0 * busy / total


# ---------------------------------------------------------------- statistics

def quantile(sorted_values, q):
    """Nearest-rank quantile. No interpolation, so the figure is a real sample."""
    if not sorted_values:
        return None
    rank = max(1, min(len(sorted_values), int(round(q * len(sorted_values) + 0.5))))
    return sorted_values[rank - 1]


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "min": ordered[0],
        "median": quantile(ordered, 0.50),
        "p99": quantile(ordered, 0.99),
        "max": ordered[-1],
    }


# ---------------------------------------------------------------- the record

class Cycle:
    __slots__ = ("index", "cameras", "valid", "clock", "pos_ms", "anchor_us", "ret_us")


def parse_field(text, cast):
    out = []
    for part in text.split("|"):
        try:
            out.append(cast(part))
        except ValueError:
            out.append(None)
    return out


def load(path):
    cycles = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("cycle,"):
                continue
            parts = line.split(",")
            if len(parts) != 7:
                continue
            record = Cycle()
            record.index = int(parts[0])
            record.cameras = int(parts[1])
            record.valid = int(parts[2])
            record.clock = parts[3]
            record.pos_ms = parse_field(parts[4], float)
            record.anchor_us = parse_field(parts[5], int)
            record.ret_us = parse_field(parts[6], int)
            cycles.append(record)
    return cycles


def reduce_cycles(cycles, fps):
    """Three figures, kept apart on purpose, plus the cross-check."""
    if not cycles:
        return None

    clock = cycles[0].clock
    cameras = cycles[0].cameras

    loop_first = None
    loop_last = None
    read_spreads = []
    skew_raw = []
    skew_anchored = []
    short_cycles = 0
    per_camera_short = [0] * cameras

    for record in cycles:
        if record.valid < record.cameras:
            short_cycles += 1
            for index in range(record.cameras):
                if record.ret_us[index] is None or record.ret_us[index] < 0:
                    if index < len(per_camera_short):
                        per_camera_short[index] += 1

        returns = [v for v in record.ret_us if v is not None and v >= 0]
        if returns:
            if loop_first is None:
                loop_first = max(returns)
            loop_last = max(returns)
        if len(returns) >= 2:
            read_spreads.append((max(returns) - min(returns)) / 1000.0)

        instants = []
        anchored = []
        for index in range(record.cameras):
            pos = record.pos_ms[index]
            anchor = record.anchor_us[index]
            if pos is None or pos < 0 or anchor is None or anchor < 0:
                continue
            instants.append(pos * 1000.0)
            anchored.append(pos * 1000.0 + anchor)
        if len(instants) >= 2:
            skew_raw.append((max(instants) - min(instants)) / 1000.0)
            skew_anchored.append((max(anchored) - min(anchored)) / 1000.0)

    return {
        "clock": clock,
        "cameras": cameras,
        "cycles": len(cycles),
        "read_return_spread_ms": distribution(read_spreads),
        "capture_skew_ms": distribution(skew_raw),
        "capture_skew_anchored_ms": distribution(skew_anchored),
        "short_cycles": short_cycles,
        "short_cycles_by_camera": per_camera_short,
        "loop_ms_per_cycle": (
            (loop_last - loop_first) / 1000.0 / (len(cycles) - 1)
            if loop_first is not None and len(cycles) > 1 else None),
        "floor_ms": {"typical": 500.0 / fps, "worst": 1000.0 / fps, "assumed_fps": fps},
    }


# ---------------------------------------------------------------- the report

CLOCK_NOTES = {
    "host": (
        "host monotonic (V4L2) — the three values share one clock, so their\n"
        "                 spread IS the capture skew"
    ),
    "source": (
        "source-relative (Media Foundation) — each stream's own clock, so the\n"
        "                 spread is only meaningful after each camera's host anchor is added"
    ),
    "stream": (
        "position in a video file — NOT A CLOCK. A file has no acquisition\n"
        "                 instant, so capture skew cannot be measured from it at all"
    ),
}


def fmt(distribution_or_none, unit="ms"):
    if not distribution_or_none:
        return "no samples"
    return "median {median:8.3f} {unit}   p99 {p99:8.3f} {unit}   max {max:8.3f} {unit}   min {min:8.3f} {unit}   n={n}".format(
        unit=unit, **distribution_or_none
    )


def report(summary, meta):
    lines = []
    add = lines.append
    add("=" * 78)
    add("OpenDartboard " + REPORT_VERSION)
    add("=" * 78)
    add("run started      : {}".format(meta["started"]))
    add("platform         : {} {}".format(meta["platform"], meta["machine"]))
    add("input            : {}".format(meta["cams"]))
    add("sources          : {} camera(s)".format(summary["cameras"]))
    add("acquisition clock: {}".format(summary["clock"]))
    add("                 = {}".format(CLOCK_NOTES.get(summary["clock"], "unknown backend")))
    add("run length       : {} cycles recorded, {:.1f} s wall".format(summary["cycles"], meta["wall_s"]))
    add("cost per cycle   : {} (capture loop only, startup and calibration excluded)".format(
        "{:.1f} ms".format(summary["loop_ms_per_cycle"]) if summary["loop_ms_per_cycle"] else "n/a"))
    add("                   {:.1f} ms including startup and calibration".format(
        1000.0 * meta["wall_s"] / max(1, summary["cycles"])))
    if meta.get("host_busy_pct") is None:
        add("host busy        : NOT MEASURED on this platform — state by hand how loaded the box was")
    else:
        add("host busy        : {:.1f}% mean over the run, {} logical CPUs".format(
            meta["host_busy_pct"], meta["cpus"]))
    add("")

    add("-" * 78)
    add("FIGURE 1 — CAPTURE SKEW")
    add("  newest minus oldest ACQUISITION instant within one capture cycle.")
    add("  This is the figure triangulation rests on. It is what #804 and #806 compare.")
    if summary["clock"] == "stream":
        add("")
        add("  NOT AVAILABLE — the inputs are video files. CAP_PROP_POS_MSEC on a file is")
        add("  a position in that stream, not an instant a camera captured anything, so")
        add("  there is no skew here to measure. What this run does prove is the harness:")
        add("  Figure 2 below is measured end to end with no camera attached.")
        add("")
        add("  (For the record, the spread of stream positions was {}.".format(
            fmt(summary["capture_skew_ms"])))
        add("   On this project's mock footage that number is the DEBUG_SEEK_VIDEO")
        add("   compile-time seek of 3 - i*0.18 s and is constant. It is not skew.)")
    else:
        add("  " + fmt(summary["capture_skew_ms"]))
        add("")
        add("  cross-check, each camera's instant re-based on its own host anchor:")
        add("  " + fmt(summary["capture_skew_anchored_ms"]))
        if summary["clock"] == "source":
            add("  On Media Foundation the cross-check line IS the figure; the line above it")
            add("  is three unrelated clocks subtracted from each other and means nothing.")
        else:
            add("  On V4L2 the two lines should agree. If they do not, the assumption that")
            add("  POS_MSEC is one host clock shared by all three devices is wrong here,")
            add("  and no skew figure from this box should be published until that is settled.")
    add("")
    floor = summary["floor_ms"]
    add("  FLOOR — three free-running USB cameras have no sync signal, so their frame")
    add("  phases are independent. At {:.0f} fps a perfect capture path still averages".format(floor["assumed_fps"]))
    add("  about {:.1f} ms apart at the extremes and reaches {:.1f} ms.".format(floor["typical"], floor["worst"]))
    add("  A reading at the floor is not a fault. The tail beyond it is the question.")
    add("")

    add("-" * 78)
    add("FIGURE 2 — READ-RETURN SPREAD")
    add("  how far apart the three read() calls RETURN, because captureFrames reads the")
    add("  cameras in a serial loop. This is DECODE AND TRANSFER COST, NOT SKEW: a")
    add("  frame's capture instant does not move because you decoded it late.")
    add("  Never put this figure beside another run's Figure 1.")
    add("  " + fmt(summary["read_return_spread_ms"]))
    add("")

    add("-" * 78)
    add("FIGURE 3 — SHORT CYCLES")
    add("  capture cycles that returned fewer frames than there are cameras.")
    add("  {} of {} ({:.2f}%)".format(
        summary["short_cycles"], summary["cycles"],
        100.0 * summary["short_cycles"] / max(1, summary["cycles"])))
    add("  by camera slot: {}".format(
        ", ".join("cam{}={}".format(i + 1, n) for i, n in enumerate(summary["short_cycles_by_camera"]))))
    add("  A non-zero count here outranks both figures above. Read it first.")
    add("")
    add("-" * 78)
    add("raw per-cycle record: {}".format(meta["csv"]))
    add("machine-readable    : {}".format(meta["json"]))
    add("=" * 78)
    return "\n".join(lines)


# ---------------------------------------------------------------- the command

def main():
    parser = argparse.ArgumentParser(description="Measure OpenDartboard capture skew and read-return spread.")
    parser.add_argument("--cams", help="comma-separated camera devices or video files")
    parser.add_argument("--minutes", type=float, help="run length in minutes")
    parser.add_argument("--cycles", type=int, help="run length in capture cycles")
    parser.add_argument("--binary", default=None, help="path to the instrumented opendartboard")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=float, default=30.0, help="camera frame rate, for the floor")
    parser.add_argument("--out", default=None, help="output directory")
    parser.add_argument("--no-record", action="store_true",
                        help="run the same length but write no per-cycle output (the control for recording cost)")
    parser.add_argument("--reduce-only", help="re-reduce an existing cycles.csv and exit")
    parser.add_argument("--label", default="", help="free text recorded with the run")
    arguments, passthrough = parser.parse_known_args()

    if arguments.reduce_only:
        cycles = load(arguments.reduce_only)
        summary = reduce_cycles(cycles, arguments.fps)
        meta = {"started": "(re-reduced)", "platform": platform.system(), "machine": platform.machine(),
                "cams": "(from file)", "wall_s": 0.0, "host_busy_pct": None, "cpus": os.cpu_count(),
                "csv": arguments.reduce_only, "json": "(not written)"}
        print(report(summary, meta))
        return 0

    if not arguments.cams:
        parser.error("--cams is required")
    if not arguments.minutes and not arguments.cycles:
        parser.error("give a run length: --minutes or --cycles")

    stamp = datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
    out_dir = arguments.out or os.path.join("skew-runs", stamp)
    os.makedirs(out_dir, exist_ok=True)
    csv_path = os.path.join(out_dir, "cycles.csv")
    json_path = os.path.join(out_dir, "summary.json")

    binary = arguments.binary
    if not binary:
        binary = "build\\opendartboard.exe" if os.name == "nt" else "./build/opendartboard"

    command = [binary, "--debug", "--cams", arguments.cams,
               "--width", str(arguments.width), "--height", str(arguments.height)] + passthrough

    environment = dict(os.environ)
    if not arguments.no_record:
        environment["OD_SKEW_LOG"] = os.path.abspath(csv_path)
    if arguments.cycles:
        environment["OD_SKEW_CYCLES"] = str(arguments.cycles)

    print("running: {}".format(" ".join(command)), file=sys.stderr)
    before = read_proc_stat()
    started = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
    begin = time.time()
    process = subprocess.Popen(command, stdout=open(os.path.join(out_dir, "run.log"), "w"),
                               stderr=subprocess.STDOUT, env=environment)
    timeout = arguments.minutes * 60.0 if arguments.minutes else None
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.send_signal(2)  # SIGINT, the program's own stop path
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
    wall = time.time() - begin
    after = read_proc_stat()

    if arguments.no_record:
        print("control run, no per-cycle output written. wall {:.1f} s, log {}".format(
            wall, os.path.join(out_dir, "run.log")))
        return 0

    cycles = load(csv_path)
    summary = reduce_cycles(cycles, arguments.fps)
    if not summary:
        print("no capture cycles were recorded — is this the instrumented build?", file=sys.stderr)
        return 1

    meta = {"started": started, "platform": platform.system(), "machine": platform.machine(),
            "cams": arguments.cams, "wall_s": wall, "host_busy_pct": busy_percent(before, after),
            "cpus": os.cpu_count(), "csv": csv_path, "json": json_path, "label": arguments.label}

    with open(json_path, "w") as handle:
        json.dump({"report_version": REPORT_VERSION, "meta": meta, "figures": summary}, handle, indent=2)

    text = report(summary, meta)
    print(text)
    with open(os.path.join(out_dir, "report.txt"), "w") as handle:
        handle.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# unrun-tester: reads the timing lines of a replay run with OD_CYCLE_COST=on, which are load-dependent by design and so never a gate; run by hand beside testers/i1683_realtime.sh.
"""#1686: where a detection cycle's time goes.

    python3 testers/i1686_cost.py <out.txt> [<out.txt> ...]

Reads the `I1686COST` lines a replay prints under OD_CYCLE_COST=on and prints, per kind
of cycle, the median and 90th percentile of every stage in milliseconds:

  idle     the motion state machine ended the cycle IDLE or in COOLDOWN, with no window
  event    SPIKE / STABILIZING / END, with no window (a dart is landing and settling)
  window   a dart window collected a frame on this cycle
  close    the dart window closed and was decided on this cycle
  all      every cycle

Under OD_CYCLE_COST=cpu every figure is the CPU time of the thread that did the work
rather than wall time, so a stage is not charged for time the quota or the decoders took.
`loop` is the scorer loop's time from the start of the read to the end of process();
`read` is capture->read() (the decode, on the capture clock; under the real-time replay the
wait for a frame, the decode being on the feed threads and shown as `decode_bg`, the
milliseconds of decoding those threads did since the previous line, all three cameras).
`proc_cpu` is the whole process's CPU time between two lines, every thread included.
`process` is detector->process(); the m_*, d_* and score columns are inside it.
With several files the cycles are pooled.
"""
import re
import sys

STAGES = ["loop", "read", "process", "m_gray", "m_blur", "m_diff", "m_morph", "m_region", "m_clone",
          "m_state", "d_accum", "d_close", "d_cumul", "d_fresh", "d_axis", "score", "decode_bg", "proc_cpu"]


def kind(state, window):
    if window == "close":
        return "close"
    if window == "collect":
        return "window"
    if state in ("IDLE", "COOLDOWN"):
        return "idle"
    return "event"


def pct(xs, q):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    i = min(len(xs) - 1, max(0, int(round(q * (len(xs) - 1)))))
    return xs[i]


def main(paths):
    rows = {}
    line_re = re.compile(r"I1686COST (?:clock=\w+ )?cycle=(\d+) state=(\S+) window=(\S+)(.*)")
    for p in paths:
        with open(p, errors="replace") as f:
            for line in f:
                m = line_re.search(line)
                if not m:
                    continue
                vals = dict((k, float(v)) for k, v in re.findall(r"(\w+)=([-\d.]+)", m.group(4)))
                k = kind(m.group(2), m.group(3))
                for bucket in (k, "all"):
                    rows.setdefault(bucket, []).append(vals)
    if not rows:
        print("no I1686COST lines (was OD_CYCLE_COST=on?)")
        return 1
    print("kind    n     " + " ".join("%-13s" % s for s in STAGES))
    for bucket in ("idle", "event", "window", "close", "all"):
        rs = rows.get(bucket, [])
        if not rs:
            continue
        cells = []
        for s in STAGES:
            xs = [r.get(s, 0.0) for r in rs]
            cells.append("%-13s" % ("%.1f/%.1f" % (pct(xs, 0.5), pct(xs, 0.9))))
        print("%-7s %-5d %s" % (bucket, len(rs), " ".join(cells)))
    print("(each cell: median/p90 ms)")
    # mean share of the loop, over all cycles: what a stage costs the loop in total
    rs = rows["all"]
    total = sum(r.get("loop", 0.0) for r in rs)
    if total > 0:
        shares = []
        for s in STAGES[1:]:
            if s in ("process", "d_close", "decode_bg", "proc_cpu"):
                continue
            shares.append((sum(r.get(s, 0.0) for r in rs) / total, s))
        shares.sort(reverse=True)
        print("share of all loop time: " + ", ".join("%s %.0f%%" % (s, 100 * v) for v, s in shares))
        print("mean loop %.1f ms over %d cycles; off-loop decode %.1f ms per cycle" %
              (total / len(rs), len(rs), sum(r.get("decode_bg", 0.0) for r in rs) / len(rs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

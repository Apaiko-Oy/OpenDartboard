#!/usr/bin/env python3
"""#1655: are two 1555-bakeoff runs the same, dart by dart?

    python3 testers/i1655_rows.py <run dir A> <run dir B> [--label-a X --label-b Y]

A run dir is what testers/i1555_run.sh leaves in runs-<tree>/1555. For each of the five
censuses it compares every per-dart row (`I1555 PAIR`, `I1555 ACCURACY-DART`,
`I1555 ACCURACY-PHANTOM`) and the ACCURACY line, with the #1655 `clock=` label taken out
so a wall run and a capture run can be compared row for row. It also compares what the
detector published, in order (`I1555PUBLISH` lines, the dart and its score, without the
log's timestamps), which is stricter than the census: two runs can agree on every dart and
still publish at different cycles.

Prints one `I1655 SAME` or `I1655 DIFF` line per census and per log, then each differing
row, and exits 0 when everything is the same, 1 when anything differs. It judges nothing
else: which of two different rows is right is for the census and the reader.
"""
import argparse
import os
import re
import sys

RUNS = ["r18-dev", "r18-open", "r22-dev", "r22-open", "r18-pin"]
ROW = re.compile(r"^I1555 (PAIR|ACCURACY-DART|ACCURACY-PHANTOM|ACCURACY) ")
CLOCK = re.compile(r" clock=\S+")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
STAMP = re.compile(r"^\[[^\]]*\]\s*")


def rows(path):
    out = []
    for raw in open(path, errors="replace"):
        line = ANSI.sub("", raw.rstrip("\n"))
        if ROW.match(line):
            out.append(CLOCK.sub("", line))
    return out


def publishes(path):
    out = []
    for raw in open(path, errors="replace"):
        line = ANSI.sub("", raw.rstrip("\n"))
        i = line.find("I1555PUBLISH")
        if i >= 0:
            out.append(line[i:])
    return out


def compare(name, a, b, la, lb):
    if a == b:
        print("I1655 SAME %s rows=%d" % (name, len(a)))
        return 0
    print("I1655 DIFF %s rows=%d/%d" % (name, len(a), len(b)))
    sa, sb = set(a), set(b)
    for line in a:
        if line not in sb:
            print("   %s only: %s" % (la, line))
    for line in b:
        if line not in sa:
            print("   %s only: %s" % (lb, line))
    if sa == sb:
        print("   (same rows, different order or multiplicity)")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--label-a", default="A")
    ap.add_argument("--label-b", default="B")
    args = ap.parse_args()
    bad = 0
    for r in RUNS:
        ca = os.path.join(args.a, "census-%s.txt" % r)
        cb = os.path.join(args.b, "census-%s.txt" % r)
        if not (os.path.exists(ca) and os.path.exists(cb)):
            print("I1655 MISSING census-%s.txt" % r)
            bad = 1
            continue
        bad |= compare("census-" + r, rows(ca), rows(cb), args.label_a, args.label_b)
        la = os.path.join(args.a, "%s.txt" % r)
        lb = os.path.join(args.b, "%s.txt" % r)
        if os.path.exists(la) and os.path.exists(lb):
            bad |= compare("publish-" + r, publishes(la), publishes(lb),
                           args.label_a, args.label_b)
    return bad


if __name__ == "__main__":
    sys.exit(main())

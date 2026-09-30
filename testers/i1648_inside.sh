#!/bin/bash
# #1648, inside the container. Two narrowed replays of rig-20260922's opening window
# (visits 1-3: 900 detector cycles, video frames 90-989; cycle n is video frame n+89),
# OD_WINDOW_CENSUS=1:
#
#   hold     OD_TAKEOUT_REREPORT=off   (#1646's hold alone: the rule pinned off)
#   rule     nothing passed            (#1648's rule on top: the default since #1662)
#
# #1662 made both the default, so these are the same two configurations as before #1662
# (OD_SETTLE_EXPOSURE=hold, then that plus OD_TAKEOUT_REREPORT=departure), reached by the
# pin for the first and by the default for the second.
#
# ASSERTED:
#   hold  the pinned run says OD_TAKEOUT_REREPORT=off is set, and the default run says
#         no pin is set;
#   hold  window 2 (the visit-1 takeout) does NOT reconcile CLEAN, and the rule's line
#         never prints: what the switch is for, measured without it;
#   rule  the rule fires on camera 2 in window 2, and window 2 reconciles CLEAN;
#   rule  the publications read S16 [MISS] | S12 T9 T8 | S20 ... : visit 1 ends at its
#         takeout, and v2.1, v2.2, v2.3 and v3.1 each publish (#1648's "Done when").
set -u
BIN=/app/build/opendartboard
RUN=/run1648
D=/app/mocks/rig-20260922
[ -x $BIN ] || { echo "FAIL no $BIN"; exit 1; }

replay() { # $1 name, rest env
    local out="$1"; shift
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=900 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_SEEK_VIDEO=off OD_WINDOW_CENSUS=1 "$@" \
        timeout 600 $BIN --cams "$D/cam_1.mp4,$D/cam_2.mp4,$D/cam_3.mp4" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    [ $rc -eq 0 ] || [ $rc -eq 124 ] || { tail -5 "$RUN/$out.txt"; echo "FAIL the $out replay did not finish"; exit 1; }
}

# #1689: both with the board counts in the scoring area, where #1648 measured its rule.
# Since #1689 they are taken out to the rim, and there the visit-1 takeout reconciles
# CLEAN WITHOUT the rule (hold window 2: cameras 1 and 3 read CLEAN, 2 of 3), so the hold
# run would no longer show what the switch is for.
replay hold OD_TAKEOUT_REREPORT=off OD_BOARD_COUNT=scoring
replay rule OD_BOARD_COUNT=scoring

for n in hold rule; do
    echo "=== $n"
    grep -E 'WINDOW CENSUS|I1555PUBLISH|SCORE: END|TIP IDENTITY|I1648 REREPORT' "$RUN/$n.txt" |
        sed -e 's/^\[[A-Z]*\]\[[A-Z_]*\] - //' | cut -c1-260
done

python3 - "$RUN" <<'PY'
import re, sys
run = sys.argv[1]
def load(n):
    return open("%s/%s.txt" % (run, n), errors="replace").read().splitlines()
def census(lines):
    out = {}
    for l in lines:
        m = re.search(r"WINDOW CENSUS: #(\d+) .*?-> (\S+) \((\d+) up, (\d+) clean", l)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out
def sequence(lines):
    seq = []
    for l in lines:
        m = re.search(r"I1555PUBLISH window=\d+ \S+ score=(\S+)", l)
        if m:
            seq.append(m.group(1))
        elif "SCORE: END" in l:
            seq.append("|")
    return seq
bad = 0
def check(ok, what):
    global bad
    print(("OK   " if ok else "FAIL ") + what)
    bad += 0 if ok else 1

h, r = load("hold"), load("rule")
hc, rc = census(h), census(r)
check(any("OD_TAKEOUT_REREPORT=off is set" in l for l in h), "hold: the pinned run says OD_TAKEOUT_REREPORT=off is set")
check(not any(re.search(r"OD_(SETTLE_EXPOSURE|TAKEOUT_REREPORT)=off is set", l) for l in r),
      "rule: the default run says no pin is set")
check(hc.get(2) not in (None, "CLEAN"), "hold: window 2 (visit-1 takeout) reconciles %s, not CLEAN" % hc.get(2))
check(not any("I1648 REREPORT" in l for l in h), "hold: the rule's line never prints under its pin")
fired = [l for l in r if "I1648 REREPORT DEPARTURE: camera 2" in l]
check(len(fired) >= 1, "rule: fires on camera 2 (%d line(s))" % len(fired))
check(rc.get(2) == "CLEAN", "rule: window 2 reconciles %s" % rc.get(2))
seq = sequence(r)
text = " ".join(seq)
print("REPORT hold sequence: %s" % " ".join(sequence(h)))
print("REPORT rule sequence: %s" % text)
ok = re.match(r"^S16 (MISS )?\| S12 T9 T8 \| S20\b", text) is not None
check(ok, "rule: publications begin S16 [MISS] | S12 T9 T8 | S20")
sys.exit(1 if bad else 0)
PY
RC=$?
[ $RC -eq 0 ] && echo "I1648 DONE" || echo "I1648 FAILED"
exit $RC

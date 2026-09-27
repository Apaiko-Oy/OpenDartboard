#!/bin/bash
# #1650, inside the container. Three narrowed replays of rig-20260918's dev window
# (1620 cycles: through visit 6 and its takeout), OD_TRACE and OD_WINDOW_CENSUS=1, all on
# OD_MOTION_CLOCK=capture with OD_COOLDOWN_MS=900:
#
#   default   the tree's settle and cooldown
#   hold      OD_SETTLE_EXPOSURE=hold (#1646)
#   fix       OD_SETTLE_EXPOSURE=hold OD_COOLDOWN_EXPIRY=spike
#
# Why the clock and the cooldown are pinned: the motion machine's timers run on wall time
# by default, so where the 1000 ms cooldown ends, counted in cycles, depends on the box's
# load. The captures do not: one video frame a cycle at any load. The capture clock makes
# a cycle 33.3 ms, and 900 ms then ends the cooldown 27 cycles after the event's END --
# where a ~37 ms wall-clock cycle (a whole-clip replay at load 4-6) ends 1000 ms.
#
# Cycle n is video frame n+89 in this window (capture_ms 51200 at cycle 1447).
#
# ASSERTED, each a measured fact of #1650's comment:
#   all three  the 2's splash is cycle 1480: camera 3 over the spike threshold, one cycle;
#   default    the 7's event ends at 1447, the splash starts an event, S7 then S2 publish;
#   hold       the 7's event ends at 1453, the splash cycle is the cooldown's last
#              (COOLDOWN -> IDLE on it), no event starts there, and visit 6 publishes S7
#              then END with no S2;
#   fix        I1650 COOLDOWN EXPIRY SPIKE at cycle 1480, and S7 then S2 publish.
set -u
BIN=/app/build/opendartboard
RUN=/run1650
D=/app/mocks/rig-20260918
[ -x $BIN ] || { echo "FAIL no $BIN"; exit 1; }

replay() { # $1 name, rest env
    local out="$1"; shift
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=1620 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_WINDOW_CENSUS=1 \
        OD_MOTION_CLOCK=capture OD_COOLDOWN_MS=900 OD_TRACE="$RUN/$out.trace.csv" "$@" \
        timeout 900 $BIN --cams "$D/cam_1.mp4,$D/cam_2.mp4,$D/cam_3.mp4" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    [ $rc -eq 0 ] || [ $rc -eq 124 ] || { tail -5 "$RUN/$out.txt"; echo "FAIL the $out replay did not finish"; exit 1; }
}

replay default
replay hold OD_SETTLE_EXPOSURE=hold
replay fix OD_SETTLE_EXPOSURE=hold OD_COOLDOWN_EXPIRY=spike

for n in default hold fix; do
    echo "=== $n"
    grep -E 'WINDOW CENSUS: #2[0-3] |I1555PUBLISH window=2[0-3] |SCORE: END|I1646 EXPOSURE (HOLD|RELEASE) cycle=14|I1650' "$RUN/$n.txt" |
        sed -e 's/^\[[A-Z]*\]\[[A-Z_]*\] - //' | cut -c1-200 | tail -12
done

python3 - "$RUN" <<'PY'
import csv, re, sys
run = sys.argv[1]
SPLASH = 1480
def trace(n):
    return {int(r["cycle"]): r for r in csv.DictReader(open("%s/%s.trace.csv" % (run, n)))}
def text(n):
    return open("%s/%s.txt" % (run, n), errors="replace").read().splitlines()
def end_of_seven(t):
    # the first STABILIZING -> COOLDOWN after the 7's splash (1443)
    for c in range(1443, SPLASH):
        r = t.get(c)
        if r and r["state_in"] == "2" and r["state_out"] == "4":
            return c
    return None
def visit6(lines):
    seq, on = [], False
    for l in lines:
        m = re.search(r"I1555PUBLISH window=(\d+) \S+ score=(\S+)", l)
        if m and int(m.group(1)) >= 21:
            seq.append(m.group(2))
        elif "SCORE: END" in l and seq:
            seq.append("|")
            break
    return seq
bad = 0
def check(ok, what):
    global bad
    print(("OK   " if ok else "FAIL ") + what)
    bad += 0 if ok else 1

for n, end, pubs in (("default", 1447, ["S7", "S2", "|"]),
                     ("hold", 1453, ["S7", "|"]),
                     ("fix", 1453, ["S7", "S2", "|"])):
    t, lines = trace(n), text(n)
    r = t.get(SPLASH, {})
    cam3 = float(r.get("r2", 0))
    nxt = float(t.get(SPLASH + 1, {}).get("r2", 1))
    check(cam3 > 0.011 and nxt < 0.011,
          "%s: the 2's splash is one cycle, camera 3 %.4f at %d and %.4f after" % (n, cam3, SPLASH, nxt))
    e = end_of_seven(t)
    check(e == end, "%s: the 7's event ends at cycle %s (expected %d)" % (n, e, end))
    s = (r.get("state_in"), r.get("state_out"))
    if n == "hold":
        check(s == ("4", "0"), "hold: cycle %d is the cooldown's last, COOLDOWN -> IDLE (%s -> %s)" % ((SPLASH,) + s))
    else:
        check(s[1] == "1", "%s: cycle %d starts an event (%s -> %s)" % ((n, SPLASH) + s))
    fired = [l for l in lines if "I1650 COOLDOWN EXPIRY SPIKE cycle=%d " % SPLASH in l]
    if n == "fix":
        check(len(fired) == 1, "fix: I1650 COOLDOWN EXPIRY SPIKE at cycle %d (%d line(s))" % (SPLASH, len(fired)))
    got = visit6(lines)
    check(got == pubs, "%s: visit 6 publishes %s (expected %s)" % (n, " ".join(got), " ".join(pubs)))
sys.exit(1 if bad else 0)
PY
RC=$?
[ $RC -eq 0 ] && echo "I1650 DONE" || echo "I1650 FAILED"
exit $RC

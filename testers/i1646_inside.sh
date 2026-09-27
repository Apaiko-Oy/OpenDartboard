#!/bin/bash
# #1646, inside the container. Two narrowed replays of rig-20260922's opening window
# (visits 1-3: 780 detector cycles, video frames 90-869), OD_WINDOW_CENSUS=1 so each
# window prints every camera's board figure:
#
#   default   the tree's settle: motion quiet is settled
#   hold      OD_SETTLE_EXPOSURE=hold
#
# A detector cycle n is video frame n+89 on this window: the opening calibration
# consumes frames 0-89 (OD_TRACE's capture_ms is 3000 on the first cycle).
#
# ASSERTED, each a measured fact of #1646's comment:
#   default  window 2 (the visit-1 takeout) reads camera 3's board >= 50000 px changed:
#            the exposure, not a dart (a dart is ~1000-3000 px on camera 3);
#   default  window 3 (v2.1's arrival) reconciles CLEAN: the 12 is swallowed;
#   hold     camera 3 is held >= 20 cycles after the visit-1 takeout;
#   hold     window 2 reads camera 3's board under 5000 px;
#   hold     some window reconciles CLEAN on all three cameras (the visit-2 takeout);
#   hold     S12 and S20 both publish (v2.1 and v3.1 get their own windows).
# REPORTED, not asserted: that under the hold visit 1 does not end (the first takeout
# cannot reconcile against a reference that holds the parked 8), so T9/T8 hit the
# DART_3 cap. That is dart_processing's half of #1646 and a fix there should move it.
set -u
BIN=/app/build/opendartboard
RUN=/run1646
D=/app/mocks/rig-20260922
[ -x $BIN ] || { echo "FAIL no $BIN"; exit 1; }

replay() { # $1 name, rest env
    local out="$1"; shift
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=780 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_SEEK_VIDEO=off OD_WINDOW_CENSUS=1 "$@" \
        timeout 600 $BIN --cams "$D/cam_1.mp4,$D/cam_2.mp4,$D/cam_3.mp4" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    [ $rc -eq 0 ] || [ $rc -eq 124 ] || { tail -5 "$RUN/$out.txt"; echo "FAIL the $out replay did not finish"; exit 1; }
}

replay default
replay hold OD_SETTLE_EXPOSURE=hold

for n in default hold; do
    echo "=== $n"
    grep -E 'WINDOW CENSUS|I1555PUBLISH|SCORE: END|I1646 EXPOSURE' "$RUN/$n.txt" |
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
        if not m:
            continue
        cams = dict((int(c), int(b)) for c, b in re.findall(r"cam(\d) \S+ board=(\d+)/", l))
        out[int(m.group(1))] = (m.group(2), int(m.group(3)), int(m.group(4)), cams)
    return out
bad = 0
def check(ok, what):
    global bad
    print(("OK   " if ok else "FAIL ") + what)
    bad += 0 if ok else 1

d, h = load("default"), load("hold")
dc, hc = census(d), census(h)
check(2 in dc and dc[2][3].get(3, 0) >= 50000,
      "default: window 2 (visit-1 takeout) reads camera 3's board at %s px -- the exposure"
      % (dc.get(2, (0, 0, 0, {}))[3].get(3)))
check(3 in dc and dc[3][0] == "CLEAN",
      "default: window 3 (v2.1's arrival) reconciles %s -- the 12 is swallowed" % (dc.get(3, ("?",))[0]))
held = [int(x) for x in re.findall(r"EXPOSURE RELEASE .*? held=(\d+)", "\n".join(h))]
cam3 = [l for l in h if "EXPOSURE HOLD" in l and " cam=3 " in l]
check(len(cam3) >= 1 and max(held or [0]) >= 20,
      "hold: camera 3 held after a takeout (%d cam-3 holds; longest hold %s cycles)" % (len(cam3), max(held or [0])))
check(2 in hc and hc[2][3].get(3, 10 ** 9) < 5000,
      "hold: window 2 reads camera 3's board at %s px" % (hc.get(2, (0, 0, 0, {}))[3].get(3)))
check(any(v[0] == "CLEAN" and v[2] == 3 for v in hc.values()),
      "hold: a takeout reconciles CLEAN on all three cameras")
pubs = re.findall(r"I1555PUBLISH window=\d+ \S+ score=(\S+)", "\n".join(h))
check("S12" in pubs and "S20" in pubs, "hold: publishes %s" % " ".join(pubs))
seq = []
for l in h:
    m = re.search(r"I1555PUBLISH window=\d+ \S+ score=(\S+)", l)
    if m:
        seq.append(m.group(1))
    elif "SCORE: END" in l:
        seq.append("|")
print("REPORT hold publication sequence: %s" % " ".join(seq))
if "S12" not in seq:
    ended = "n/a (no S12)"
elif "|" in seq[:seq.index("S12")]:
    ended = "yes"
else:
    ended = "no -- dart_processing's half of #1646"
print("REPORT (not asserted) visit 1 ends before v2.1 under the hold: %s" % ended)
sys.exit(1 if bad else 0)
PY
RC=$?
[ $RC -eq 0 ] && echo "I1646 DONE" || echo "I1646 FAILED"
exit $RC

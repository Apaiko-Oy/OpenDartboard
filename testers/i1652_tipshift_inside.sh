#!/bin/bash
# #1652, inside the container: how far the published tip -- the one the string vote
# scores and the entry solve corroborates with -- moves on the board when the fresh-diff
# chain is made translation-free (OD_MASK_UNSHIFT=on), per camera.
#
# Four replays on OD_MOTION_CLOCK=capture (#1650), so the off and on runs cut the same
# windows whatever the box's load: rig-20260918 and rig-20260922, dev window, each with
# the switch off and on, all with OD_MASK_SHIFT_CENSUS=1. Every I1652TIP line gives the
# tip's board point and what the chain's (+4, +4) px is worth there in mm. The two runs'
# lines are paired by (window, camera) and the measured move is set beside that figure.
# REPORTED, not asserted, except that each run must print census lines at all.
set -u
BIN=/app/build/opendartboard
RUN=/run1652
[ -x $BIN ] || { echo "FAIL no $BIN"; exit 1; }

replay() { # $1 fixture, $2 name, rest env
    local d="/app/mocks/$1" out="$2"; shift 2
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_MASK_SHIFT_CENSUS=1 \
        OD_MOTION_CLOCK=capture "$@" \
        timeout 900 $BIN --cams "$d/cam_1.mp4,$d/cam_2.mp4,$d/cam_3.mp4" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt") tipLines=$(grep -c I1652TIP "$RUN/$out.txt")"
    [ $rc -eq 0 ] || [ $rc -eq 124 ] || { tail -5 "$RUN/$out.txt"; echo "FAIL the $out replay did not finish"; exit 1; }
}

replay rig-20260918 r18-off
replay rig-20260918 r18-on OD_MASK_UNSHIFT=on
replay rig-20260922 r22-off
replay rig-20260922 r22-on OD_MASK_UNSHIFT=on

python3 - "$RUN" <<'PY'
import re, sys, statistics as st
run = sys.argv[1]
pat = re.compile(r"I1652TIP window=(\d+) cam=(\d) fixed=(\d) tip=\(([-\d.]+),([-\d.]+)\) board=\(([-\d.]+),([-\d.]+)\) "
                 r"r=([-\d.]+) chainMm=([-\d.]+) radial=([-\d.]+) tangential=([-\d.]+)")
pub = re.compile(r"I1555PUBLISH window=(\d+) \S+ score=(\S+) .* vote=(\S+) ")
def load(n):
    rows, pubs = {}, {}
    for l in open("%s/%s.txt" % (run, n), errors="replace"):
        m = pat.search(l)
        if m:
            g = m.groups()
            rows[(int(g[0]), int(g[1]))] = [float(x) for x in g[3:]]
        m = pub.search(l)
        if m:
            pubs[int(m.group(1))] = "%s(vote %s)" % (m.group(2), m.group(3))
    return rows, pubs
bad = 0
for fx in ("r18", "r22"):
    off, poff = load(fx + "-off")
    on, pon = load(fx + "-on")
    if not off or not on:
        print("FAIL %s: no I1652TIP lines (off %d, on %d)" % (fx, len(off), len(on))); bad += 1; continue
    print("=== %s dev: %d off, %d on, %d paired (window, camera)" % (fx, len(off), len(on), len(set(off) & set(on))))
    per = {}
    for key in sorted(set(off) & set(on)):
        a, b = off[key], on[key]
        dpx = (b[0] - a[0], b[1] - a[1])
        dmm = ((b[2] - a[2]) ** 2 + (b[3] - a[3]) ** 2) ** 0.5
        r = (a[2] ** 2 + a[3] ** 2) ** 0.5 or 1.0
        rad = ((b[2] - a[2]) * a[2] + (b[3] - a[3]) * a[3]) / r
        same = abs(dpx[0] + 4) <= 1.5 and abs(dpx[1] + 4) <= 1.5
        print("I1652PAIR %s window=%d cam=%d tipPx=(%.1f,%.1f)->(%.1f,%.1f) dPx=(%+.1f,%+.1f) r=%.1f->%.1f "
              "moved=%.2fmm radial=%+.2f chainMm=%.2f %s" %
              (fx, key[0], key[1], a[0], a[1], b[0], b[1], dpx[0], dpx[1], a[4], b[4], dmm, rad, a[5],
               "translated" if same else "OTHER-POINT"))
        per.setdefault(key[1], []).append((dmm, a[5], rad, same))
    for c in sorted(per):
        v = per[c]
        tr = [x for x in v if x[3]]
        print("I1652CAM %s cam=%d n=%d translated=%d | chain's (+4,+4) px at the tip: median %.2f mm (%.2f..%.2f) | "
              "measured move of translated tips: median %.2f mm, radial median %+.2f mm" %
              (fx, c, len(v), len(tr), st.median(x[1] for x in v), min(x[1] for x in v), max(x[1] for x in v),
               st.median(x[0] for x in tr) if tr else float("nan"),
               st.median(x[2] for x in tr) if tr else float("nan")))
    for w in sorted(set(poff) | set(pon)):
        if poff.get(w) != pon.get(w):
            print("I1652PUB %s window=%d published off=%s on=%s" % (fx, w, poff.get(w, "-"), pon.get(w, "-")))
print("PASS i1652_tipshift" if bad == 0 else "FAIL i1652_tipshift")
sys.exit(1 if bad else 0)
PY
exit $?

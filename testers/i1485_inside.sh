#!/bin/bash
# #1485, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1485.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

CENSUS=/run1485/census
SRC=/app

build_census() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -I "$1/src" -I "$1/src/utils" -I "$1/src/detector/geometry/calibration" \
    -o "$2" "$1/testers/i1485_radius_census.cpp" \
    "$1"/src/detector/geometry/calibration/*.cpp \
    "$1"/src/detector/geometry/detection/score_processing.cpp \
    "$1"/src/detector/geometry/detection/dart_processing.cpp \
    "$1"/src/detector/geometry/detection/motion_processing.cpp \
    $(pkg-config --cflags --libs opencv4) > "$2.build.log" 2>&1
}

echo "=== building the radius census ==="
if ! build_census "$SRC" "$CENSUS"; then
  tail -30 "$CENSUS.build.log"
  echo "FAIL the radius census did not build; nothing below measures anything"
  exit 2
fi

collect() { # $1 binary, $2 out-file, rest: env for the run
  : > "$2"
  local c
  for c in 1 2 3; do "$1" "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) 1 90 2>/dev/null >> "$2"; done
  for c in 1 2 3; do "$1" "$SRC/mocks/cam_$c.mp4" $((c-1)) 1 90 2>/dev/null >> "$2"; done
}

collect "$CENSUS" /run1485/rows.txt
OD_RINGS=asfitted collect "$CENSUS" /run1485/asfitted.txt

echo
echo "=== 1. every ring, as a multiple of the ray-traced doubles ring ================"
# The control and the defect in one table. The bands are geometry -- each ring's
# millimetres over the doubles ring's, with the boundary at the geometric mean of two
# adjacent rings -- so nothing here was chosen by looking at this footage.
grep '^I1485 ' /run1485/rows.txt
echo
echo "    the board's own millimetres put them at 0.0374 0.0935 0.5824 0.6294 0.9529"

# The rig's 25 ring is the defect, measured. This asserts the DEFECT IS STILL THERE in
# the fitted geometry -- the repair drops the ring, it does not mend the contour -- and it
# is read off the OD_RINGS=asfitted run, which is the tree as it was before this issue.
python3 - /run1485/asfitted.txt <<'PY'
import sys
rows = [r for r in open(sys.argv[1]) if r.startswith("I1485 ")]
def f(r):
    return dict(kv.split("=",1) for kv in r.split() if "=" in kv)
rig  = [f(r) for r in rows if "rig-20260918" in r]
mock = [f(r) for r in rows if "rig-20260918" not in r]
ok = True
for r in rig:
    x = float(r["outerBull"])
    print("  rig %s: the 25 ring is fitted at %.4f of the board -- %.1fx the 0.0935 the "
          "millimetres give" % (r["clip"].split("/")[-1], x, x/0.0935))
    ok = ok and x > 0.2334
for r in mock:
    x = float(r["outerBull"])
    print("  control %s: %.4f, inside 0.0591..0.2334" % (r["clip"].split("/")[-1], x))
    ok = ok and 0.0591 <= x <= 0.2334
sys.exit(0 if ok else 1)
PY
if [ $? -eq 0 ]; then
  say "OK   the rig fits a 25 ring outside its band on all three cameras, and the shipped mocks fit one inside it" ok
else
  say "FAIL the fixture no longer shows the defect, or the control no longer passes -- neither half of this measures anything now" no
fi

echo
echo "=== 2. the radius a dart is JUDGED against, against the one it is AT ==========="
# The number the issue asks for. A tip is placed on a ray from the bull at a known true
# fraction of the board's radius along that ray -- the board being the ray-traced doubles
# ellipse -- and `score_processing::scorePoint` is asked what radius it is at.
python3 - /run1485/asfitted.txt /run1485/rows.txt <<'PY'
import sys
def read(path):
    out = {}
    for line in open(path):
        if not line.startswith("I1485RING "): continue
        f = dict(kv.split("=",1) for kv in line.split() if "=" in kv)
        if "rig-20260918/cam_2" not in f["clip"]: continue
        if f["r"] == "none": continue
        out.setdefault(f["f"], []).append(float(f["r"]))
    return out
before, after = read(sys.argv[1]), read(sys.argv[2])
print("  rig camera 2, the worst of the three:")
print("    true radius   judged before   judged after")
worst = 0.0
for k in sorted(before, key=float):
    b = sum(before[k])/len(before[k])
    a = sum(after.get(k, [0]))/max(1, len(after.get(k, [1])))
    print("      %s         %.4f         %.4f" % (k, b, a))
    if float(k) <= 1.0 and b > 0:
        worst = max(worst, float(k)/b)
print("  before this issue a dart is judged at up to %.1fx nearer the bull than it is" % worst)
sys.exit(0 if worst >= 3.0 else 1)
PY
if [ $? -eq 0 ]; then
  say "OK   the departure is measured on this fixture and it is a factor, not a percent" ok
else
  say "FAIL the radius a dart is judged against no longer departs from the real one here" no
fi

echo
echo "=== 3. nothing on the board is published as a 25 that is not in the 25 ring ===="
# `OUTER` is the OUTER BULL -- turnaus_client::postableSector posts it as "25" -- so this
# is the issue's own criterion said exactly: a dart on the board must not read as one.
python3 - /run1485/rows.txt <<'PY'
import sys
bad = []
n = 0
for line in open(sys.argv[1]):
    if not line.startswith("I1485RING "): continue
    f = dict(kv.split("=",1) for kv in line.split() if "=" in kv)
    n += 1
    if f["want"] != "outer" and f["got"] == "OUTER": bad.append(f)
    if f["want"] == "off" and f["got"] != "MISS": bad.append(f)
print("  %d probes over six cameras, %d of them published as something they are not" % (n, len(bad)))
for f in bad[:12]:
    print("    %s deg=%s f=%s want=%s got=%s" % (f["clip"].split("/")[-1], f["deg"], f["f"], f["want"], f["got"]))
sys.exit(0 if not bad else 1)
PY
if [ $? -eq 0 ]; then
  say "OK   no dart on the board reads as a 25, and every dart past the edge reads MISS" ok
else
  say "FAIL a dart on the board still reads as a 25, or a dart past the edge does not read MISS" no
fi

echo
echo "=== 4. THE FALSIFIER: OD_RINGS=asfitted, on the same binary ==================="
# The state before this issue, reachable at run time: every contour is taken for the ring
# it is named after. A switch that only ever refuses would pass any check asking it to.
python3 - /run1485/asfitted.txt <<'PY'
import sys
bad = 0; n = 0
for line in open(sys.argv[1]):
    if not line.startswith("I1485RING "): continue
    f = dict(kv.split("=",1) for kv in line.split() if "=" in kv)
    if "rig-20260918" not in f["clip"]: continue
    n += 1
    if f["want"] != "outer" and f["got"] == "OUTER": bad += 1
print("  with OD_RINGS=asfitted the rig publishes %d of %d on-board probes as a 25" % (bad, n))
sys.exit(0 if bad > 0 else 1)
PY
if [ $? -eq 0 ]; then
  say "OK   the falsifier puts the defect back on the same binary" ok
else
  say "FAIL OD_RINGS=asfitted changes nothing, so section 3 is not measuring this repair" no
fi

echo
echo "=== 5. the detector itself, over the whole of mocks/rig-20260918 =============="
# The census above holds the DECISION; this holds the PROGRAM. OD_MAX_CYCLES is high
# enough to reach the end of the footage: at 1200 the run stopped after four of seven
# visits and the last line was an ordinary END, so the truncation was invisible.
run_detector() { # $1 tag, rest: env
  local tag="$1"; shift
  rm -rf "/run1485/$tag"; mkdir -p "/run1485/$tag"; cd "/run1485/$tag"
  env "$@" OD_MAX_CYCLES=6000 /app/build/opendartboard \
    --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
    --width 1280 --height 720 > "/run1485/$tag.log" 2>&1
  cd /
}
if [ ! -x /app/build/opendartboard ]; then
  say "FAIL /app/build/opendartboard is not here, so the program half of this measures nothing" no
else
  run_detector after OD_RINGS=
  AFTER_OUT=$(grep -c 'SCORE: OUTER' /run1485/after.log || true)
  AFTER_DARTS=$(grep 'SCORE: ' /run1485/after.log | grep -vc 'SCORE: END' || true)
  AFTER_ENDS=$(grep -c 'SCORE: END' /run1485/after.log || true)
  echo "  after:  $AFTER_DARTS darts over $AFTER_ENDS completed visits, $AFTER_OUT published OUTER"
  grep -E 'SCORE: ' /run1485/after.log | sed 's/.*SCORE: /    /; s/ | Processing.*//'
  # GROUND-TRUTH.md: twenty-one throws, two of them misses, and NOT ONE of the nineteen
  # darts that hit the board is in the 25 ring. So every OUTER here is a dart on the board
  # published as a 25.
  if [ "$AFTER_OUT" -eq 0 ]; then
    say "OK   no dart in the whole of the rig footage is published as a 25; GROUND-TRUTH.md has none in the 25 ring" ok
  else
    say "FAIL $AFTER_OUT darts are still published as a 25, and GROUND-TRUTH.md says none was thrown at one" no
  fi
  run_detector before OD_RINGS=asfitted
  BEFORE_OUT=$(grep -c 'SCORE: OUTER' /run1485/before.log || true)
  BEFORE_DARTS=$(grep 'SCORE: ' /run1485/before.log | grep -vc 'SCORE: END' || true)
  echo "  before: $BEFORE_DARTS darts, $BEFORE_OUT published OUTER"
  if [ "$BEFORE_OUT" -gt 0 ]; then
    say "OK   OD_RINGS=asfitted publishes $BEFORE_OUT of $BEFORE_DARTS darts as a 25 on the same binary" ok
  else
    say "FAIL the falsifier publishes no 25s either, so the detector half proves nothing" no
  fi
fi

echo
echo "=== 6. THE MUTATION PROOF: widen the bands and watch this file go red ========="
# A tester that has never been shown to fail is a tester nobody can trust (#1412, #1463).
# The break is a band that cannot refuse anything -- which is the defect itself, reached
# through the source rather than through the switch section 4 uses, so the two are not one
# measurement written twice.
PLANT=/run1485/planted
rm -rf "$PLANT"; mkdir -p "$PLANT"
cp -r "$SRC/src" "$SRC/testers" "$PLANT/"
sed -i 's|inline double ringBandHigh(int ring) { return std::sqrt(ringTruth(ring) \* ringTruth(ring + 1)); }|inline double ringBandHigh(int ring) { return ringTruth(ring) * 50.0; }|' \
  "$PLANT/src/detector/geometry/calibration/ellipse_processing.hpp"
if ! grep -q 'ringTruth(ring) \* 50.0' "$PLANT/src/detector/geometry/calibration/ellipse_processing.hpp"; then
  say "FAIL the plant did not land -- ringBandHigh was not where this proof expects it, so nothing below proves anything" no
else
  if ! build_census "$PLANT" /run1485/planted_census; then
    tail -20 /run1485/planted_census.build.log
    say "FAIL the planted tree did not build, so the mutation proves nothing" no
  else
    collect /run1485/planted_census /run1485/planted_rows.txt
    P=$(python3 - /run1485/planted_rows.txt <<'PY'
import sys
bad = 0
for line in open(sys.argv[1]):
    if not line.startswith("I1485RING "): continue
    f = dict(kv.split("=",1) for kv in line.split() if "=" in kv)
    if f["want"] != "outer" and f["got"] == "OUTER": bad += 1
print(bad)
PY
)
    echo "    with the band at 50x each ring, $P on-board probes publish as a 25"
    if [ "$P" -gt 0 ]; then
      say "OK   the mutation is fatal: section 3 goes red on it" ok
    else
      say "FAIL the mutation changed nothing, so section 3 cannot fail and measures nothing" no
    fi
  fi
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1485: PASS"; else echo "i1485: FAIL"; fi
exit $FAILED

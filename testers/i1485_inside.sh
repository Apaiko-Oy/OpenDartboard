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

# RE-POINTED BY #1515. Until 77bb5b1 this section asserted the DEFECT WAS STILL THERE in
# the asfitted geometry -- the rig fitted its 25 ring at 0.9733/0.6112/0.3401 of the
# board, 3.6x-10.4x the 0.0935 the millimetres give, and #1485's repair dropped the ring
# rather than mending the contour. 77bb5b1 ("a red room is not the board", 2026-09-22)
# then mended the contour itself: each ring mask keeps the largest component whose hull
# contains the bull, so even OD_RINGS=asfitted fits the rig's 25 ring in band
# (0.0910/0.0927/0.0973, measured by #1499's gate) and the old assertion was red on every
# tree from that commit on. What is asserted now is that the mask repair HOLDS: the fitted
# 25 ring stays in band on the rig and on the shipped mocks alike, before any hold is
# applied. Section 4 is where the defect is still reproduced, by undoing 77bb5b1 in a
# planted tree.
python3 - /run1485/asfitted.txt <<'PY'
import sys
rows = [r for r in open(sys.argv[1]) if r.startswith("I1485 ")]
def f(r):
    return dict(kv.split("=",1) for kv in r.split() if "=" in kv)
rig  = [f(r) for r in rows if "rig-20260918" in r]
mock = [f(r) for r in rows if "rig-20260918" not in r]
ok = len(rig) == 3 and len(mock) == 3
for r in rig:
    x = float(r["outerBull"])
    print("  rig %s: the 25 ring is fitted at %.4f of the board, inside 0.0591..0.2334 "
          "(the millimetres give 0.0935)" % (r["clip"].split("/")[-1], x))
    ok = ok and 0.0591 <= x <= 0.2334
for r in mock:
    x = float(r["outerBull"])
    print("  control %s: %.4f, inside 0.0591..0.2334" % (r["clip"].split("/")[-1], x))
    ok = ok and 0.0591 <= x <= 0.2334
sys.exit(0 if ok else 1)
PY
if [ $? -eq 0 ]; then
  say "OK   every camera of both fixtures fits the 25 ring in band before any hold -- 77bb5b1's mask repair holds" ok
else
  say "FAIL a 25 ring is fitted outside its band before the hold -- the mask defect 77bb5b1 repaired is back, or a camera stopped calibrating" no
fi

echo
echo "=== 2. retired by #1515: the judged-radius departure no longer exists to measure ="
# RETIRED BY NAME (#1515), the way #1353 moved i1338's constant rather than deleting it.
# This section measured the number #1485 was filed for: a tip placed at a known true
# fraction of the board's radius was judged by `scorePoint` at up to 5.9x nearer the bull
# than it was, read off the OD_RINGS=asfitted run on rig camera 2, and it asserted
# `worst >= 3.0` -- the departure is a factor, not a percent. 77bb5b1's mask repair
# (keep the component whose hull contains the bull) mended the fits themselves, so on
# every tree since 2026-09-22 the asfitted judged radius tracks the true one and the
# assertion was red on main with nothing wrong (#1499's gate measured it; #1515 retired
# it). The measurement it made is in git history: `git log -p --follow testers/i1485_inside.sh`
# at eb47d4e holds the section, and GROUND-TRUTH.md's "cause of the eight 25s" section
# holds its conclusion. Sections 1 and 4 carry what is still falsifiable: the fits stay
# in band, and undoing 77bb5b1 brings the departure back.
echo "    (was: scorePoint judged a dart at up to 5.9x nearer the bull than it is, on the"
echo "     asfitted geometry; 77bb5b1 mended those fits and the departure with them)"

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
echo "=== 4. THE FALSIFIER: 77bb5b1 undone in a planted tree, and the hold refuses it ="
# RE-POINTED BY #1515. Until 77bb5b1 this section was `OD_RINGS=asfitted` on the same
# binary: the fits themselves held the defect, so bypassing the hold put it back at run
# time. 77bb5b1's mask repair mended the fits, so on every tree since 2026-09-22 the
# switch changes nothing on this footage and the old assertion (asfitted publishes 25s)
# was red on main with nothing wrong -- #1499's gate measured it, #1515 re-pointed it.
# The defect is now reproduced where it still exists: undo 77bb5b1's
# keep-the-component-around-the-bull in a planted copy of the source -- each ring mask
# back to keeping the largest component of anything, which is what fitted a red carpet as
# a doubles ring -- and the rig fits its 25 ring huge again. Then both halves of #1485
# are asked on that planted tree: OD_RINGS=asfitted publishes on-board probes as 25s
# (the defect, red today), and the default hold refuses the recreated bad fit and
# publishes none (the repair, still catching it).
REVERT=/run1485/revert77bb5b1
rm -rf "$REVERT"; mkdir -p "$REVERT"
cp -r "$SRC/src" "$SRC/testers" "$REVERT/"
sed -i 's|if (around.x >= 0 \&\& around.y >= 0)|if (false /* #1515: 77bb5b1 undone -- no ring is held to its bull */)|' \
  "$REVERT/src/detector/geometry/calibration/mask_processing.cpp"
if ! grep -q '77bb5b1 undone' "$REVERT/src/detector/geometry/calibration/mask_processing.cpp"; then
  say "FAIL the revert did not land -- 77bb5b1's hull test is not where this falsifier expects it, so nothing here reproduces anything" no
else
  if ! build_census "$REVERT" /run1485/revert_census; then
    tail -20 /run1485/revert_census.build.log
    say "FAIL the reverted tree did not build, so the falsifier proves nothing" no
  else
    : > /run1485/revert_asfitted.txt
    : > /run1485/revert_held.txt
    for c in 1 2 3; do
      OD_RINGS=asfitted /run1485/revert_census "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) 1 90 2>/dev/null >> /run1485/revert_asfitted.txt
      /run1485/revert_census "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) 1 90 2>/dev/null >> /run1485/revert_held.txt
    done
    grep '^I1485 ' /run1485/revert_asfitted.txt | sed 's/^/    /'
    python3 - /run1485/revert_asfitted.txt /run1485/revert_held.txt <<'PY'
import sys
def bad(path):
    b = 0; n = 0
    for line in open(path):
        if not line.startswith("I1485RING "): continue
        f = dict(kv.split("=",1) for kv in line.split() if "=" in kv)
        n += 1
        if f["want"] != "outer" and f["got"] == "OUTER": b += 1
    return b, n
asf, an = bad(sys.argv[1])
held, hn = bad(sys.argv[2])
print("  with 77bb5b1 undone and OD_RINGS=asfitted, %d of %d on-board probes publish as a 25" % (asf, an))
print("  with 77bb5b1 undone and the rings HELD,    %d of %d do" % (held, hn))
sys.exit(0 if asf > 0 and held == 0 and an > 0 and hn > 0 else 1)
PY
    if [ $? -eq 0 ]; then
      say "OK   the falsifier puts the defect back through the source, and #1485's hold still refuses it" ok
    else
      say "FAIL undoing 77bb5b1 reproduces no defect, or the hold no longer refuses it -- section 3 is not being measured against anything that can fail" no
    fi
  fi
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
  # The `before` run is RETIRED BY NAME (#1515). It ran the same binary under
  # OD_RINGS=asfitted and asserted the whole program published 25s -- true until
  # 77bb5b1's mask repair mended the fits, red on every tree since, with nothing wrong
  # (#1499's gate measured it). The program-level defect reproduction would need a
  # detector binary built from a reverted tree, which is a build this harness does not
  # own; the decision-level reproduction lives in section 4's planted tree, and the
  # program half above still holds the shipped binary to GROUND-TRUTH.md.
  echo "  (the OD_RINGS=asfitted 'before' run is retired: since 77bb5b1 the switch changes"
  echo "   nothing on this footage -- section 4 reproduces the defect through the source)"
fi

echo
echo "=== 6. THE MUTATION PROOF: a bad fit meets a band that cannot refuse it ======="
# A tester that has never been shown to fail is a tester nobody can trust (#1412, #1463).
# RE-POINTED BY #1515. The break used to be the band alone, widened to 50x -- which was
# fatal while the fits held the defect, because the widened band ADMITTED the huge 25
# ring. 77bb5b1's mask repair mended the fits, so on every tree since 2026-09-22 a
# widened band had nothing bad left to admit, 0 probes went wrong, and this section was
# red on main with nothing wrong (#1499's gate measured it). The plant now recreates both
# halves of the day the band exists for: 77bb5b1 undone (the fit goes bad, section 4's
# mutation) AND the band at 50x (the hold cannot refuse it), run with the rings HELD --
# so what is proved is that the band's arithmetic is the one thing standing between a bad
# fit and a published 25, which is section 3's whole subject. Section 4 proves the hold
# refuses the bad fit; this section proves that WITHOUT the band it would not.
PLANT=/run1485/planted
rm -rf "$PLANT"; mkdir -p "$PLANT"
cp -r "$SRC/src" "$SRC/testers" "$PLANT/"
sed -i 's|if (around.x >= 0 \&\& around.y >= 0)|if (false /* #1515: 77bb5b1 undone -- the fit goes bad */)|' \
  "$PLANT/src/detector/geometry/calibration/mask_processing.cpp"
sed -i 's|inline double ringBandHigh(int ring) { return std::sqrt(ringTruth(ring) \* ringTruth(ring + 1)); }|inline double ringBandHigh(int ring) { return ringTruth(ring) * 50.0; }|' \
  "$PLANT/src/detector/geometry/calibration/ellipse_processing.hpp"
if ! grep -q '77bb5b1 undone' "$PLANT/src/detector/geometry/calibration/mask_processing.cpp" \
   || ! grep -q 'ringTruth(ring) \* 50.0' "$PLANT/src/detector/geometry/calibration/ellipse_processing.hpp"; then
  say "FAIL a plant did not land -- the hull test or ringBandHigh is not where this proof expects it, so nothing below proves anything" no
else
  if ! build_census "$PLANT" /run1485/planted_census; then
    tail -20 /run1485/planted_census.build.log
    say "FAIL the planted tree did not build, so the mutation proves nothing" no
  else
    : > /run1485/planted_rows.txt
    for c in 1 2 3; do
      /run1485/planted_census "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) 1 90 2>/dev/null >> /run1485/planted_rows.txt
    done
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
    echo "    with the fit bad and the band at 50x, $P on-board probes publish as a 25 THROUGH the hold"
    if [ "$P" -gt 0 ]; then
      say "OK   the mutation is fatal: the band is what refuses section 4's bad fit, and section 3 goes red without it" ok
    else
      say "FAIL the mutation changed nothing, so section 3 cannot fail and measures nothing" no
    fi
  fi
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1485: PASS"; else echo "i1485: FAIL"; fi
exit $FAILED

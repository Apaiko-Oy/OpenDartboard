#!/bin/bash
# #1423, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1423.
#
# It never ends on an `echo`: #1463 is open about six scripts that do, and a tester whose
# last statement is an echo exits 0 whatever it measured. This one ends on `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

CENSUS=/run1423/census
SRC=/app

build_census() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -I "$1/src" -I "$1/src/utils" -I "$1/src/detector/geometry/calibration" \
    -o "$2" "$1/testers/i1423_ring_census.cpp" \
    "$1"/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
    > "$2.build.log" 2>&1
}

echo "=== building the ring census ==="
if ! build_census "$SRC" "$CENSUS"; then
  tail -30 "$CENSUS.build.log"
  echo "FAIL the ring census did not build; nothing below measures anything"
  exit 2
fi

# One row per look, for one clip at its own camera index -- which decides the seek, and
# therefore which stretch of the clip is read (DEBUG_SEEK_VIDEO's arithmetic is in the
# census itself).
LOOKS="${LOOKS:-3}"
SPACING="${SPACING:-90}"
census() { "$1" "$2" "$3" "$LOOKS" "$SPACING" 2>/dev/null | grep -E '^I1423 '; }

field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<< "$1"; }

collect() { # $1 binary, $2 out-file
  : > "$2"
  local c
  for c in 1 2 3; do census "$1" "$SRC/mocks/cam_$c.mp4" $((c-1)) >> "$2"; done
  for c in 1 2 3; do census "$1" "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) >> "$2"; done
}

echo
echo "=== 1. every look on both fixtures, and what ring it names ====================="
collect "$CENSUS" /run1423/rows.txt
cat /run1423/rows.txt
ROWS=$(wc -l < /run1423/rows.txt)
if [ "$ROWS" -eq $((6 * LOOKS)) ]; then
  say "OK   $ROWS rows, which is $LOOKS looks on each of six cameras" ok
else
  say "FAIL $ROWS rows where $((6 * LOOKS)) were expected -- a clip was left out, and a census that skips a fixture is not a control" no
fi

# THE CONTROL PAIR. The two fixtures disagree about this BY CONSTRUCTION -- the rig's
# doubles ring is broken into arcs in the colour mask and its largest closed region is the
# treble ring -- so a rule that named them both the same thing would be measuring nothing.
MOCK_ROWS=$(grep -c 'clip=/app/mocks/cam_' /run1423/rows.txt)
MOCK_D=$(grep 'clip=/app/mocks/cam_' /run1423/rows.txt | grep -c 'ring=doubles')
RIG_ROWS=$(grep -c 'clip=/app/mocks/rig-20260918/' /run1423/rows.txt)
RIG_T=$(grep 'clip=/app/mocks/rig-20260918/' /run1423/rows.txt | grep -c 'ring=trebles')
if [ "$MOCK_D" -eq "$MOCK_ROWS" ] && [ "$MOCK_ROWS" -gt 0 ]; then
  say "OK   mocks/cam_*.mp4: $MOCK_D of $MOCK_ROWS looks name the DOUBLES ring" ok
else
  say "FAIL mocks/cam_*.mp4: only $MOCK_D of $MOCK_ROWS looks name the doubles ring" no
fi
if [ "$RIG_T" -eq "$RIG_ROWS" ] && [ "$RIG_ROWS" -gt 0 ]; then
  say "OK   mocks/rig-20260918: $RIG_T of $RIG_ROWS looks name the TREBLE ring" ok
else
  say "FAIL mocks/rig-20260918: only $RIG_T of $RIG_ROWS looks name the treble ring" no
fi

echo
echo "=== 2. the reading sits inside its band, with the margin printed ==============="
# The bands are geometry and not a fitted number: sqrt(170/107) = 1.2605 either side of
# each of the two legal answers, 1.0 and 1.589. This asserts the MARGIN rather than the
# verdict, because a verdict that is true by a hair is a verdict about this footage.
python3 - /run1423/rows.txt <<'PY'
import sys, math, re
rows = open(sys.argv[1]).read().strip().splitlines()
band = math.sqrt(170.0/107.0)
legal = {"doubles": 1.0, "trebles": 170.0/107.0}
worst = 99.0
bad = []
for r in rows:
    f = dict(kv.split("=",1) for kv in r.split() if "=" in kv)
    ring, reach = f["ring"], float(f["reach"])
    if ring not in legal:
        bad.append(r); continue
    c = legal[ring]
    # distance to the nearer boundary, as a ratio
    m = min(reach/(c/band), (c*band)/reach)
    worst = min(worst, m)
    if m <= 1.0: bad.append(r)
print("the tightest margin over %d rows is %.3fx the nearer band edge" % (len(rows), worst))
sys.exit(1 if bad else 0)
PY
if [ $? -eq 0 ]; then say "OK   every reading is inside its band, and none of them near the edge" ok
else say "FAIL a reading is outside its band, or on it" no; fi

echo
echo "=== 3. the four measurements, re-taken in these terms =========================="
# #1378, #1393, #1392 and #1388/#1416 each measured this same physical thing while
# measuring something else, and each expressed it as a ratio. Re-expressed as
# board-radius-over-span they read: #1378 mocks 0.99/0.90/0.92 and rig 1.63/1.63/1.60;
# #1393 rig 1/0.62 = 1.61; #1388/#1416 rig 1/0.64 = 1.56. Four independent numbers
# converging is the evidence this slice inherits, so here is the fifth beside them.
for cam in 1 2 3; do
  R=$(grep "rig-20260918/cam_$cam" /run1423/rows.txt | sed -n 's/.* reach=\([^ ]*\).*/\1/p' | head -1)
  B=$(grep "rig-20260918/cam_$cam" /run1423/rows.txt | sed -n 's/.* boardRadius=\([^ ]*\).*/\1/p' | head -1)
  S=$(grep "rig-20260918/cam_$cam" /run1423/rows.txt | sed -n 's/.* span=\([^ ]*\).*/\1/p' | head -1)
  echo "    rig camera $cam: span $S px, reach $R spans, board $B px"
done
# #1378 measured the fitted doubles semi-major over the span at 1.63, 1.63 and 1.60 on
# this rig. The reach is the same ratio read from the colour mask instead of from a fitted
# ellipse, so the two have to agree or one of them is not measuring this.
python3 - /run1423/rows.txt <<'PY'
import sys
rows = open(sys.argv[1]).read().strip().splitlines()
want = {1: 1.63, 2: 1.63, 3: 1.60}      # #1378's table, per rig camera
ok = True
for cam, w in want.items():
    got = [float(dict(kv.split("=",1) for kv in r.split() if "=" in kv)["reach"])
           for r in rows if "rig-20260918/cam_%d" % cam in r]
    if not got:
        print("  rig camera %d: no rows" % cam); ok = False; continue
    m = sum(got)/len(got)
    # 12%: #1378 read its number off a FITTED ELLIPSE's semi-major and this reads it off
    # the mask's own outer edge, so they are two measurements of one thing and not two
    # spellings of one measurement. Anything inside 12% is the same ring.
    good = abs(m - w) / w <= 0.12
    print("  rig camera %d: this slice reads %.3f where #1378 read %.2f -- %s"
          % (cam, m, w, "agrees" if good else "DISAGREES"))
    ok = ok and good
sys.exit(0 if ok else 1)
PY
if [ $? -eq 0 ]; then say "OK   the reach agrees with #1378's fitted-ellipse ratio on all three rig cameras" ok
else say "FAIL the reach disagrees with #1378's fitted-ellipse ratio" no; fi

# And the convergence #1393 and #1416 were pointing at: three cameras 30 cm apart from ONE
# board should measure ONE board radius. The span alone says 195, 196, 197 -- consistent
# and wrong by 1.589. Corrected by the stated identity they should agree AND land near the
# ~316 px doubles semi-axis #1393 fitted.
python3 - /run1423/rows.txt <<'PY'
import sys
rows = [r for r in open(sys.argv[1]).read().strip().splitlines() if "rig-20260918" in r]
b = [float(dict(kv.split("=",1) for kv in r.split() if "=" in kv)["boardRadius"]) for r in rows]
if not b:
    print("  no rig rows"); sys.exit(1)
lo, hi = min(b), max(b)
print("  the rig's three cameras measure a board of %.1f to %.1f px -- a spread of %.1f%%"
      % (lo, hi, 100.0*(hi-lo)/lo))
# #1393 fitted ~316 px on this rig. 10% either side of it, and a spread under 5%.
sys.exit(0 if (hi-lo)/lo <= 0.05 and 0.90*316 <= lo and hi <= 1.10*316 else 1)
PY
if [ $? -eq 0 ]; then say "OK   the rig's three cameras now agree on one board, near #1393's fitted 316 px" ok
else say "FAIL the rig's three cameras do not agree on one board" no; fi

echo
echo "=== 4. the refused method, and the measurement that refused it ================"
# THE RING'S OWN WIDTH was the steer. It does separate these two clusters -- the mocks
# read 0.042 to 0.061 of their span and the rig 0.071 to 0.077, which is close to the
# 0.047 and 0.075 the millimetres predict. What it cannot do is separate them AGAINST A
# CUTOFF NOBODY FITTED: the geometry-derived boundary is the geometric mean of the two,
# sqrt(0.047 * 0.075) = 0.0593, and at least one mocks look sits on the wrong side of it
# -- a doubles ring reading as a treble ring, which is the exact failure this issue is
# about. Buying it would mean choosing a cutoff per rig, and #1322 is entirely about that.
#
# This section is an assertion and not a note, because the argument in ring_identity.hpp
# is only worth the measurement under it: if a later tree makes the width clear that
# boundary on every look, this goes red and the method is worth taking again.
python3 - /run1423/rows.txt <<'PY_W'
import sys, math
rows = open(sys.argv[1]).read().strip().splitlines()
doubles_w, trebles_w = 8.0/170.0, 8.0/107.0
cut = math.sqrt(doubles_w * trebles_w)
w = {"mocks": [], "rig": []}
wrong = []
for r in rows:
    f = dict(kv.split("=",1) for kv in r.split() if "=" in kv)
    rig = "rig-20260918" in r
    x = float(f["widthOfSpan"])
    w["rig" if rig else "mocks"].append(x)
    said = "trebles" if x >= cut else "doubles"
    truth = "trebles" if rig else "doubles"
    if said != truth: wrong.append((r.split()[1], f["look"], x, said))
print("  the ring's own width in spans: mocks %.4f-%.4f, rig %.4f-%.4f  (geometry says %.4f and %.4f)"
      % (min(w["mocks"]), max(w["mocks"]), min(w["rig"]), max(w["rig"]), doubles_w, trebles_w))
print("  against the cutoff the millimetres give, %.4f, it gets %d of %d looks wrong:"
      % (cut, len(wrong), len(rows)))
for c, l, x, said in wrong:
    print("      %s look %s reads %.4f and would be called the %s ring" % (c, l, x, said))
sys.exit(0 if wrong else 1)
PY_W
if [ $? -eq 0 ]; then say "OK   the ring's own width still misclassifies against an underived cutoff, which is why it was not the method taken" ok
else say "FAIL the ring's own width now clears the geometry-derived cutoff on every look -- the refused method is worth re-taking, and ring_identity.hpp's argument for refusing it is stale" no; fi

echo
echo "=== 5. the falsifier: OD_RING=span, on the same binary ========================="
# The state before this issue, reachable at run time: no identity is ever stated and the
# span is taken for the board. A switch that only ever states an identity passes any test
# asking it to state one.
OD_RING=span "$CENSUS" "$SRC/mocks/rig-20260918/cam_1.mp4" 0 1 90 2>/dev/null | grep -E '^I1423' > /run1423/span.txt
cat /run1423/span.txt
if grep -q 'ring=unknown' /run1423/span.txt && grep -q 'OD_RING=span' /run1423/span.txt; then
  say "OK   OD_RING=span states no identity, and says so in its own words" ok
else
  say "FAIL OD_RING=span still states an identity" no
fi
# And the fallback it falls back TO is named rather than silent: the worst case, 1.589.
SPANB=$(sed -n 's/.* boardRadius=\([^ ]*\).*/\1/p' /run1423/span.txt | head -1)
SPANS=$(sed -n 's/.* span=\([^ ]*\).*/\1/p' /run1423/span.txt | head -1)
if python3 -c "import sys; sys.exit(0 if abs($SPANB - $SPANS*1.589) < 1.0 else 1)"; then
  say "OK   an unnamed ring falls back to span x 1.589 -- $SPANS px reads as $SPANB px, which is what every stage below already assumed" ok
else
  say "FAIL an unnamed ring does not fall back to 1.589" no
fi

echo
echo "=== 6. THE MUTATION PROOF: break the rule and watch this file go red ==========="
# A tester that has never been shown to fail is a tester nobody can trust (#1412, #1463).
# The break is the defect itself: a search that never looks outside the measured span
# cannot find the second ring, so every camera reads as the doubles ring -- which is
# exactly the state this issue was filed about.
PLANT=/run1423/planted
rm -rf "$PLANT"; mkdir -p "$PLANT"
cp -r "$SRC/src" "$SRC/testers" "$PLANT/"
sed -i 's/const double kSearchCeiling = 2.1;/const double kSearchCeiling = 1.0;/' \
  "$PLANT/src/detector/geometry/calibration/ring_identity.cpp"
if ! grep -q 'kSearchCeiling = 1.0' "$PLANT/src/detector/geometry/calibration/ring_identity.cpp"; then
  say "FAIL the plant did not land -- kSearchCeiling was not where this proof expects it, so nothing below proves anything" no
else
  if ! build_census "$PLANT" /run1423/planted_census; then
    tail -20 /run1423/planted_census.build.log
    say "FAIL the planted tree did not build, so the mutation proves nothing" no
  else
    collect /run1423/planted_census /run1423/planted_rows.txt
    P_RIG_T=$(grep 'rig-20260918' /run1423/planted_rows.txt | grep -c 'ring=trebles')
    P_RIG_N=$(grep -c 'rig-20260918' /run1423/planted_rows.txt)
    echo "    with the search ceiling at 1.0 span, the rig reads: $(grep 'rig-20260918' /run1423/planted_rows.txt | sed -n 's/.* ring=\([a-z]*\).*/\1/p' | sort | uniq -c | tr '\n' ' ')"
    if [ "$P_RIG_T" -eq 0 ]; then
      say "OK   the mutation is fatal: 0 of $P_RIG_N rig looks name the treble ring, where this tree names $RIG_T of $RIG_ROWS -- section 1 goes red on it" ok
    else
      say "FAIL the mutation changed nothing: $P_RIG_T of $P_RIG_N rig looks still name the treble ring, so section 1 cannot fail and measures nothing" no
    fi
  fi
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1423: PASS"; else echo "i1423: FAIL"; fi
exit $FAILED

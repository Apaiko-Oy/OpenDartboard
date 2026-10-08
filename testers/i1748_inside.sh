#!/bin/bash
# #1748, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1748.
#
# Five sections. 1 is the control (rig-20260929's camera 2, the physical camera the live
# log of 2026-10-08 numbered 1, unpainted); 2 is the planted frame -- the same frame with
# board red painted at 1.30..1.45 spans so the ring identity reads TREBLE the way it did
# live -- and asserts the plane is still built at x1.0 and accepted; 3 is the pure check
# of the fit's refusal on the live log's own figures; 4 and 5 are the mutation proofs:
# the cross-check removed (criterion 1 must refuse the planted frame alone, by name),
# then both rules removed, which is 0fb2ca3, where the planted frame is accepted at
# x1.589 with a fitted ring 60-odd mm from its wire -- section 2's red.
#
# It never ends on an `echo`: #1463. This one ends on `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

OUT=/run1748
SRC=/app
mkdir -p "$OUT"

CLIP=/app/mocks/rig-20260929/cam_2.mp4
CAMIDX=1     # cam_2: the physical camera the live log numbers 1 (bull (646,232), board 332 px)
FRAME=90     # 3 s, where the dev build's own calibration seeks

build_census() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -I "$1/src" -I "$1/src/utils" -I "$1/src/detector/geometry/calibration" \
    -o "$2" "$1/testers/i1748_scale_census.cpp" \
    "$1"/src/detector/geometry/calibration/*.cpp \
    "$1"/src/detector/geometry/detection/score_processing.cpp \
    "$1"/src/detector/geometry/detection/dart_processing.cpp \
    "$1"/src/detector/geometry/detection/motion_processing.cpp \
    $(pkg-config --cflags --libs opencv4) > "$2.build.log" 2>&1
}
build_check() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -I "$1/src" -I "$1/src/utils" \
    -o "$2" "$1/testers/i1748_scale_check.cpp" \
    "$1/src/detector/geometry/calibration/wire_model.cpp" \
    $(pkg-config --cflags --libs opencv4) > "$2.build.log" 2>&1
}

field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<< "$1" | head -1; }
# A float comparison without python: awk.
lt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a+0 < b+0) }'; }
ge() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a+0 >= b+0) }'; }
near() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= t) }'; }

echo "=== building the census and the check against this tree ======================"
if ! build_census "$SRC" "$OUT/census"; then
  tail -30 "$OUT/census.build.log"
  echo "FAIL the scale census did not build; nothing below measures anything"
  exit 2
fi
if ! build_check "$SRC" "$OUT/check"; then
  tail -30 "$OUT/check.build.log"
  echo "FAIL the scale check did not build; nothing below measures anything"
  exit 2
fi

census() { # $1 binary, $2 plant, $3 log  -- runs in its own dir: the calibration writes debug files
  local d="$OUT/dbg_$(basename "$1")_$2"
  mkdir -p "$d" && cd "$d" || return 2
  "$1" "$CLIP" "$CAMIDX" "$FRAME" "$2" "$OUT/frame_$2.jpg" > "$3" 2>&1
  local rc=$?
  cd / || return 2
  return $rc
}

echo
echo "=== 1. the control: rig-20260929 cam_2 at frame $FRAME, unpainted ==============="
census "$OUT/census" none "$OUT/control.log"
grep -E '^I1748' "$OUT/control.log"
CTRL=$(grep '^I1748 ' "$OUT/control.log" | head -1)
C_ID=$(field "$CTRL" identity); C_CONIC=$(field "$CTRL" conicOfDoubles); C_ACC=$(field "$CTRL" geometryAccepted)
C_SPAN=$(field "$CTRL" span); C_WF=$(field "$CTRL" worstFittedMm)
if [ "$C_ID" = doubles ] && [ "$C_CONIC" = 1.0000 ] && [ "$C_ACC" = 1 ] && lt "$C_WF" 8; then
  say "OK   the control reads the DOUBLES ring, builds at x1.0 and is accepted, worst fitted residual $C_WF mm" ok
else
  say "FAIL the control is not clean (identity=$C_ID conicOfDoubles=$C_CONIC geometryAccepted=$C_ACC worstFittedMm=$C_WF), so nothing below is a comparison" no
fi

echo
echo "=== 2. the planted frame: red at 1.30..1.45 spans, the live fault rebuilt =========="
census "$OUT/census" arc "$OUT/planted.log"
grep -E '^I1748' "$OUT/planted.log"
grep -E 'ring identity|wire model: the|board model' "$OUT/planted.log" | sed 's/^/    /' | cut -c1-400
PLANT=$(grep '^I1748 ' "$OUT/planted.log" | head -1)
P_ID=$(field "$PLANT" identity); P_REACH=$(field "$PLANT" reach); P_SPAN=$(field "$PLANT" span)
P_CONIC=$(field "$PLANT" conicOfDoubles); P_ACC=$(field "$PLANT" geometryAccepted)
P_WF=$(field "$PLANT" worstFittedMm); P_WH=$(field "$PLANT" worstHeldOutMm)
# The plant must LAND, or the sections below measure a clean frame twice.
if [ "$P_ID" = trebles ] && ge "$P_REACH" 1.2605; then
  say "OK   the plant landed: the identity reads TREBLE at a reach of $P_REACH spans (band 1.2605..2.003), as it did live" ok
else
  say "FAIL the plant did not land: identity=$P_ID reach=$P_REACH -- the painted red did not reach the identity, so nothing below measures the fault" no
fi
if near "$P_SPAN" "$C_SPAN" 10; then
  say "OK   the span is still the doubles ring: $P_SPAN px painted against $C_SPAN px unpainted" ok
else
  say "FAIL the paint moved the span ($P_SPAN px against $C_SPAN px), so the frame is not the live fault" no
fi
# Criterion 2: the identity is cross-checked where it is spent. Five rings in band
# against the trace at x1.0 -- a treble ring INSIDE the traced ring -- means the trace is
# the doubles ring, whatever lies outside it.
if [ "$P_CONIC" = 1.0000 ]; then
  say "OK   the plane is built at x1.0: the treble ring inside the trace outweighs the colour outside it" ok
else
  say "FAIL the plane is built at x$P_CONIC of the traced doubles ring" no
fi
if grep -q 'treble ring inside it' "$OUT/planted.log" && grep -q 'built at 1.0' "$OUT/planted.log"; then
  say "OK   the WARN names both readings and which won: $(grep -o 'wire model: the fitted conic.*' "$OUT/planted.log" | head -1 | cut -c1-260)..." ok
else
  say "FAIL no WARN names the two readings against each other" no
fi
if [ "$P_ACC" = 1 ] && lt "$P_WF" 8 && lt "$P_WH" 8; then
  say "OK   and the fit is accepted at residuals the control would show: worst fitted $P_WF mm, worst held-out $P_WH mm" ok
else
  say "FAIL the planted frame's fit: geometryAccepted=$P_ACC worstFittedMm=$P_WF worstHeldOutMm=$P_WH" no
fi

echo
echo "=== 3. the fit refuses a scale its own fitted ring contradicts (the live figures) ==="
"$OUT/check" | tee "$OUT/check.log" | sed 's/^/    /'
if [ "${PIPESTATUS[0]}" = 0 ]; then
  say "OK   the pure check passes: the live camera 1 figures at x1.589 are REJECTED by name; cameras 2/3 and rig-20260929 at x1.0 are accepted" ok
else
  say "FAIL the pure check failed: $(grep -c '^FAIL' "$OUT/check.log") assertion(s)" no
fi

echo
echo "=== 4. MUTATION A: the cross-check removed -- the refusal must catch the frame alone ="
# Criterion 1 is the guard that stays even if criterion 2 is wrong (the issue's order),
# so with the cross-check planted out the planted frame builds at x1.589 again and the
# fit must refuse it by name. This is the refusal sentence from a replay of the planted
# frame that acceptance criterion 1 asks for.
PLANT_A=$OUT/plant_a
rm -rf "$PLANT_A"; mkdir -p "$PLANT_A"
cp -r "$SRC/src" "$SRC/testers" "$PLANT_A/"
sed -i 's/const bool trebleInsideTheTrace = ellipse_processing::trebleRingInsideTheTrace(calib.ellipses);/const bool trebleInsideTheTrace = false;/' \
  "$PLANT_A/src/detector/geometry/calibration/wire_processing.cpp"
if ! grep -q 'const bool trebleInsideTheTrace = false;' "$PLANT_A/src/detector/geometry/calibration/wire_processing.cpp"; then
  say "FAIL plant A did not land -- the cross-check is not where this proof expects it" no
elif ! build_census "$PLANT_A" "$OUT/census_a"; then
  tail -20 "$OUT/census_a.build.log"
  say "FAIL plant A did not build, so the mutation proves nothing" no
else
  census "$OUT/census_a" arc "$OUT/planted_a.log"
  grep -E '^I1748 |^I1748STORY' "$OUT/planted_a.log" | cut -c1-600
  A=$(grep '^I1748 ' "$OUT/planted_a.log" | head -1)
  A_CONIC=$(field "$A" conicOfDoubles); A_ACC=$(field "$A" geometryAccepted); A_WF=$(field "$A" worstFittedMm)
  if [ "$A_CONIC" != 1.0000 ] && [ "$A_ACC" = 0 ] && ge "$A_WF" 50 && grep -q 'further than that ring is wide' "$OUT/planted_a.log"; then
    say "OK   without the cross-check the plane is built at x$A_CONIC, the fitted ring sits $A_WF mm from its wire, and the fit is REJECTED by name:" ok
    grep -o 'REJECTED: .*' "$OUT/planted_a.log" | head -1 | cut -c1-400 | sed 's/^/       /'
  else
    say "FAIL without the cross-check: conicOfDoubles=$A_CONIC geometryAccepted=$A_ACC worstFittedMm=$A_WF -- the refusal did not catch the frame on its own" no
  fi
fi

echo
echo "=== 5. MUTATION B: both rules removed, which is 0fb2ca3 -- section 2 goes red ======"
PLANT_B=$OUT/plant_b
rm -rf "$PLANT_B"; mkdir -p "$PLANT_B"
cp -r "$PLANT_A/src" "$PLANT_A/testers" "$PLANT_B/"
sed -i 's/return profile.outerDoubleRadiusMm - profile.innerDoubleRadiusMm;/return 1e9;/' \
  "$PLANT_B/src/detector/geometry/calibration/board_model.hpp"
if ! grep -q 'return 1e9;' "$PLANT_B/src/detector/geometry/calibration/board_model.hpp"; then
  say "FAIL plant B did not land -- the bound is not where this proof expects it" no
elif ! build_census "$PLANT_B" "$OUT/census_b" || ! build_check "$PLANT_B" "$OUT/check_b"; then
  tail -20 "$OUT/census_b.build.log" "$OUT/check_b.build.log"
  say "FAIL plant B did not build, so the mutation proves nothing" no
else
  census "$OUT/census_b" arc "$OUT/planted_b.log"
  grep -E '^I1748 ' "$OUT/planted_b.log" | cut -c1-400
  B=$(grep '^I1748 ' "$OUT/planted_b.log" | head -1)
  B_CONIC=$(field "$B" conicOfDoubles); B_ACC=$(field "$B" geometryAccepted); B_WF=$(field "$B" worstFittedMm)
  if [ "$B_CONIC" != 1.0000 ] && [ "$B_ACC" = 1 ] && ge "$B_WF" 50; then
    say "OK   the mutation is fatal: with both rules out the planted frame is ACCEPTED at x$B_CONIC with its fitted ring $B_WF mm from its wire -- the live fault, and section 2's red" ok
  else
    say "FAIL with both rules out: conicOfDoubles=$B_CONIC geometryAccepted=$B_ACC worstFittedMm=$B_WF -- section 2 could not have failed on this, so it measures nothing" no
  fi
  "$OUT/check_b" > "$OUT/check_b.log" 2>&1
  if [ $? -ne 0 ] && grep -q '^FAIL' "$OUT/check_b.log"; then
    say "OK   and the pure check goes red on it: $(grep -c '^FAIL' "$OUT/check_b.log") assertion(s), first: $(grep -m1 '^FAIL' "$OUT/check_b.log" | cut -c1-160)" ok
  else
    say "FAIL the pure check still passes with the bound removed, so it measures nothing" no
  fi
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1748: PASS"; else echo "i1748: FAIL"; fi
exit $FAILED

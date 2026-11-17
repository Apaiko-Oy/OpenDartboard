#!/bin/bash
# turnaus#2003, inside the container: the club's Harrows board, off the board's own
# start-up pictures (testers/fixtures2003/), read by calibrateSingleCamera through
# i2003_still_census.cpp. One compile, the same binary run three ways:
#
#   unset              the tree's rule: a camera refused at the ring is read a second time
#   OD_RED_KEY=first   the pin: one reading, as keyed -- the detector before #2003. It is
#                      the CONTROL, and it must stay red: a camera 1 that calibrates as
#                      keyed means the pictures no longer hold the fault.
#   OD_RED_KEY=always  the dark red set aside on every first reading, measured and printed,
#                      asserted only for camera 1
#
#   i2003_inside.sh [tree-root]     default /app; the mutation proof hands it a copy of
#                                   the tree with the fix taken out
#
# What is asserted, by label:
#
#   pictures    the three pictures are there
#   control     under the pin camera 1 is refused, and refused at the ring: the doubles
#               ring it traced is under half the span of its own colour
#   camera 1    unset, camera 1 calibrates -- sees=1, twenty wires, the plane trusted, the
#               board model accepted -- and its bull is within 5 px of (698,297), where the
#               afternoon starts that sealed it put it
#   said        ... and the log says a second reading was taken, once, about camera 1
#   untouched   cameras 2 and 3 calibrate, are not read twice, and their rows are byte for
#               byte the pin's: a camera that calibrates as keyed is measured as it was
#   always      camera 1 calibrates with the dark red set aside from the start too
#   twice       a picture both readings refuse (the census's `middle`: camera 1's still
#               with everything but the middle of the board painted black) is refused at
#               the ring, and its log unset is the pin's line for line at DEBUG -- a second
#               reading that does not calibrate says nothing and leaves one refusal
set -u

SRC="${1:-/app}"
OUT=/run2003
FIX=/app/testers/fixtures2003
mkdir -p "$OUT"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<< "$1" | head -1; }

echo "=== building the still census against $SRC ==================================="
if ! g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
    -o "$OUT/census" /app/testers/i2003_still_census.cpp \
    "$SRC"/src/detector/geometry/calibration/*.cpp \
    "$SRC"/src/detector/geometry/detection/score_processing.cpp \
    "$SRC"/src/detector/geometry/detection/dart_processing.cpp \
    "$SRC"/src/detector/geometry/detection/motion_processing.cpp \
    $(pkg-config --cflags --libs opencv4) -lpthread > "$OUT/census.build.log" 2>&1; then
  tail -30 "$OUT/census.build.log"
  echo "FAIL the still census did not build; nothing below measures anything"
  exit 2
fi

# $1 the rule's name, $2 OD_RED_KEY's value ("" leaves it unset), $3 0-based camera
read_camera() {
  local d="$OUT/$1_cam$3"
  mkdir -p "$d" && cd "$d" || return 2
  if [ -n "$2" ]; then
    OD_RED_KEY="$2" "$OUT/census" "$FIX/camera_$3_background.jpg" "$3" > "$OUT/$1_cam$3.log" 2>&1
  else
    env -u OD_RED_KEY "$OUT/census" "$FIX/camera_$3_background.jpg" "$3" > "$OUT/$1_cam$3.log" 2>&1
  fi
  local rc=$?
  cd / || return 2
  return $rc
}
row() { grep -a "^I2003 " "$OUT/$1_cam$2.log" | head -1; }
second_readings() { grep -a -c "read a second time with the dark red set aside" "$OUT/$1_cam$2.log"; }

echo
echo "=== pictures ================================================================="
for i in 0 1 2; do
  if [ -s "$FIX/camera_${i}_background.jpg" ]; then
    say "OK   pictures camera_${i}_background.jpg is there ($(stat -c %s "$FIX/camera_${i}_background.jpg") bytes)" ok
  else
    say "FAIL pictures camera_${i}_background.jpg is missing from $FIX" bad
  fi
done

for rule in unset first always; do
  key="$rule"; [ "$rule" = unset ] && key=""
  for i in 0 1 2; do
    read_camera "$rule" "$key" "$i" || say "FAIL the census exited non-zero under $rule on camera $((i + 1)); see $OUT/${rule}_cam$i.log" bad
    echo "$rule: $(row "$rule" "$i")"
  done
done

echo
echo "=== control: OD_RED_KEY=first, the detector before #2003 ======================"
R=$(row first 0)
SEES=$(field "$R" sees); SPAN=$(field "$R" span); CONIC=$(field "$R" conic)
if [ "$SEES" = 0 ]; then
  say "OK   control camera 1 is refused as keyed (sees=0, R=$(field "$R" R), $(field "$R" candidates) wire candidates)" ok
else
  say "FAIL control camera 1 calibrates as keyed (sees=$SEES), so these pictures no longer hold the fault and nothing below proves anything" bad
fi
if awk -v c="$CONIC" -v s="$SPAN" 'BEGIN { exit !(s + 0 > 0 && c + 0 < 0.5 * s) }'; then
  say "OK   control ... and at the ring: the doubles ring it traced reaches $CONIC px where its colour spans $SPAN px" ok
else
  say "FAIL control camera 1's traced ring is $CONIC px on a span of $SPAN px, which is not under half of it: the refusal is not the ring's" bad
fi

echo
echo "=== camera 1, unset ==========================================================="
R=$(row unset 0)
if [ "$(field "$R" sees)" = 1 ] && [ "$(field "$R" whole)" = 1 ] && [ "$(field "$R" trusted)" = 1 ] &&
   [ "$(field "$R" geometryAccepted)" = 1 ]; then
  say "OK   camera 1 calibrates: R=$(field "$R" R) over $(field "$R" candidates) candidates, bull $(field "$R" bull), residuals $(field "$R" worstFittedMm) mm fitted and $(field "$R" worstHeldOutMm) mm held out" ok
else
  say "FAIL camera 1 did not calibrate on the club's Harrows board: sees=$(field "$R" sees) whole=$(field "$R" whole) trusted=$(field "$R" trusted) geometryAccepted=$(field "$R" geometryAccepted) R=$(field "$R" R) conic=$(field "$R" conic) span=$(field "$R" span)" bad
fi
BULL=$(field "$R" bull)
BX=$(sed -n 's/^(\([0-9-]*\),.*/\1/p' <<< "$BULL"); BY=$(sed -n 's/^([0-9-]*,\([0-9-]*\))$/\1/p' <<< "$BULL")
if awk -v x="${BX:-0}" -v y="${BY:-0}" 'BEGIN { dx = x - 698; dy = y - 297; exit !(dx * dx + dy * dy <= 25) }'; then
  say "OK   camera 1 bull $BULL is within 5 px of (698,297)" ok
else
  say "FAIL camera 1 bull $BULL is more than 5 px from (698,297), where the starts that sealed this camera put it" bad
fi
N=$(second_readings unset 0)
if [ "$N" = 1 ]; then
  say "OK   said the log says once that camera 1 was read a second time:" ok
  grep -a "read a second time with the dark red set aside" "$OUT/unset_cam0.log" | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-600
else
  say "FAIL said the log names a second reading of camera 1 $N time(s), not once" bad
fi

echo
echo "=== cameras 2 and 3 are measured as they were ================================"
for i in 1 2; do
  U=$(row unset "$i"); P=$(row first "$i")
  if [ "$(field "$U" sees)" = 1 ] && [ "$(field "$U" trusted)" = 1 ]; then
    say "OK   untouched camera $((i + 1)) calibrates: R=$(field "$U" R), bull $(field "$U" bull), residuals $(field "$U" worstFittedMm) mm fitted and $(field "$U" worstHeldOutMm) mm held out" ok
  else
    say "FAIL untouched camera $((i + 1)) did not calibrate: sees=$(field "$U" sees) trusted=$(field "$U" trusted) R=$(field "$U" R)" bad
  fi
  if [ -n "$U" ] && [ "$U" = "$P" ] && [ "$(second_readings unset "$i")" = 0 ]; then
    say "OK   untouched camera $((i + 1))'s row is the pin's byte for byte and it was read once" ok
  else
    say "FAIL untouched camera $((i + 1)) is not what the pin reads, or was read twice ($(second_readings unset "$i") second readings):" bad
    echo "       unset: $U"
    echo "       first: $P"
  fi
done

echo
echo "=== OD_RED_KEY=always, measured ==============================================="
R=$(row always 0)
if [ "$(field "$R" sees)" = 1 ] && [ "$(field "$R" trusted)" = 1 ]; then
  say "OK   always camera 1 calibrates with the dark red set aside from the start: R=$(field "$R" R), bull $(field "$R" bull)" ok
else
  say "FAIL always camera 1 did not calibrate with the dark red set aside from the start: sees=$(field "$R" sees) R=$(field "$R" R)" bad
fi
for i in 1 2; do
  R=$(row always "$i")
  echo "     always camera $((i + 1)) (reported, not asserted): sees=$(field "$R" sees) R=$(field "$R" R) bull $(field "$R" bull) against R=$(field "$(row first "$i")" R) as keyed"
done

echo
echo "=== a picture both readings refuse ==========================================="
mkdir -p "$OUT/twice" && cd "$OUT/twice" || exit 2
env -u OD_RED_KEY "$OUT/census" "$FIX/camera_0_background.jpg" 0 middle 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/twice_unset.txt"
OD_RED_KEY=first "$OUT/census" "$FIX/camera_0_background.jpg" 0 middle 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/twice_first.txt"
cd / || exit 2
R=$(grep -a "^I2003 " "$OUT/twice_first.txt" | head -1)
RING=$(grep -a -c "did not calibrate: the doubles ring could not be fitted" "$OUT/twice_first.txt")
if [ "$(field "$R" sees)" = 0 ] && [ "$RING" = 1 ]; then
  say "OK   twice the middle of camera 1's board alone is refused at the ring as keyed: $(grep -a -o '120 rays traced, [0-9]* gave a boundary point' "$OUT/twice_first.txt" | head -1)" ok
else
  say "FAIL twice the middle of camera 1's board alone is not refused at the ring as keyed (sees=$(field "$R" sees), $RING ring refusals), so no second reading is taken of it and the rest of this section proves nothing" bad
fi
LINES=$(wc -l < "$OUT/twice_unset.txt")
DIFFER=$(diff "$OUT/twice_first.txt" "$OUT/twice_unset.txt" | grep -c '^[<>]')
REFUSALS=$(grep -a -c "did not calibrate" "$OUT/twice_unset.txt")
if [ "$LINES" -gt 10 ] && [ "$DIFFER" = 0 ] && [ "$REFUSALS" = 1 ]; then
  say "OK   twice unset it is refused too, in the pin's $LINES lines at DEBUG with none differing and one refusal" ok
else
  say "FAIL twice unset, the twice-refused picture's log is $LINES lines with $DIFFER differing from the pin's and $REFUSALS refusals (wanted: none differing, one refusal):" bad
  diff "$OUT/twice_first.txt" "$OUT/twice_unset.txt" | cut -c1-240 | head -12
fi

echo
if [ "$FAILED" = 0 ]; then
  echo "I2003 PASS"
else
  echo "I2003 FAIL"
fi
exit $FAILED

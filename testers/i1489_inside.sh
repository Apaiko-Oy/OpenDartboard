#!/bin/bash
# #1489, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1489.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
CHECK=/run1489/wedge_check

build_check() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -o "$2" "$1/testers/i1489_wedge_check.cpp" \
    "$1"/src/detector/geometry/calibration/wire_processing.cpp \
    "$1"/src/detector/geometry/calibration/wire_model.cpp \
    "$1"/src/detector/geometry/calibration/perspective_processing.cpp \
    "$1"/src/detector/geometry/detection/score_processing.cpp \
    -I"$1/src" -I"$1/src/utils" \
    -I"$1/src/detector/geometry/calibration" -I"$1/src/detector/geometry/detection" \
    $(pkg-config --cflags --libs opencv4) -lpthread > "$2.build.log" 2>&1
}

echo "=== building the wedge check ==="
if ! build_check "$SRC" "$CHECK"; then
  tail -30 "$CHECK.build.log"
  echo "FAIL the wedge check did not build; nothing below measures anything"
  exit 2
fi

echo
echo "=== 1-2. the vote and the reading, this tree's decision ========================"
"$CHECK" > /run1489/plain.txt 2>&1
PLAIN_RC=$?
cat /run1489/plain.txt
if [ "$PLAIN_RC" = 0 ]; then
  say "OK   every assertion in the pure check holds on this tree" ok
else
  say "FAIL the pure check answered rc=$PLAIN_RC on this tree" no
fi

echo
echo "=== 3. THE FALSIFIER: OD_RING_ONLY=counted, on the same binary ================="
# The state before this issue, reachable at run time: a ring-only reading counts as a
# camera that measured a wedge. A distinction that can only ever be drawn cannot be shown
# to be doing anything, and the check asserts the OTHER answers under the switch -- so
# this is one binary asked two questions, not one question asked twice.
OD_RING_ONLY=counted "$CHECK" > /run1489/counted.txt 2>&1
COUNTED_RC=$?
cat /run1489/counted.txt
if [ "$COUNTED_RC" = 0 ]; then
  say "OK   and the switch really puts the pre-#1489 reading back: every assertion about that state holds too" ok
else
  say "FAIL OD_RING_ONLY=counted does not produce the state this issue is about" no
fi
# The two runs must not be the same run. Asserted rather than assumed: if the switch were
# read nowhere, both files would be identical and both would be green.
if diff -q /run1489/plain.txt /run1489/counted.txt > /dev/null; then
  say "FAIL the switch changed nothing -- the two runs are identical, so neither measures the other" no
else
  say "OK   the two runs differ, so the switch reaches the decision it names" ok
fi

echo
echo "=== 4. the detector itself, over the whole of mocks/rig-20260918 ==============="
# The census above holds the DECISION; this holds the PROGRAM, on the fixture the issue
# was measured on. OD_MAX_CYCLES is high enough to reach the end of the footage: #1484's
# agent measured all 60 s played in 62 s over 1694 cycles, and an earlier run capped at
# 1200 reached four of seven visits and had its figures quoted as the clip's.
#
# THE RIG AS THIS BRANCH SCORES IT HAS NO BULL AND NO OUTER IN IT. #1485 held every ring
# ellipse to the board the doubles ring says this is, and the eight 25s the issue counted
# went to zero -- they were darts on the board judged against a 25 ring fitted 3.6x to
# 10.4x too large. So the fixture is played BOTH ways here: OD_RINGS=asfitted is #1485's
# own falsifier and puts those eight back, which is the state #1489 was measured in, and
# the plain run is the tree as it scores today.
run_detector() { # $1 tag, rest: env
  local tag="$1"; shift
  rm -rf "/run1489/$tag"; mkdir -p "/run1489/$tag"; cd "/run1489/$tag"
  env "$@" OD_MAX_CYCLES=6000 /app/build/opendartboard \
    --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
    --width 1280 --height 720 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "/run1489/$tag.log"
  cd /
}

# The census this issue is judged by, off one run's console output. Prints the four
# figures and leaves the arithmetic to the caller.
census() { # $1 log
  python3 - "$1" <<'PY'
import re, sys
log = open(sys.argv[1], errors="replace").read().splitlines()
SCORE = re.compile(r"SCORE:\s+(\S+)\s+\|\s+Position:.*Confidence:\s+([0-9.]+)")
BOARD = re.compile(r"BOARD:\s+(wedge measured|wedge by default|wedge not in this reading)\s+\|\s+ring=(\S*)")
darts, board = [], None
for line in log:
    m = BOARD.search(line)
    if m:
        board = m.group(1)
        continue
    m = SCORE.search(line)
    if m:
        if m.group(1) == "END":
            board = None
            continue
        darts.append((m.group(1), float(m.group(2)), board))
        board = None
def n(pred):
    return sum(1 for d in darts if pred(d))
high = lambda d: d[1] > 0.6
print("DARTS=%d" % len(darts))
print("MEASURED=%d" % n(lambda d: d[2] == "wedge measured"))
print("BYDEFAULT=%d" % n(lambda d: d[2] == "wedge by default"))
print("RINGONLY=%d" % n(lambda d: d[2] == "wedge not in this reading"))
print("HIGH=%d" % n(high))
print("HIGH_RINGONLY=%d" % n(lambda d: high(d) and d[2] == "wedge not in this reading"))
print("HIGH_CLAIMING_A_WEDGE=%d" % n(lambda d: high(d) and d[2] != "wedge not in this reading"))
print("OUTER=%d" % n(lambda d: d[0] == "OUTER"))
print("BULL=%d" % n(lambda d: d[0] == "BULL"))
print("ENDED=%s" % ("end-of-footage" if any("END OF FOOTAGE:" in l for l in log) else "not-end-of-footage"))
PY
}

if [ ! -x /app/build/opendartboard ]; then
  say "FAIL /app/build/opendartboard is not here, so the program half of this measures nothing" no
else
  for tag in asfitted-after asfitted-before held; do
    case $tag in
      asfitted-after)  run_detector "$tag" OD_RINGS=asfitted OD_RING_ONLY= ;;
      asfitted-before) run_detector "$tag" OD_RINGS=asfitted OD_RING_ONLY=counted ;;
      held)            run_detector "$tag" OD_RINGS= OD_RING_ONLY= ;;
    esac
    census "/run1489/$tag.log" > "/run1489/$tag.census"
    echo "  --- $tag ---"
    sed 's/^/      /' "/run1489/$tag.census"
    # Each run's figures, under its own prefix: asfitted_after_HIGH, held_DARTS, ...
    eval "$(sed "s/^/${tag//-/_}_/" "/run1489/$tag.census")"
  done

  # Every run must have played the whole clip, or the figures are of its opening.
  if [ "$asfitted_after_ENDED" = end-of-footage ] && [ "$asfitted_before_ENDED" = end-of-footage ] &&
     [ "$held_ENDED" = end-of-footage ]; then
    say "OK   all three runs ended where the FOOTAGE ended, so every figure is of the whole clip" ok
  else
    say "FAIL a run stopped before the end of the footage, so its figures are of an opening and not of this fixture" no
  fi

  # The precondition the whole section rests on: the fixture really holds the reading this
  # issue is about. #708's rule -- the needle is proved to exist before its absence means
  # anything -- and it is not hypothetical here, because with the rings HELD there is no
  # 25 in this clip at all and every assertion below would pass over nothing.
  if [ "$asfitted_after_OUTER" -gt 0 ]; then
    say "OK   with OD_RINGS=asfitted the fixture publishes $asfitted_after_OUTER darts as a 25, so there is a ring-only reading here to be counted" ok
  else
    say "FAIL OD_RINGS=asfitted publishes no 25 at all, so nothing below is measuring this issue" no
  fi

  # THE DEFECT, on the same binary, in one line: darts published at 0.7 or 0.9 -- the two
  # confidences that mean a camera MEASURED A WEDGE -- in a run where no wedge was measured.
  echo "      before: $asfitted_before_HIGH darts at 0.7 or 0.9, $asfitted_before_HIGH_CLAIMING_A_WEDGE of them by a camera that read no wedge"
  echo "      after:  $asfitted_after_HIGH darts at 0.7 or 0.9, $asfitted_after_HIGH_CLAIMING_A_WEDGE of them by a camera that read no wedge"
  if [ "$asfitted_before_HIGH_CLAIMING_A_WEDGE" -gt 0 ]; then
    say "OK   before #1489 $asfitted_before_HIGH_CLAIMING_A_WEDGE darts at 0.7 or 0.9 carry a board line claiming a wedge nobody read" ok
  else
    say "FAIL the falsifier shows no such dart, so the after figure below is a census of nothing" no
  fi
  if [ "$asfitted_after_HIGH_CLAIMING_A_WEDGE" = 0 ] && [ "$asfitted_after_HIGH_RINGONLY" -gt 0 ]; then
    say "OK   after it, all $asfitted_after_HIGH_RINGONLY of them say the wedge was no part of the reading -- and they are still published at 0.7 and 0.9" ok
  else
    say "FAIL a dart at 0.7 or 0.9 still claims a wedge, or the ring readings stopped reaching a consensus" no
  fi

  # The issue's own criterion: zero wedges measured, on a fixture where every BOARD line
  # already said so. Asked of all three runs, because it is a fact about the rig (no camera
  # on it is anchored) and not about this repair.
  if [ "$asfitted_after_MEASURED" = 0 ] && [ "$asfitted_before_MEASURED" = 0 ] && [ "$held_MEASURED" = 0 ]; then
    say "OK   not one dart in any of the three runs had a wedge measured -- no camera on this rig is anchored" ok
  else
    say "FAIL a wedge was measured on a rig where no camera can be read for one" no
  fi

  # And the tree as it scores today: nineteen darts, every one of them an asserted 20 at
  # 0.5, nothing at 0.7 or 0.9 at all. This is the figure #1484's census reports, and after
  # this issue it moves with the anchor and with nothing else.
  echo "      held:   $held_DARTS darts, $held_HIGH at 0.7 or 0.9, $held_BYDEFAULT asserted, $held_RINGONLY ring-only"
  if [ "$held_DARTS" -gt 0 ] && [ "$held_HIGH" = 0 ] && [ "$held_RINGONLY" = 0 ] && [ "$held_OUTER" = 0 ]; then
    say "OK   with the rings held this fixture has no ring-only reading in it and publishes nothing above 0.5" ok
  else
    say "FAIL the fixture as this branch scores it is not the run this issue's figures were taken from" no
  fi
fi

echo
echo "=== 5. THE MUTATION PROOF: delete the distinction at its source ================"
# A tester that has never been shown to fail is a tester nobody can trust (#1412, #1463).
# Two plants, one per half, each reached through the SOURCE rather than through the switch
# section 3 uses -- so the two are not one measurement written twice.
plant() { # $1 tag, $2 file under src/, $3 sed expression, $4 what must be there afterwards
  local tag="$1" file="$2" expr="$3" want="$4"
  local dir="/run1489/planted-$tag"
  rm -rf "$dir"; mkdir -p "$dir"
  cp -r "$SRC/src" "$SRC/testers" "$dir/"
  sed -i "$expr" "$dir/$file"
  if ! grep -q "$want" "$dir/$file"; then
    say "FAIL the $tag plant did not land -- the line was not where this proof expects it, so it proves nothing" no
    return
  fi
  if ! build_check "$dir" "/run1489/planted_$tag"; then
    tail -20 "/run1489/planted_$tag.build.log"
    say "FAIL the $tag plant did not build, so the mutation proves nothing" no
    return
  fi
  "/run1489/planted_$tag" > "/run1489/planted_$tag.txt" 2>&1
  local rc=$? n
  n=$(grep -c '^FAIL ' "/run1489/planted_$tag.txt" || true)
  echo "    $tag: the planted tree answered rc=$rc with $n failed assertions"
  grep '^FAIL ' "/run1489/planted_$tag.txt" | head -3 | sed 's/^/      | /'
  if [ "$rc" != 0 ] && [ "$n" -gt 0 ]; then
    say "OK   the $tag mutation is fatal: the check goes red on it" ok
  else
    say "FAIL the $tag mutation changed nothing, so the assertions it should break measure nothing" no
  fi
}

# The READING: the scorer stops stating that a bull is scored by the ring alone.
plant reading src/detector/geometry/detection/score_processing.cpp \
  's|out.ring_only = on_bull \&\& !ringOnlyReadingsCountAsMeasured();|out.ring_only = false;|' \
  'out.ring_only = false;'

# The VOTE: the choice stops carrying what its cameras agreed about. Section 4 cannot see
# this one -- the BOARD line is the reading's own word -- which is why there are two.
plant vote src/detector/geometry/detection/score_processing.hpp \
  's|out.ring_only = points\[out.camera\].ring_only;|out.ring_only = false;|' \
  'out.ring_only = false;'

echo
if [ "$FAILED" = 0 ]; then echo "i1489: PASS"; else echo "i1489: FAIL"; fi
exit $FAILED

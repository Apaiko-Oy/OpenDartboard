#!/bin/bash
# #1498, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1498.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

PROBE=/run1498/probe
SRC=/app

echo "=== building the number-anchor census ==========================================="
g++ -std=c++17 -O2 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$PROBE" "$SRC/testers/i1498_number_anchor.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  $(pkg-config --cflags --libs opencv4) > /run1498/build.log 2>&1
if [ $? -ne 0 ]; then
  tail -40 /run1498/build.log
  echo "FAIL the census did not build; nothing below it measures anything"
  exit 2
fi

# One run of the probe. $1 names the run, $2 the fixture directory, the rest is the
# environment the SAME BINARY is given -- which is the whole of the difference between a
# measurement and its controls (#1339's shape, and OD_ANCHOR=own's convention).
probe() {
  local name="$1" dir="$2"; shift 2
  mkdir -p "/run1498/$name"
  # THE SHIPPED CUT IS LEFT ALONE. A separation is printed whatever the cut is, so the
  # sweep below reads the numbers themselves, and `read=` then says what the cut this
  # binary really ships with made of each one -- which is the half that can be asserted.
  env "$@" "$PROBE" "/run1498/$name" \
    "$dir/cam_1.mp4" "$dir/cam_2.mp4" "$dir/cam_3.mp4" \
    > "/run1498/$name.rows.txt" 2> "/run1498/$name.log"
  local rc=$?
  if [ $rc -ne 0 ]; then
    tail -20 "/run1498/$name.log"
    say "FAIL $name: the probe exited $rc" no
  fi
  return $rc
}

field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<< "$1"; }

# `grep -c` prints 0 AND exits 1 when it matches nothing, so `$(grep -c ... || echo 0)`
# -- which is what this file had, and what read as a passing check reporting "believed 0
# 0" -- yields TWO lines. One of them then falls out of the comparison as a word of its
# own. Counted here once, in the one place that can be wrong.
count() { local n; n=$(grep -c "$1" "$2" 2>/dev/null); echo "${n:-0}"; }

RIG="$SRC/mocks/rig-20260918"
MOCKS="$SRC/mocks"

echo
echo "=== 1. the number ring, both fixtures =========================================="
probe rig "$RIG"
probe mocks "$MOCKS"
grep -h '^I1498CAM \|^I1498WHY ' /run1498/rig.rows.txt /run1498/mocks.rows.txt 2>/dev/null

echo
echo "=== 2. the same reader on a ring with no numbers in it ========================="
echo "  OD_NUMBER_ANCHOR=inner moves the annulus to the big singles -- between the treble"
echo "  ring (107 mm) and the doubles (162.5 mm) -- and changes nothing else. It is the"
echo "  only thing that can tell 'the numbers were read' from 'twenty cells of anything"
echo "  score a rotation'."
probe null-rig "$RIG" OD_NUMBER_ANCHOR=inner
probe null-mocks "$MOCKS" OD_NUMBER_ANCHOR=inner
grep -h '^I1498CAM ' /run1498/null-rig.rows.txt /run1498/null-mocks.rows.txt 2>/dev/null

echo
echo "=== 3. the sweep the cut came from ============================================="
python3 - <<'PY'
import re, sys

def rows(path):
    out = []
    try:
        for line in open(path):
            if line.startswith('I1498CAM '):
                d = dict(p.split('=', 1) for p in line.split()[1:])
                out.append(d)
    except FileNotFoundError:
        pass
    return out

real = rows('/run1498/rig.rows.txt') + rows('/run1498/mocks.rows.txt')
null = rows('/run1498/null-rig.rows.txt') + rows('/run1498/null-mocks.rows.txt')
if not real or not null:
    print('  no rows to sweep')
    sys.exit(0)

rs = sorted(float(r['separation']) for r in real)
ns = sorted(float(r['separation']) for r in null)
print('  number ring    : ' + ' '.join('%.2f' % v for v in rs))
print('  numberless ring: ' + ' '.join('%.2f' % v for v in ns))
print()
print('   cut    number rings refused    numberless rings admitted')
for cut in [1.5, 2.0, 2.5, 2.75, 3.0, 3.25, 3.5, 4.0]:
    refused = sum(1 for v in rs if v < cut)
    admitted = sum(1 for v in ns if v >= cut)
    print('   %-5.2f  %d/%-20d %d/%d' % (cut, refused, len(rs), admitted, len(ns)))
PY

echo
echo "=== 4. what the reader must say, and what it must not =========================="
# Every assertion below is STRUCTURAL. Not one of them says which wire the 20 is at on
# either fixture: a tester that stated the answer would pass on a reader that had been
# fitted to these two clips, which is the thing #1322 refuses.
READ_ALL=1
for FIX in rig mocks; do
  ATT=$(count 'attempted=1' "/run1498/$FIX.rows.txt")
  CELLS=$(count '^I1498CELL ' "/run1498/$FIX.rows.txt")
  if [ "$ATT" = 3 ] && [ "$CELLS" = 60 ]; then
    say "OK   $FIX: every camera's number ring was cut into twenty cells and read" ok
  else
    say "FAIL $FIX: $ATT of 3 cameras were read and $CELLS of 60 cells came out" no
    READ_ALL=0
  fi
  # AT LEAST ONE, deliberately, and not three. "A camera on this fixture reports anchored
  # from the printed numbers" is the claim; "all three of them do, on these two clips" is
  # a number off this footage, and asserting it would make the harness go red the first
  # time somebody films the board in worse light -- which is the case the fallback exists
  # for and not a defect.
  HOWMANY=$(count ' read=1' "/run1498/$FIX.rows.txt")
  if [ "$HOWMANY" -ge 1 ]; then
    say "OK   $FIX: $HOWMANY of 3 cameras cleared the shipped cut and are anchored by the board itself" ok
  else
    say "FAIL $FIX: no camera cleared the shipped cut, so nothing on this fixture is anchored by its numbers" no
  fi
done

# The direction that MATTERS is the other one: a ring with nothing printed in it must
# never be believed, whatever it scores, because a confidently wrong anchor is every dart
# of a run on the wrong wedge.
for FIX in null-rig null-mocks; do
  BELIEVED=$(count ' read=1' "/run1498/$FIX.rows.txt")
  if [ "$BELIEVED" = 0 ]; then
    say "OK   $FIX: the shipped cut believed none of the three numberless rings" ok
  else
    say "FAIL $FIX: the shipped cut believed $BELIEVED numberless ring(s) -- it is admitting chance" no
  fi
done

# The control must score LOWER than the measurement on every camera of its own fixture.
# This is the mutation proof in the form it can take here: the same binary, the same
# frames, the same twenty cells, moved to a ring with nothing printed in it.
python3 - <<'PY'
import sys
def sep(path):
    out = {}
    try:
        for line in open(path):
            if line.startswith('I1498CAM '):
                d = dict(p.split('=', 1) for p in line.split()[1:])
                out[d['cam']] = float(d['separation'])
    except FileNotFoundError:
        pass
    return out
bad = 0
for fix in ('rig', 'mocks'):
    real, null = sep('/run1498/%s.rows.txt' % fix), sep('/run1498/null-%s.rows.txt' % fix)
    for cam in sorted(real):
        if cam not in null:
            print('FAIL %s cam %s: the control did not run' % (fix, cam)); bad = 1; continue
        if real[cam] > null[cam]:
            print('OK   %s cam %s: the number ring reads %.2f where the numberless ring reads %.2f'
                  % (fix, cam, real[cam], null[cam]))
        else:
            print('FAIL %s cam %s: the number ring reads %.2f and the numberless ring reads %.2f '
                  '-- this reader is not reading numbers' % (fix, cam, real[cam], null[cam]))
            bad = 1
sys.exit(bad)
PY
[ $? -eq 0 ] || FAILED=1

echo
echo "=== 5. beside the clip wires, and disagreement is a finding ====================="
# mocks/cam_2 is the ONE camera in this repository that anchors itself from four clip
# wires. It is therefore the only independent check of the reader that exists here, and
# it is an independent one: the clips are a different part of the board from the numbers.
STAR=$(grep '^I1498CAM ' /run1498/mocks.rows.txt 2>/dev/null | grep 'starAnchored=1' | head -1)
# The reader has already run by the time a camera reaches this census, so a camera it
# anchored carries ITS OWN index -- see the probe's own note. `starWedge20` is the clip
# wires' answer and is -1 on every camera they did not anchor, which is what makes this
# comparison an independent one rather than the reader agreeing with itself.
if [ -z "$STAR" ]; then
  say "FAIL the shipped mocks anchored no camera from its clip wires, so there is nothing to agree with" no
else
  SW=$(field "$STAR" starWedge20)
  RW=$(field "$STAR" readWedge20)
  CAM=$(field "$STAR" cam)
  if [ "$SW" = "$RW" ]; then
    say "OK   mocks cam $CAM: the clip wires put the 20 at wire $SW and the printed numbers put it at wire $RW" ok
  else
    say "FAIL mocks cam $CAM: the clip wires say wire $SW and the printed numbers say wire $RW" no
  fi
fi

echo
echo "=== 6. the falsifier: the same binary, not asked ==============================="
probe off-rig "$RIG" OD_NUMBER_ANCHOR=off
OFF=$(count 'attempted=0' /run1498/off-rig.rows.txt)
if [ "$OFF" = 3 ]; then
  say "OK   OD_NUMBER_ANCHOR=off: no camera's numbers were read, on the binary that can read them" ok
else
  say "FAIL OD_NUMBER_ANCHOR=off: $OFF of 3 cameras refused to read; the falsifier does not falsify" no
fi

echo
echo "=== what a reader would be handed =============================================="
echo "  /run1498/<fixture>/cam<N>/read.png   the twenty cells with the number each was read as"
echo "  /run1498/<fixture>.rows.txt          every row this census printed"

echo
if [ "$FAILED" = 0 ]; then echo "i1498: PASS"; else echo "i1498: FAIL"; fi
exit $FAILED

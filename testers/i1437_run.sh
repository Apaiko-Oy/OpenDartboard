#!/bin/bash
# #1437's harness: two halves, because they live at two altitudes and neither can measure
# the other.
#
#   testers/run_all.sh 1437        build the tree and run this
#   testers/i1437_run.sh           run it against whatever is in build/
#   testers/i1437_run.sh census    only the half that reads files
#   testers/i1437_run.sh fixture   only the half that runs the detector
#
# CENSUS runs testers/i1437_fixture_census.py over this tree and asserts it EMPTY -- and
# then runs it again over a copy of the tree with #1416's own shape planted in it, a
# fixture-wide measurement naming cam_1 and cam_3 of the rig and not cam_2, and asserts it
# names that file by line. That second run is the whole point: a census that has never
# been shown to fail is a census nobody can trust. It reads files and builds nothing, but
# it runs in a container all the same (#1607): both runs used to be a HOST `python3`, and
# the box that runs the suite has none -- the Windows Store stub answers "Python ei
# loytynyt", rc=49, and both halves then read 49. The tree goes in at /app and the planted
# copy at /run1437/planted, read-only; the same file with the same argument, only its
# paths are the container's. #1560's rule: the IMAGE has an interpreter, the HOST may not.
# The BUSY=/WALL= lines below are still host python and stay so: a failed substitution
# costs them an empty field and not a verdict (#1588).
#
# FIXTURE runs the real binary and asks what each fixture ANSWERS for, which is the half a
# file census cannot reach. Its own mutation proof is inside it (phase B).
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job. Every number it prints belongs to a build carrying DEBUG_SEEK_VIDEO, which seeks a
# file source three seconds in: a release binary calibrates on a different frame of the
# same clip and holds a different stretch of it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
WHICH="${1:-both}"
BASE="$OD_RUNS_BASE/1437"
RUN="$BASE/fixture"
mkdir -p "$BASE"
# Reaped by the exact name before it starts: an interrupted run leaves the container
# alive, --rm never fires, and the next run then dies on the name rather than on what it
# measures.
docker rm -f "$(od_name i1437-fixture)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "i1437-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/fixture > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

RC=0

if [ "$WHICH" = both ] || [ "$WHICH" = all ] || [ "$WHICH" = census ]; then
  echo "=================================================================="
  echo "=== the census, over this tree ==="
  od_run "i1437-census" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" \
    python3 testers/i1437_fixture_census.py /app < /dev/null
  C=$?
  echo
  echo "=== the same census, over a copy of this tree with #1416's shape planted ==="
  # The plant is the measurement this issue was found by: a ring-identity census across
  # the rig fixture that names two of its three clips. It goes in a new file rather than
  # into an existing tester, because what the census reads is one file's own references
  # and a plant spread over two would be measuring something else.
  PLANT="$RUN/planted"
  rm -rf "$PLANT"; mkdir -p "$PLANT/testers" "$PLANT/mocks"
  cp -r "$OD_TREE_ROOT/testers" "$PLANT/" 2>/dev/null
  # The clips themselves are 87 MB and this census reads only their NAMES, so the copy
  # gets empty files with the right ones.
  for d in "$OD_TREE_ROOT"/mocks/*/; do mkdir -p "$PLANT/mocks/$(basename "$d")"; done
  ( cd "$OD_TREE_ROOT/mocks" && find . -name 'cam_*.mp4' ) | while read -r c; do
    mkdir -p "$PLANT/mocks/$(dirname "$c")"; : > "$PLANT/mocks/$c"
  done
  # fixture-subset-exempt: the two clip names below are the PLANT, not a measurement --
  # this harness writes #1416's shape into a scratch copy so the census can be shown to
  # catch it. The census caught this file the first time it was run, which is the census
  # working: it reads what a file SAYS, and this file says cam_1 and cam_3. Every other
  # file in this tree that names two clips of a fixture is measuring them.
  cat > "$PLANT/testers/i1416_ring_identity.sh" <<'PLANTED'
# A ring-identity census across both fixtures, written the way #1416's was: it measures
# the rig and names two of the three clips the rig holds. The figure it publishes -- `rig
# doubles 27/27, --, 35/35` -- is one camera short and says so with a dash rather than
# with a failure.
for c in /app/mocks/rig-20260918/cam_1.mp4 /app/mocks/rig-20260918/cam_3.mp4; do
  measure "$c"
done
PLANTED
  od_run "i1437-census-planted" --network none -v "$OD_TREE_ROOT":/app:ro \
    -v "$PLANT":/run1437/planted:ro -w /app "$OD_IMAGE" \
    python3 testers/i1437_fixture_census.py /run1437/planted < /dev/null
  P=$?
  echo
  if [ "$C" = 0 ]; then echo "OK   the census is empty on this tree"
  else echo "FAIL the census is not empty on this tree"; RC=1; fi
  if [ "$P" != 0 ]; then echo "OK   the planted subset is caught, by file and line"
  else echo "FAIL the planted subset passed, so this census cannot fail"; RC=1; fi
fi

if [ "$WHICH" = both ] || [ "$WHICH" = all ] || [ "$WHICH" = fixture ]; then
  echo "=================================================================="
  echo "=== what each fixture answers for, from the real binary ==="
  if [ ! -x "$OD_TREE_ROOT/build/opendartboard" ]; then
    echo "FAIL no binary at $OD_TREE_ROOT/build/opendartboard; run_all builds it" >&2
    exit 2
  fi
  cp "$OD_TREE_ROOT/testers/phases1437/1437-fixture.sh" "$RUN/inside.sh"

  read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
  T0=$(date +%s.%N)

  od_run "i1437-fixture" --cpus=2 --network none -e HOME=/root \
    -v "$OD_TREE_ROOT":/app \
    -v "$RUN":/run1437 -v "$RUN/cfg":/root/.config \
    -w /run1437 "$OD_IMAGE" bash /run1437/inside.sh
  F=$?
  [ "$F" = 0 ] || RC=1

  T1=$(date +%s.%N)
  read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat
  BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
  WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")
  echo "RUN=fixture rc=$F wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
fi

exit $RC

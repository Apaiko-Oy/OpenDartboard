set -u
# #1330: the guarantee, the refusal it makes, and the board still calibrating under it.
#
# Four phases, in the order a reader needs them:
#
#   1. The guarantee holds, and the file round-trips. testers/i1330_cache_round_trip.cpp
#      writes three calibrations, reads them back field for field, and fires each of the
#      header's refusals with a control beside it.
#
#   2. The guarantee REFUSES the old shape. A check that cannot be made to fail is not a
#      check (#1317's sentence, and this issue is the second half of it), so #1321's
#      std::string is put back into EllipseBoundaryData in a scratch copy of the tree and
#      the same translation unit is compiled against it. The build must fail, and it must
#      fail by NAME -- on the static_assert under DartboardCalibration, not on some
#      unrelated error that happens to be red.
#
#   3. A pointer is really what used to go in the file. This one is compiled against the
#      commit this issue was filed against, unpacked into $RUN/pre-1330 by the harness --
#      not against the scratch tree of phase 2, because phase 2's tree still carries the
#      new assert and therefore will not build at all, which is phase 2's result. The old
#      struct is given a reason longer than the small-string buffer and asked what fwrite
#      would have copied: the bytes at the string's offset, read back as an address.
#
#   4. The mock footage still calibrates, with no new ERROR or WARN; a second start in the
#      same directory looks at the board AGAIN, because inheriting is not the default; and
#      a third with --reuse-calibration reads the file the first one wrote. That third run
#      is what the commented-out load() call has been withholding since before #1317, and
#      the second one is why it is not simply uncommented.

RUN=/run1330
SRC=/app
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

INC="-I$SRC/src -I$SRC/src/utils -I$SRC/src/detector/geometry/calibration -I$SRC/src/detector/geometry/detection"
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

echo "=== 1. the round trip, and the header's refusals ==="
mkdir -p $RUN/roundtrip
g++ -std=c++17 -O1 -o $RUN/round_trip $SRC/testers/i1330_cache_round_trip.cpp $INC $CVFLAGS -lpthread || exit 1
( cd $RUN/roundtrip && $RUN/round_trip ) > $RUN/round_trip.out 2>&1
RC=$?
sed 's/\x1b\[[0-9;]*m//g' $RUN/round_trip.out | grep -E '^(OK|FAIL|sizeof|ALL|FAILURES)' || true
if [ "$RC" = "0" ]; then say "OK   the round trip and every header refusal pass" ok
else say "FAIL i1330_cache_round_trip exited $RC" no; fi

echo
echo "=== 2. put #1321's std::string back, and watch the build refuse it ==="
# A scratch copy of the tree: the worktree is not touched.
rm -rf $RUN/old-shape
mkdir -p $RUN/old-shape
cp -r $SRC/src $RUN/old-shape/src
cp -r $SRC/testers $RUN/old-shape/testers
python3 - <<'PY'
p = '/run1330/old-shape/src/detector/geometry/calibration/ellipse_processing.hpp'
s = open(p).read()
anchor = '        // CONTOUR FITTING RESULTS (triples & bull rings - efficient)'
assert anchor in s, 'anchor for the old shape not found'
s = s.replace(anchor, '        std::string doublesFailure; // #1321, put back exactly where it was\n\n' + anchor, 1)
open(p, 'w').write(s)
print('the old shape is back in the scratch copy')
PY

OLDINC="-I$RUN/old-shape/src -I$RUN/old-shape/src/utils -I$RUN/old-shape/src/detector/geometry/calibration -I$RUN/old-shape/src/detector/geometry/detection"
g++ -std=c++17 -O1 -fsyntax-only $RUN/old-shape/testers/i1330_cache_round_trip.cpp $OLDINC $CVFLAGS \
  > $RUN/old-shape.out 2>&1
OLD_RC=$?
echo "--- what the compiler said (first 12 lines) ---"
head -12 $RUN/old-shape.out
if [ "$OLD_RC" != "0" ]; then say "OK   the build refuses the old shape (g++ exited $OLD_RC)" ok
else say "FAIL the old shape compiled: the guarantee cannot fail, so it is not a guarantee" no; fi
if grep -q 'static assertion failed' $RUN/old-shape.out && \
   grep -q 'nothing in it may own memory' $RUN/old-shape.out; then
  say "OK   it refuses BY NAME, on the ownership assert, quoting what to do instead" ok
else say "FAIL the build failed for some other reason than the ownership assert" no; fi

echo
echo "=== 3. what used to go in the file, at the offset it went in at ==="
PRE="$RUN/pre-1330"
PREINC="-I$PRE/src -I$PRE/src/utils -I$PRE/src/detector/geometry/calibration -I$PRE/src/detector/geometry/detection"
if [ ! -f "$PRE/src/detector/geometry/calibration/ellipse_processing.hpp" ]; then
  say "FAIL the pre-#1330 source was not unpacked into $PRE by the harness" no
fi
cat > $RUN/pointer_proof.cpp <<'CPP'
// The old shape, asked what the cache's fwrite would have copied. Not a simulation: this
// is compiled against the commit the issue was filed against, with #1321's string in it,
// and the bytes read are the bytes fwrite reads.
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include "geometry_calibration.hpp"

int main()
{
    DartboardCalibration calib;
    calib.ellipses.doublesFailure =
        "the doubles mask holds 341 white pixels and this stage needs at least 1000 -- a "
        "frame this dark keys almost nothing as dartboard red or green";

    const size_t offset = (const char *)&calib.ellipses.doublesFailure - (const char *)&calib;
    const char *bytes = (const char *)&calib;

    void *first_word = nullptr;
    memcpy(&first_word, bytes + offset, sizeof(first_word));

    printf("reason length          = %zu characters\n", calib.ellipses.doublesFailure.size());
    printf("offset in the struct   = %zu bytes into the %zu the cache fwrites\n", offset, sizeof(calib));
    printf("first word written     = %p\n", first_word);
    printf("the string's own data  = %p\n", (const void *)calib.ellipses.doublesFailure.data());
    printf("heap? (not inside this struct) = %s\n",
           (first_word == (void *)calib.ellipses.doublesFailure.data() &&
            (first_word < (void *)bytes || first_word >= (void *)(bytes + sizeof(calib))))
               ? "YES -- the file would have got an address, not the words"
               : "no");

    // And the short one, which is the half that makes this so hard to see.
    DartboardCalibration shortly;
    shortly.ellipses.doublesFailure = "no doubles mask";
    const char *short_bytes = (const char *)&shortly;
    void *short_first = nullptr;
    memcpy(&short_first, short_bytes + offset, sizeof(short_first));
    printf("a 15-character reason: first word written = %p, inside the struct = %s\n",
           short_first,
           (short_first >= (void *)short_bytes && short_first < (void *)(short_bytes + sizeof(shortly)))
               ? "YES -- this one survives by accident, through the small-string buffer"
               : "no");
    return 0;
}
CPP
g++ -std=c++17 -O1 -o $RUN/pointer_proof $RUN/pointer_proof.cpp $PREINC $CVFLAGS -lpthread \
  > $RUN/pointer_proof.build 2>&1
if [ $? = 0 ]; then
  $RUN/pointer_proof | tee $RUN/pointer_proof.out
  if grep -q 'YES -- the file would have got an address' $RUN/pointer_proof.out && \
     grep -q 'YES -- this one survives by accident' $RUN/pointer_proof.out; then
    say "OK   the long reason was a heap pointer on disk and the short one was not" ok
  else say "FAIL the pointer proof did not show both halves" no; fi
else
  head -5 $RUN/pointer_proof.build
  say "FAIL the pointer proof did not build" no
fi

echo
echo "=== 4. the mocks still calibrate, the default does not inherit, and --reuse-calibration does ==="
mkdir -p $RUN/mocks
MOCKS=$SRC/mocks/cam_1.mp4,$SRC/mocks/cam_2.mp4,$SRC/mocks/cam_3.mp4
run_detector() {
  ( cd $RUN/mocks && OD_MAX_CYCLES=20 $SRC/build/opendartboard --cams $MOCKS --width 1280 --height 720 "$@" ) \
    > $RUN/$RUNNAME.out 2>&1
  echo "${RUNNAME}_RC=$?"
  sed 's/\x1b\[[0-9;]*m//g' $RUN/$RUNNAME.out > $RUN/$RUNNAME.txt
}
RUNNAME=first  run_detector
RUNNAME=second run_detector
RUNNAME=third  run_detector --reuse-calibration

echo "--- ERROR and WARNING in the first run ---"
grep -E '^\[(ERROR|WARNING|WARN)\]' $RUN/first.txt || echo "(none)"
FIRST_BAD=$(grep -cE '^\[(ERROR|WARNING|WARN)\]' $RUN/first.txt || true)
if [ "$FIRST_BAD" = "0" ]; then say "OK   the mocks calibrate with no ERROR and no WARN" ok
else say "FAIL $FIRST_BAD ERROR/WARN lines on the mock footage" no; fi

if [ -f $RUN/mocks/cache/geometry_calibration.dat ]; then
  say "OK   the first run wrote cache/geometry_calibration.dat ($(stat -c%s $RUN/mocks/cache/geometry_calibration.dat) bytes)" ok
else say "FAIL no calibration file was written" no; fi

# The default. This is the check that would go red if somebody made the read unconditional,
# which is what five starts of i1318 in one directory did while #1330 was being written.
if grep -q 'Performing immediate calibration' $RUN/second.txt && \
   ! grep -q 'Using the cached calibration' $RUN/second.txt; then
  say "OK   a second start in the same directory looks at the board again, by default" ok
else say "FAIL the second start inherited the first one's calibration without being asked" no; fi

echo "--- what the third run, with --reuse-calibration, said ---"
grep -E 'cached calibration|Loaded .* calibrations|Performing immediate calibration' $RUN/third.txt || echo "(nothing)"
if grep -q 'Using the cached calibration' $RUN/third.txt; then
  say "OK   --reuse-calibration reads the file the first run wrote -- load() is live" ok
else say "FAIL --reuse-calibration did not read the cache: the file is still write-only" no; fi
if grep -q 'Performing immediate calibration' $RUN/third.txt; then
  say "FAIL it calibrated anyway" no
else say "OK   and skipped the eight-and-a-half-second calibration doing it" ok; fi
if grep -q 'did not look at the board' $RUN/third.txt; then
  say "OK   it says on that start that it did not look at the picture" ok
else say "FAIL a cached start does not say that it did not look" no; fi
THIRD_BAD=$(grep -cE '^\[(ERROR|WARNING|WARN)\]' $RUN/third.txt || true)
if [ "$THIRD_BAD" = "0" ]; then say "OK   and says nothing at ERROR or WARN doing it" ok
else grep -E '^\[(ERROR|WARNING|WARN)\]' $RUN/third.txt | head -5; say "FAIL $THIRD_BAD ERROR/WARN lines in the cached start" no; fi

echo
if [ "$FAILED" = "0" ]; then echo "1330 PHASE: PASS"; else echo "1330 PHASE: FAIL"; fi
exit $FAILED

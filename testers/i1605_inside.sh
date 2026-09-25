set -u
# #1605, inside the container: rig-20260922's camera 1 in the dev window, why #1445's
# twelve looks never reached a frame it could calibrate on, and that the longer budget
# does. That budget was opt-in (OD_LOOK_BUDGET=1605) while admitting the camera cost the
# window accuracy; since #1631 it is the DEFAULT and OD_LOOK_BUDGET=12 is the pin that
# restores #1445's twelve (the account is at kFurtherLooksPastAStandingDart in
# geometry_detector.cpp). #1631 swapped which run is which and asserted the same things:
# B and C read the default run (was the opt-in), D reads the pinned run (was the
# default), and E compares the default with the pin (was the opt-in with the default).
#
# THE FINDING. The first failing stage is the BULL, and the fault is on the frames, not in
# a gate. Visit 1's second dart (the 16, v1.2) arrives at f64 and its barrel stands
# straight down through camera 1's bull -- the annotated shaft runs x=652..654 from y=103
# to y=364 (testers/i1511_annotations/rig-20260922.csv) and the bull is at (653,302) --
# until visit 1 is pulled at f203-241. A bull cut in half by a barrel is two half-bulls,
# and the bull stage picks one: (640,302) or (667,302), either side of (653,302), at
# 0.072-0.078 of the board where a bull is 0.093. The 25 ring refusal, R=0.577558 on the
# wire model and the treble ring traced as the board all follow from that centre. The
# averaged frame (f90-119) and all twelve looks (f124-f179) are inside the dart's stay;
# the first clean frame is ~f244. The parked 8 (v1.1) is in the opening window too, where
# camera 1 calibrates at R=0.873, so it is not the cause.
#
# WHAT IS ASSERTED
#
#   A  THE CENSUS, prediction first: #1445's own look census on that clip, from its dev
#      seek, at the tree's spacing. The averaged frame is refused, a run of consecutive
#      looks is refused, and then the camera calibrates. The default budget (#1445's) does
#      not outlast the run -- the defect -- and the default budget (#1605's) does.
#   B  THE DEFAULT BOARD, rig-20260922 dev: camera 1 calibrates on the
#      look the census predicts, SCORING 3 of 3, every look is before the one seal
#      (ADR-0080 section 2), and the background is re-taken after the looks.
#   C  ITS FIGURES ARE THE OPENING WINDOW'S: coherence no more than 0.05 below the
#      opening's, every ring where the board puts it, bull within 3 px of the opening's.
#   D  THE PIN OD_LOOK_BUDGET=12 is the old default: camera 1 set aside after twelve
#      looks, SCORING 2 of 3, and no background re-taken.
#   E  NOTHING ELSE MOVES: rig-20260918 in both windows and rig-20260922's opening give
#      byte-identical calibration transcripts under the default and the pin, rig-20260918
#      takes no look at all, and none of them re-takes its background. The longer budget
#      is only ever spent past look twelve, and only on a camera still refused there.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RUN=/run1605
SRC=/app/src/detector/geometry/geometry_detector.cpp
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: testers/run_all.sh 1605"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

PINNED="$(grep -oE 'constexpr int kFurtherLooks = [0-9]+' $SRC | grep -oE '[0-9]+$')"
BUDGET="$(grep -oE 'constexpr int kFurtherLooksPastAStandingDart = [0-9]+' $SRC | grep -oE '[0-9]+$')"
SPACING="$(grep -oE 'constexpr int kFramesBetweenLooks = [0-9]+' $SRC | grep -oE '[0-9]+$')"
if [ -z "$BUDGET" ] || [ -z "$PINNED" ] || [ -z "$SPACING" ]; then
  echo "FAIL could not read the look budget out of $SRC"
  exit 2
fi
echo "the budget this tree holds: $BUDGET further looks by default, $PINNED under the pin OD_LOOK_BUDGET=12, $SPACING capture cycles apart"

echo
echo "---- A. the look census: rig-20260922/cam_1 from its dev seek ----"
echo "     PREDICTED: the averaged frame and a run of looks are refused while the 16's barrel"
echo "     stands through the bull, the run is longer than $PINNED and shorter than $BUDGET, and"
echo "     every look after it calibrates."
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o $RUN/look_census /app/testers/i1445_look_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > $RUN/build_census.log 2>&1 || { tail -20 $RUN/build_census.log; echo "FAIL the look census did not build"; exit 2; }
WINDOW=40
$RUN/look_census /app/mocks/rig-20260922/cam_1.mp4 0 $WINDOW "$SPACING" 30 2>/dev/null | grep '^I1445' > $RUN/census.txt
AVG_SEES="$(sed -n 's/.*look=avg .*sees=\([01]\).*/\1/p' $RUN/census.txt | head -1)"
# look number, frame index it read (at = frames consumed, so the frame is at-1), verdict
awk '/ look=[0-9]+ /{ for(i=1;i<=NF;i++){ split($i,kv,"="); v[kv[1]]=kv[2] } print v["look"], v["at"]-1, v["sees"] }' \
  $RUN/census.txt > $RUN/census.looks
RUN_REFUSED="$(awk '{ if(!done && $3==0) r++; else done=1 } END{print r+0}' $RUN/census.looks)"
FIRST_OK="$(awk '$3==1{print $1; exit}' $RUN/census.looks)"
FIRST_OK_AT="$(awk '$3==1{print $2; exit}' $RUN/census.looks)"
AFTER_BAD="$(awk -v f="${FIRST_OK:-999}" '$1>f && $3==0' $RUN/census.looks | wc -l)"
LOOKS_TAKEN="$(wc -l < $RUN/census.looks)"
echo "     averaged frame sees=$AVG_SEES; looks 1..$RUN_REFUSED refused (last at f$(awk -v r="$RUN_REFUSED" '$1==r{print $2}' $RUN/census.looks));"
echo "     first calibrating look ${FIRST_OK:-none} at f${FIRST_OK_AT:-none}; refused after it: $AFTER_BAD of $((LOOKS_TAKEN - ${FIRST_OK:-0}))"
[ "$AVG_SEES" = 0 ]; note $? "the averaged frame (f90-119) is refused, as on main"
[ -n "$FIRST_OK" ] && [ "$RUN_REFUSED" -ge "$PINNED" ]
note $? "#1445's $PINNED looks do not outlast the refused run ($RUN_REFUSED) -- the defect, measured"
[ -n "$FIRST_OK" ] && [ "$BUDGET" -gt "$RUN_REFUSED" ]
note $? "the default $BUDGET looks do (first calibrating look ${FIRST_OK:-none})"
[ "$AFTER_BAD" = 0 ]; note $? "once the board is clear every look calibrates -- the refusal was the dart's, not the camera's"

# ---- the detector runs -------------------------------------------------------------------
transcript() {
  sed 's/\x1b\[[0-9;]*m//g' "$1" |
    grep -E '\[(GEOMETRY_CALIBRATION|WIRE_PROCESSING|ELLIPSE_PROCESSING|MASK_PROCESSING|ORIENTATION_PROCESSING|GEOMETRYDETECTOR)\]|CAPANCHOR|DEBUG_SEEK_VIDEO' |
    sed -E 's/ host_return_us=[0-9]+ anchor_us=[0-9]+//'
}
# The same transcript with the one sentence the pin is ALLOWED to change -- the budget
# printed in the LOOK AGAIN lines, and its own warning -- taken out.
comparable() {
  transcript "$1" | grep -v 'OD_LOOK_BUDGET=12 is set' |
    sed -E 's/Up to [0-9]+ further looks/Up to N further looks/; s/on look ([0-9]+) of [0-9]+/on look \1 of N/'
}
calibrate_once() { # $1 fixture dir, $2 out file, $3.. extra env
  local dir="$1" out="$2"; shift 2
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=1 "$@" timeout 1200 $BIN \
      --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" \
      --width 1280 --height 720 > "$out" 2>&1 )
  return $?
}
R22=/app/mocks/rig-20260922
R18=/app/mocks/rig-20260918
# #1631: the unmarked run is the default (31 looks); "-12" is the pin OD_LOOK_BUDGET=12.
calibrate_once $R22 $RUN/r22-dev.out;                                    RC1=$?
calibrate_once $R22 $RUN/r22-dev-12.out    OD_LOOK_BUDGET=12;            RC2=$?
calibrate_once $R22 $RUN/r22-open.out      OD_SEEK_VIDEO=off;            RC3=$?
calibrate_once $R22 $RUN/r22-open-12.out   OD_SEEK_VIDEO=off OD_LOOK_BUDGET=12; RC4=$?
calibrate_once $R18 $RUN/r18-dev.out;                                    RC5=$?
calibrate_once $R18 $RUN/r18-dev-12.out    OD_LOOK_BUDGET=12;            RC6=$?
calibrate_once $R18 $RUN/r18-open.out      OD_SEEK_VIDEO=off;            RC7=$?
calibrate_once $R18 $RUN/r18-open-12.out   OD_SEEK_VIDEO=off OD_LOOK_BUDGET=12; RC8=$?
for f in r22-dev r22-dev-12 r22-open r22-open-12 r18-dev r18-dev-12 r18-open r18-open-12; do
  transcript $RUN/$f.out > $RUN/$f.cal
done
[ "$RC1$RC2$RC3$RC4$RC5$RC6$RC7$RC8" = 00000000 ]
note $? "all eight calibration runs ended cleanly (rc $RC1 $RC2 $RC3 $RC4 $RC5 $RC6 $RC7 $RC8)"
grep -q "DEBUG_SEEK_VIDEO: video 1 seeked forward" $RUN/r22-dev.cal
note $? "the binary carries the dev seek, so the dev window is the one measured"

scoring_of() { grep -oE 'SCORING: [0-9]+ of [0-9]+' "$1" | head -1; }
seal_cam1() { grep -oE 'GEOMETRY SEALED: camera 1 index=0 scoring=[01] bull=[0-9]+,[0-9]+' "$1" | head -1; }

echo
echo "---- B. rig-20260922, dev window, the default budget ----"
echo "     PREDICTED: camera 1 calibrates on look ${FIRST_OK:-?} of $BUDGET (the census's first clean look), SCORING: 3 of 3"
D=$RUN/r22-dev.cal
LOOKED="$(grep -oE 'LOOK AGAIN: camera 1 calibrated on look [0-9]+ of [0-9]+' $D | head -1)"
echo "     measured:  ${LOOKED:-no such line}; $(scoring_of $D); $(seal_cam1 $D)"
! grep -q "OD_LOOK_BUDGET=12 is set" $D; note $? "the default run is not pinned"
[ "$LOOKED" = "LOOK AGAIN: camera 1 calibrated on look ${FIRST_OK:-x} of $BUDGET" ]
note $? "camera 1 calibrated on exactly the look the census predicted"
[ "$(scoring_of $D)" = "SCORING: 3 of 3" ]; note $? "and the board scores with 3 of 3 cameras"
seal_cam1 $D | grep -q 'scoring=1'; note $? "camera 1 is in the seal, scoring"
SEALS="$(grep -c 'GEOMETRY SEALED:' $D)"
SL="$(grep -n 'GEOMETRY SEALED:' $D | head -1 | cut -d: -f1)"
LAST="$(grep -n 'LOOK AGAIN' $D | tail -1 | cut -d: -f1)"
[ "$SEALS" = 1 ] && [ -n "$SL" ] && [ -n "$LAST" ] && [ "$LAST" -lt "$SL" ]
note $? "one seal, and every look before it (ADR-0080 section 2): last LOOK AGAIN line $LAST, seal line $SL"
grep -q "its background is re-taken" $D; note $? "the looks ran past $PINNED, so the background was re-taken after them"

echo
echo "---- C. camera 1's dev-window fit against its opening-window fit ----"
# The ACCEPTED camera-1 lines: the last of each kind, which belong to the calibrating look.
cam1_last() { grep -E "$2" "$1" | tail -1; }
WD="$(cam1_last $D 'Camera 1 wire model: [0-9]+ candidates')"
WO="$(cam1_last $RUN/r22-open.cal 'Camera 1 wire model: [0-9]+ candidates')"
RD="$(echo "$WD" | sed -n 's/.*coherence R=\([0-9.]*\).*/\1/p')"
RO="$(echo "$WO" | sed -n 's/.*coherence R=\([0-9.]*\).*/\1/p')"
BD="$(cam1_last $D 'Camera 1 bull at' | sed -n 's/.*bull at (\([0-9]*\),\([0-9]*\)).*/\1 \2/p')"
BO="$(cam1_last $RUN/r22-open.cal 'Camera 1 bull at' | sed -n 's/.*bull at (\([0-9]*\),\([0-9]*\)).*/\1 \2/p')"
RINGD="$(cam1_last $D 'Camera 1 rings:')"
MODELD="$(cam1_last $D 'Camera 1 board model')"
echo "     dev:     R=${RD:-none} bull=(${BD:-none}) ${RINGD#*- }"
echo "     opening: R=${RO:-none} bull=(${BO:-none})"
echo "     dev board model: ${MODELD#*- }" | cut -c1-400
echo "     the bulls camera 1 read while refused, default run: $(grep -oE 'Camera 1 bull at \([0-9]+,[0-9]+\)' $RUN/r22-dev.cal | grep -oE '\([0-9]+,[0-9]+\)' | sort | uniq -c | tr '\n' ' ')"
[ -n "$RD" ] && [ -n "$RO" ] && awk -v d="$RD" -v o="$RO" 'BEGIN{exit !(d >= 0.60 && d >= o - 0.05)}'
note $? "coherence R=$RD is over the 0.60 gate and within 0.05 of the opening's R=$RO (or above it)"
echo "$RINGD" | grep -q "every ring is where the board puts it"
note $? "every ring is where the board puts it"
[ -n "$BD" ] && [ -n "$BO" ] && echo "$BD $BO" | awk '{dx=$1-$3; dy=$2-$4; exit !(dx*dx+dy*dy <= 9)}'
note $? "the bull is within 3 px of the opening's (dev $BD, opening $BO)"
[ -n "$MODELD" ]; note $? "and the board model was fitted for it"

echo
echo "---- D. the pin OD_LOOK_BUDGET=12 is the old default ----"
echo "     PREDICTED: camera 1 is set aside after $PINNED further looks, SCORING: 2 of 3, no background re-taken"
P=$RUN/r22-dev-12.cal
grep -q "OD_LOOK_BUDGET=12 is set" $P; note $? "the pinned run says it is pinned"
grep -q "LOOK AGAIN: camera(s) 1 were refused on the averaged frame and on all $PINNED further looks" $P
note $? "camera 1 refused on the averaged frame and on all $PINNED further looks"
[ "$(scoring_of $P)" = "SCORING: 2 of 3" ]; note $? "SCORING: 2 of 3 -- the board this issue was filed on, restored by the pin"
! grep -q "its background is re-taken" $P; note $? "and its background is the averaged calibration frames"

echo
echo "---- E. nothing else moves ----"
for pair in "r18-dev" "r18-open" "r22-open"; do
  comparable $RUN/$pair.out > $RUN/$pair.cmp
  comparable $RUN/$pair-12.out > $RUN/$pair-12.cmp
  if diff -q $RUN/$pair.cmp $RUN/$pair-12.cmp > /dev/null; then
    note 0 "$pair: the calibration transcript is byte-identical under the default and the pin ($(wc -l < $RUN/$pair.cmp) lines; $(scoring_of $RUN/$pair.cal))"
  else
    diff $RUN/$pair.cmp $RUN/$pair-12.cmp | head -10
    note 1 "$pair: the calibration transcript moved with the budget"
  fi
  ! grep -q "its background is re-taken" $RUN/$pair.cal; note $? "$pair: no background re-taken under the default"
done
for f in r18-dev r18-open; do
  ! grep -q "LOOK AGAIN" $RUN/$f.cal; note $? "$f: no look is taken at all -- every camera calibrates on the averaged frame"
done
grep -oE 'LOOK AGAIN: camera [0-9]+ calibrated on look [0-9]+ of [0-9]+' $RUN/r22-open.cal | sed 's/^/     r22-open: /'
grep -oE 'LOOK AGAIN: camera [0-9]+ calibrated on look [0-9]+ of [0-9]+' $RUN/r22-dev.cal | sed 's/^/     r22-dev:  /'

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: by default the looks outlast the dart that stood through camera 1's bull and camera 1 calibrates in the dev window with the opening's figures; OD_LOOK_BUDGET=12 restores the old twelve; nothing else moves"
exit 0

set -u
# #1358: the dart window used to open after the darts had been pulled.
#
# The window opens when a motion event ENDS, and an event only ends by settling. On
# mocks/rig-20260918 the only motion big enough to form an event under the trigger this
# issue was filed against was the player walking up and pulling the darts, so every
# window the detector opened measured a board that had just been emptied -- #1345
# counted each camera's changed pixels INSIDE its own fitted board and got 0 of 197117,
# 0 of 200385 and 0 of 194335, in all six windows, while camera 1's 12,109-13,372
# changed pixels were the thrower's shoes at the top of its frame.
#
# Three things are asserted here and the third is what makes the first two mean
# anything:
#
#   1. a window that moves the board UP has its evidence ON the board, on the cameras
#      that voted for it -- so a dart is measured while it is in the board;
#   2. a window whose evidence is entirely off every fitted board never moves the board
#      up, however many cameras vote -- the shoes cannot score;
#   3. OD_DART_WINDOW=settle restores the trigger as it was when the issue was filed,
#      on this same binary, and the rig's windows go back to zero on-board pixels and
#      zero scores. Without that the first two are claims about a fixture rather than
#      about a change.
#
# OD_WINDOW_CENSUS=1 is the instrument. #1345's account prints only for a window whose
# vote changed nothing, so a window that SCORED said nothing about where its evidence
# was; the census prints one line per completed window either way and is off unless
# asked for.

RIG=/app/mocks/rig-20260918
MOCKS=/app/mocks
CYCLES=${OD_CYCLES:-1800}

run() { # run <name> <cams> [env=value ...]
  local name="$1" cams="$2"; shift 2
  mkdir -p /run1358/$name && cd /run1358/$name
  env OD_MAX_CYCLES=$CYCLES OD_WINDOW_CENSUS=1 "$@" \
    /app/build/opendartboard --cams "$cams" --width 1280 --height 720 > /run1358/$name.out 2>&1
  sed 's/\x1b\[[0-9;]*m//g' /run1358/$name.out > /run1358/$name.txt
  cd /run1358
}

run rig      "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4"
run mocks    "$MOCKS/cam_1.mp4,$MOCKS/cam_2.mp4,$MOCKS/cam_3.mp4"
run rigsettle "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4" OD_DART_WINDOW=settle
# #1827: #1345's figures are counts inside the SCORING area, and since #1689 (191583e) a
# camera's board count is taken out to the rim by default. So #1345's figures are
# reproduced under OD_BOARD_COUNT=scoring, the pin that restores the count they were
# taken in, and the shipped count is held to what it may honestly promise (section 4).
run rigsettlescoring "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4" OD_DART_WINDOW=settle OD_BOARD_COUNT=scoring

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
census() { grep 'WINDOW CENSUS' /run1358/$1.txt | sed 's/.*\] - //'; }
windows() { census $1 | wc -l; }
# a window that moved the board: its account does not name the same state twice
moved()  { census $1 | grep -vE '([A-Z_0-9]+) -> \1 ' | wc -l; }
moved_up() { census $1 | grep -E '\-> DART_[123] ' | grep -vE '([A-Z_0-9]+) -> \1 ' | wc -l; }
# a window with not one changed pixel on ANY camera's fitted board
allempty() { census $1 | awk '{n=0;e=0; while (match($0,/board=[0-9]+\/[0-9]+/)) { s=substr($0,RSTART+6,RLENGTH-6); split(s,p,"/"); n++; if (p[1]==0) e++; $0=substr($0,RSTART+RLENGTH) } if (n>0 && n==e) print}'; }
scores() { grep -c 'SCORER] - SCORE:' /run1358/$1.txt || true; }

echo "=== 0. every run calibrated, so every number below is about darts ==="
for r in rig mocks rigsettle rigsettlescoring; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1358/$r.txt; then
    say "OK   $r calibrated 3 of 3" ok
  else say "FAIL $r did not calibrate 3 of 3, so it measures nothing" no; fi
done
E=$(grep -c '^\[ERROR\]' /run1358/mocks.txt || true)
if [ "$E" = 0 ]; then say "OK   the shipped mocks produce no ERROR" ok
else grep '^\[ERROR\]' /run1358/mocks.txt | head -3; say "FAIL the shipped mocks produce $E ERROR lines" no; fi

echo
echo "=== 1. a dart is measured while it is IN the board ==="
for r in rig mocks; do
  echo "$r: $(windows $r) windows, $(moved $r) moved the board, $(moved_up $r) of them up, $(scores $r) score lines"
done
# The rig is the fixture this issue is about. Under the trigger it was filed against it
# reached DART_1 never; a round of three darts is three windows that move up.
UP=$(moved_up rig)
if [ "$UP" -ge 12 ]; then say "OK   the rig's windows move the board up $UP times" ok
else say "FAIL the rig moved up only $UP times; the footage is seven rounds of three darts" no; fi
# and every one of those had its evidence on a board.
BAD=$(census rig | grep -E '\-> DART_[123] ' | grep -vE '([A-Z_0-9]+) -> \1 ' | while read -r l; do
        echo "$l" | grep -q 'board=0/' && echo "$l" | awk '{n=0;e=0; while (match($0,/board=[0-9]+\//)) { if (substr($0,RSTART+6,1)=="0") e++; n++; $0=substr($0,RSTART+RLENGTH) } if (n==e) print }'
      done | wc -l)
if [ "$BAD" = 0 ]; then say "OK   no window moved the board up with nothing on any camera's board" ok
else census rig | head -3; say "FAIL $BAD windows moved the board up on no on-board evidence at all" no; fi

echo
echo "=== 2. a window whose evidence is entirely off the board is not a dart ==="
for r in rig mocks; do
  N=$(allempty $r | wc -l)
  U=$(allempty $r | grep -E '\-> DART_[123] ' | grep -vE '([A-Z_0-9]+) -> \1 ' | wc -l)
  echo "$r: $N windows had no changed pixel on any camera's board; $U of them moved the board up"
  if [ "$U" = 0 ]; then say "OK   $r never called a dart on evidence that was entirely off the board" ok
  else allempty $r | head -2; say "FAIL $r called a dart $U times with nothing on any board" no; fi
done

echo
echo "=== 3. the shipped mocks keep their dart events ==="
MW=$(windows mocks); MS=$(scores mocks)
echo "mocks: $MW windows, $MS score lines in $CYCLES cycles"
if [ "$MW" -ge 16 ] && [ "$MS" -ge 9 ]; then
  say "OK   the control keeps its windows ($MW) and its scores ($MS)" ok
else say "FAIL the control fell to $MW windows and $MS scores; it measured 16 and 9 before #1358" no; fi

echo
echo "=== 4. falsification: restore the trigger this issue was filed against ==="
# #1345's own figures, which the trigger restored must reproduce: six windows, per-camera
# on-board counts of 0 of 197117 / 0,0,0,0,16,0 of 200385 / 0 of 194335, and camera 1
# clearing the frame threshold in every one of them on 12,109-13,372 changed pixels that
# are the thrower's shoes. So: every window empty but one, nothing scored, and the board
# never leaves CLEAN.
#
# #1827: those are counts inside the SCORING area (Region::mask), and #1689 (191583e)
# moved the default count out to the rim (Region::tip_mask) while keeping the scoring
# area as the denominator. On this footage that puts 115-122 px of the band between the
# double and the rim into window 4 on camera 1 -- 0.06% of its board, under the 0.10%
# floor, voted CLEAN -- and the shipped count reads 4 of 6 windows wholly empty where
# #1345 read 5. Measured on one binary (34c94eb): OD_BOARD_COUNT=scoring takes window 4
# back to 0 and reads 5 of 6. The one non-empty window is now window 3 (camera 1, 23 px
# inside the scoring area) rather than #1345's window 5 (16 px on camera 2): every window
# closes one cycle later than it did on 2026-09-30 (527 against 526, 922 against 921),
# and OD_WINDOW_CROP=off leaves that unchanged, so it is not #1687's crop.
#
# So #1345's figure is asserted in the count it was taken in, and the shipped count is
# held to what the falsification is about: the old trigger measures no dart -- nothing
# moves, nothing scores, and no camera's on-board figure in any window reaches the floor
# a dart has to clear.
if grep -q 'OD_BOARD_COUNT=scoring is set' /run1358/rigsettlescoring.txt; then
  say "OK   the pinned run really counted in the scoring area" ok
else say "FAIL OD_BOARD_COUNT=scoring did not announce itself, so the pinned run measured the default count" no; fi
PW=$(windows rigsettlescoring); PS=$(scores rigsettlescoring); PM=$(moved rigsettlescoring)
PE=$(allempty rigsettlescoring | wc -l)
echo "rig under OD_DART_WINDOW=settle OD_BOARD_COUNT=scoring: $PW windows, $PE of them with nothing on ANY camera's board, $PM moved the board, $PS score lines"
census rigsettlescoring
if [ "$PW" -ge 6 ] && [ "$PE" -ge $((PW - 1)) ] && [ "$PS" = 0 ] && [ "$PM" = 0 ]; then
  say "OK   the old trigger, counted as #1345 counted, puts the rig's windows back on an empty board and scores nothing" ok
else say "FAIL the old trigger, counted in the scoring area, left $PE of $PW windows empty, moved the board $PM times and scored $PS; #1345 measured six windows, all but one wholly empty, none scored" no; fi

RW=$(windows rigsettle); RS=$(scores rigsettle); RM=$(moved rigsettle)
RE=$(allempty rigsettle | wc -l)
# a window in which some camera's on-board figure reaches board_change_percent_threshold
# (0.10% of its own board, dart_processing.hpp)
RF=$(census rigsettle | awk '{f=0; while (match($0,/board=[0-9]+\/[0-9]+/)) { s=substr($0,RSTART+6,RLENGTH-6); split(s,p,"/"); if (p[2]>0 && 1000*p[1] >= p[2]) f=1; $0=substr($0,RSTART+RLENGTH) } if (f) print}' | wc -l)
echo "rig under OD_DART_WINDOW=settle, the shipped count: $RW windows, $RE of them with nothing on ANY camera's board, $RF with a camera at the floor, $RM moved the board, $RS score lines"
census rigsettle
if [ "$RW" -ge 6 ] && [ "$RF" = 0 ] && [ "$RS" = 0 ] && [ "$RM" = 0 ]; then
  say "OK   under the shipped count the old trigger measures no dart: nothing at the floor on any board, nothing moved, nothing scored" ok
else say "FAIL under the shipped count the old trigger left $RF of $RW windows with a camera at the floor, moved the board $RM times and scored $RS" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED

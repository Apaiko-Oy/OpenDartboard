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
for r in rig mocks rigsettle; do
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
RW=$(windows rigsettle); RS=$(scores rigsettle); RM=$(moved rigsettle)
RE=$(allempty rigsettle | wc -l)
echo "rig under OD_DART_WINDOW=settle: $RW windows, $RE of them with nothing on ANY camera's board, $RM moved the board, $RS score lines"
census rigsettle
# #1345's own figures, which this run must reproduce: six windows, per-camera on-board
# counts of 0 of 197117 / 0,0,0,0,16,0 of 200385 / 0 of 194335, and camera 1 clearing
# the frame threshold in every one of them on 12,109-13,372 changed pixels that are the
# thrower's shoes. So: every window empty but the one 16 px, nothing scored, and the
# board never leaves CLEAN.
if [ "$RW" -ge 6 ] && [ "$RE" -ge $((RW - 1)) ] && [ "$RS" = 0 ] && [ "$RM" = 0 ]; then
  say "OK   the old trigger puts the rig's windows back on an empty board and scores nothing" ok
else say "FAIL the old trigger left $RE of $RW windows empty, moved the board $RM times and scored $RS; #1345 measured six windows, all but one wholly empty, none scored" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED

set -u
# #1456, inside the container: which look a camera refused on its averaged frame seals.
#
# THE DEFECT. #1445's retry sealed the FIRST look on which a refused camera calibrated, so
# the board's geometry for that camera was whichever frame first cleared the gate (#1442's
# count of twenty when filed; R >= 0.60 since #1467). THE DECISION (2026-09-24): run the
# whole budget and seal the look with the highest R among those that pass, a tie to the
# earliest look. The rule is src/detector/geometry/look_choice.hpp, held as arithmetic by
# unit_check.sh 1456; this is the detector doing it on the two rig fixtures.
#
# WHO TAKES LOOKS AT ALL (measured on main, 1605-looks and #1551's census): rig-20260918
# none, in either window -- every camera calibrates on its averaged frame. rig-20260922
# dev: camera 2 (passes from look 9) and camera 1 (refused on all 12 by the default; a
# dart stands through its bull until f239, #1605). rig-20260922 opening: camera 2 (passes
# from look 3). So these two starts are where the rule can move a seal by default.
#
# THE ARMS (the issue's HYPOTHESIS section, rewritten by the decision comment):
#   tree   this binary, best of the budget
#   first  OD_LOOK_SEAL=first, the falsifier: the first look that passes is sealed and
#          looking at that camera stops, which is what main does (checked against main's
#          own binary out of this harness; see the PR)
#   once   OD_CALIBRATION_LOOKS=once, no look at all (#1445's pin)
# OD_I1456_REPS runs of each arm per start (default 2 of the tree's and 1 of each pin; 5
# of every arm is the decision's experiment), and the bull and the sealed look index are
# recorded TOGETHER for every camera, which is what the arms were asked to record.
#
# WHAT IS ASSERTED
#   A  rig-20260918 takes no look in either window, so the tester pins (671,309) (626,292)
#      (700,298) of its dev window are the AVERAGED frame's bulls (f90..119, f84..113,
#      f79..108 of cameras 1, 2, 3) and this issue does not move them.
#   B  THE RULE, on every start that looks: each camera that seals a look was looked at to
#      the end of its window (12 by default), sealed the highest R in its own look:R list
#      with ties to the earliest, and the bull GEOMETRY SEALED carries is the bull that
#      look measured -- the seal is that look's calibration, not the last one taken.
#   C  THE FALSIFIER: under OD_LOOK_SEAL=first each camera seals the first look of the
#      tree's own list (the same frame: nothing about the frames read differs until then)
#      and stops looking there.
#   D  REPRODUCIBLE: every run of an arm on a start seals the same look at the same bull.
#   E  OD_CALIBRATION_LOOKS=once takes no look and sets the refused cameras aside.
#   F  UNDER OD_LOOK_BUDGET=1605 (rig-20260922 dev): camera 1, refused on all of looks
#      1..12, is looked at to 31 and seals the best of the looks that pass (25..31);
#      camera 2, which passed within twelve, is looked at 12 times and not 31; the
#      background is re-taken; and OD_LOOK_SEAL=first puts camera 1 back on look 25.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RUN=/run1456
REPS="${OD_I1456_REPS:-2}"
PINREPS=1
[ "$REPS" -ge 5 ] && PINREPS=$REPS
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: testers/run_all.sh 1456-bestlook"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

R22=/app/mocks/rig-20260922
R18=/app/mocks/rig-20260918
calibrate_once() { # $1 fixture dir, $2 out file, $3.. extra env
  local dir="$1" out="$2"; shift 2
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=1 "$@" timeout 1200 $BIN \
      --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" \
      --width 1280 --height 720 > "$out.raw" 2>&1 )
  local rc=$?
  sed 's/\x1b\[[0-9;]*m//g' "$out.raw" > "$out"
  rm -f "$out.raw"
  return $rc
}

# One I1456 SEAL line per camera per run: the sealed look, its R, how many of how many
# looks passed, the list, the bull GEOMETRY SEALED carries and the bull the sealed look
# measured, the first look that passed and ITS bull.
seal_table() { # $1 log, $2 label
  awk -v label="$2" '
    function bullof(line,   m) { if (match(line, /\(([0-9]+),([0-9]+)\)[^(]*$/)) { m = substr(line, RSTART + 1, RLENGTH - 1); sub(/\).*/, "", m); return m } return "" }
    /LOOK AGAIN: camera\(s\) .* were refused on this start/ { looking = 1 }
    /Calibrating camera [0-9]+$/ { c = $NF; if (looking) { n[c]++ } cur[c] = "" }
    /Camera [0-9]+ bull at \(/ { split($0, a, "Camera "); split(a[2], b, " "); c = b[1]; cur[c] = bullof(substr($0, 1, index($0, ")"))) }
    /Camera [0-9]+ bull centre corrected by its wire evidence/ { split($0, a, "Camera "); split(a[2], b, " "); c = b[1]; s = $0; sub(/.* to \(/, "(", s); sub(/\).*/, ")", s); cur[c] = bullof(s) }
    /LOOK AGAIN: camera [0-9]+ calibrated on look [0-9]+ of/ { split($0, a, "camera "); split(a[2], b, " "); c = b[1]; lk = b[5]; lookbull[c, lk] = cur[c]; if (!(c in first)) first[c] = lk }
    /LOOK AGAIN: camera [0-9]+ seals look/ {
      split($0, a, "camera "); split(a[2], b, " "); c = b[1]; seal[c] = b[4]; r = $0; sub(/.* at R=/, "", r); sub(/,.*/, "", r); sr[c] = r
      l = $0; sub(/.*look:R /, "", l); sub(/\).*/, "", l); list[c] = l
      p = $0; if (match(p, /the highest R of the [0-9]+ of its [0-9]+/)) { split(substr(p, RSTART, RLENGTH), q, " "); passed[c] = q[6]; looked[c] = q[9] }
      else if (match(p, /the first of its [0-9]+/)) { split(substr(p, RSTART, RLENGTH), q, " "); looked[c] = q[5]; passed[c] = split(l, zz, " ") }
    }
    /were refused on the averaged frame and on all [0-9]+ further looks/ { s = $0; sub(/.*camera\(s\) /, "", s); sub(/ were.*/, "", s); nc = split(s, cs, ", "); for (i = 1; i <= nc; i++) refusedall[cs[i]] = 1 }
    /GEOMETRY SEALED:/ { for (c = 1; c <= 3; c++) { s = $0; if (match(s, "camera " c " index=[0-9]+ scoring=[01] bull=[0-9]+,[0-9]+")) { t = substr(s, RSTART, RLENGTH); sub(/.*bull=/, "", t); sb[c] = t; u = substr(s, RSTART, RLENGTH); sub(/.*scoring=/, "", u); sc[c] = substr(u, 1, 1) } } }
    END {
      for (c = 1; c <= 3; c++) {
        if (c in seal) printf "I1456 SEAL %s cam=%d look=%s R=%s passed=%s looked=%s sealed_bull=%s look_bull=%s first=%s first_bull=%s list=%s\n", label, c, seal[c], sr[c], passed[c], looked[c], sb[c], lookbull[c, seal[c]], first[c], lookbull[c, first[c]], list[c]
        else if (c in refusedall) printf "I1456 SEAL %s cam=%d look=none refused_on_every_look=1 scoring=%s\n", label, c, sc[c]
        else printf "I1456 SEAL %s cam=%d look=averaged sealed_bull=%s scoring=%s\n", label, c, sb[c], sc[c]
      }
    }' "$1"
}

declare -A RC_OF
run_arm() { # $1 start label (r22-dev ...), $2 fixture dir, $3 arm, $4 reps, $5.. env
  local start="$1" dir="$2" arm="$3" reps="$4"; shift 4
  local k
  for k in $(seq 1 "$reps"); do
    calibrate_once "$dir" "$RUN/$start.$arm.$k.log" "$@"
    RC_OF["$start.$arm.$k"]=$?
    seal_table "$RUN/$start.$arm.$k.log" "start=$start arm=$arm run=$k" | tee "$RUN/$start.$arm.$k.seal"
  done
}

echo "---- the runs: $REPS of the tree's rule and $PINREPS of each pin per start ----"
run_arm r18-dev  $R18 tree 1
run_arm r18-open $R18 tree 1 OD_SEEK_VIDEO=off
for start in r22-dev r22-open; do
  envs=(); [ $start = r22-open ] && envs=(OD_SEEK_VIDEO=off)
  run_arm $start $R22 tree  "$REPS"    "${envs[@]}"
  run_arm $start $R22 first "$PINREPS" "${envs[@]}" OD_LOOK_SEAL=first
  run_arm $start $R22 once  "$PINREPS" "${envs[@]}" OD_CALIBRATION_LOOKS=once
done
run_arm r22-dev-1605 $R22 tree  1 OD_LOOK_BUDGET=1605
run_arm r22-dev-1605 $R22 first 1 OD_LOOK_BUDGET=1605 OD_LOOK_SEAL=first

BAD=0; for k in "${!RC_OF[@]}"; do [ "${RC_OF[$k]}" = 0 ] || { echo "     rc ${RC_OF[$k]}: $k"; BAD=1; }; done
[ $BAD = 0 ]; note $? "every calibration run ended cleanly (${#RC_OF[@]} runs)"
grep -q "DEBUG_SEEK_VIDEO: video 1 seeked forward" $RUN/r22-dev.tree.1.log
note $? "the binary carries the dev seek, so the dev window is the one measured"

field() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<< "$1"; }
seal_of() { grep " cam=$3 " "$RUN/$1.$2.1.seal"; }

echo
echo "---- A. rig-20260918 takes no look, so its pinned bulls are the averaged frame's ----"
for s in r18-dev r18-open; do
  ! grep -q "LOOK AGAIN" $RUN/$s.tree.1.log; note $? "$s: no look is taken at all"
done
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  grep -qF "$e" $RUN/r18-dev.tree.1.log; note $? "r18-dev: '$e' is read on the averaged frame, and nothing after it"
done

echo
echo "---- B. the rule, on every start that looks ----"
for s in r22-dev r22-open r22-dev-1605; do
  for c in 1 2 3; do
    L="$(seal_of $s tree $c)"
    look="$(field "$L" look)"
    case "$look" in none|averaged|"") continue ;; esac
    list="$(field "$L" list | tr '_' ' ')"
    # the list is space-separated look:R pairs inside the SEAL line; re-read it whole
    list="$(sed -n 's/.* list=//p' <<< "$L")"
    best="$(tr ' ' '\n' <<< "$list" | awk -F: 'NF==2 { if (!seen || $2 > r) { r = $2; l = $1; seen = 1 } } END { print l }')"
    [ "$look" = "$best" ]
    note $? "$s camera $c: sealed look $look is the highest R of its list, ties to the earliest (list: $list)"
    [ "$(field "$L" sealed_bull)" = "$(field "$L" look_bull)" ] && [ -n "$(field "$L" look_bull)" ]
    note $? "$s camera $c: GEOMETRY SEALED carries look $look's own bull ($(field "$L" look_bull)), not another look's"
    want=12; [ $s = r22-dev-1605 ] && [ $c = 1 ] && want=31
    [ "$(field "$L" looked)" = "$want" ]
    note $? "$s camera $c: looked at $(field "$L" looked) times, to the end of its window ($want)"
  done
done

echo
echo "---- C. the falsifier, OD_LOOK_SEAL=first ----"
for s in r22-dev r22-open r22-dev-1605; do
  for c in 1 2 3; do
    T="$(seal_of $s tree $c)"; F="$(seal_of $s first $c)"
    case "$(field "$T" look)" in none|averaged|"") continue ;; esac
    [ "$(field "$F" look)" = "$(field "$T" first)" ] && [ "$(field "$F" sealed_bull)" = "$(field "$T" first_bull)" ]
    note $? "$s camera $c: the pin seals look $(field "$F" look) at $(field "$F" sealed_bull) -- the tree's first passing look ($(field "$T" first), $(field "$T" first_bull)); the tree seals look $(field "$T" look) at $(field "$T" sealed_bull)"
    [ "$(field "$F" looked)" = "$(field "$T" first)" ]
    note $? "$s camera $c: and the pin stops looking there ($(field "$F" looked) looks)"
  done
done

echo
echo "---- D. every run of an arm seals the same look at the same bull ----"
for s in r22-dev r22-open; do
  for arm in tree first once; do
    n=$(ls $RUN/$s.$arm.*.seal | wc -l)
    u=$(cat $RUN/$s.$arm.*.seal | sed 's/ run=[0-9]*//' | sort -u | wc -l)
    [ "$u" = 3 ]; note $? "$s $arm: $n run(s), one seal per camera across all of them"
  done
done

echo
echo "---- E. OD_CALIBRATION_LOOKS=once ----"
for s in r22-dev r22-open; do
  ! grep -q "LOOK AGAIN: camera [0-9]* calibrated" $RUN/$s.once.1.log && grep -q "OD_CALIBRATION_LOOKS=once is set" $RUN/$s.once.1.log
  note $? "$s: no look is taken, and the run says why ($(grep -oE 'SCORING: [0-9]+ of [0-9]+' $RUN/$s.once.1.log | head -1))"
done

echo
echo "---- F. under OD_LOOK_BUDGET=1605, rig-20260922 dev ----"
C1="$(seal_of r22-dev-1605 tree 1)"; C1F="$(seal_of r22-dev-1605 first 1)"
[ "$(field "$C1" first)" = 25 ] && [ "$(field "$C1" passed)" = 7 ]
note $? "camera 1 passes from look 25 (#1605's census) on all 7 of looks 25..31 (passed $(field "$C1" passed), first $(field "$C1" first))"
[ "$(field "$C1F" look)" = 25 ]; note $? "OD_LOOK_SEAL=first puts camera 1 back on look 25, #1605's seal"
grep -q "its background is re-taken" $RUN/r22-dev-1605.tree.1.log; note $? "the looks ran past 12, so the background is re-taken"

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: a refused camera seals the look with the highest R of its budget, ties to the earliest; OD_LOOK_SEAL=first restores the first-passing look; rig-20260918 takes no look and its pins are the averaged frame's"
exit 0

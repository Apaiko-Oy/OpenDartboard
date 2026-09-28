#!/bin/bash
# #1487, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1487.
#
# The fixture: mocks/rig-20260918 is a Winmau Blade 6, on which #1498's number reader
# measures the wedge of every dart (#1512's note: 19 of 19), so the clip AS RECORDED is
# the CONTROL -- measured, and never marked. The same footage with the reader switched off
# (OD_NUMBER_ANCHOR=off) is a board nothing recognises and no camera can anchor, which is
# exactly the board #1487 was re-scoped to on 2026-09-24: every dart falls back to #1346's
# asserted 20. Both are asked of one binary, and a third run holds the falsifier.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
CHECK=/run1487/notice_check

build_check() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -o "$2" "$1/testers/i1487_notice_check.cpp" \
    -I"$1/src" -I"$1/src/utils" \
    -I"$1/src/detector/geometry/calibration" -I"$1/src/detector/geometry/detection" \
    $(pkg-config --cflags --libs opencv4) -lpthread > "$2.build.log" 2>&1
}

echo "=== building the notice check ==="
if ! build_check "$SRC" "$CHECK"; then
  tail -30 "$CHECK.build.log"
  echo "FAIL the notice check did not build; nothing below measures anything"
  exit 2
fi

echo
echo "=== 1. the notice, this tree's decision ========================================"
"$CHECK" > /run1487/plain.txt 2>&1
PLAIN_RC=$?
cat /run1487/plain.txt
if [ "$PLAIN_RC" = 0 ]; then
  say "OK   every assertion in the pure check holds on this tree" ok
else
  say "FAIL the pure check answered rc=$PLAIN_RC on this tree" no
fi

echo
echo "=== 2. THE FALSIFIER: OD_ASSERTED_WEDGE=unsaid, on the same binary ============="
OD_ASSERTED_WEDGE=unsaid "$CHECK" > /run1487/unsaid.txt 2>&1
UNSAID_RC=$?
cat /run1487/unsaid.txt
if [ "$UNSAID_RC" = 0 ]; then
  say "OK   and the switch really puts the pre-#1487 board back: every assertion about that state holds" ok
else
  say "FAIL OD_ASSERTED_WEDGE=unsaid does not produce the state this issue is about" no
fi
if diff -q /run1487/plain.txt /run1487/unsaid.txt > /dev/null; then
  say "FAIL the switch changed nothing -- the two runs are identical, so neither measures the other" no
else
  say "OK   the two runs differ, so the switch reaches the decision it names" ok
fi

echo
echo "=== 3. the detector itself, over the whole of mocks/rig-20260918 ==============="
run_detector() { # $1 tag, rest: env
  local tag="$1"; shift
  rm -rf "/run1487/$tag"; mkdir -p "/run1487/$tag"; cd "/run1487/$tag" || exit 2
  env OD_MAX_CYCLES=0 "$@" timeout 900 /app/build/opendartboard \
    --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
    --width 1280 --height 720 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "/run1487/$tag.log"
  cd /
}

# One run's figures. A dart is a SCORE line that is not END; its BOARD line is the one
# printed before it since the previous SCORE line (the geometric path prints its own
# words, which are neither of the two below).
census() { # $1 log
  python3 - "$1" <<'PY'
import re, sys
log = open(sys.argv[1], errors="replace").read().splitlines()
SCORE = re.compile(r"SCORE:\s+(\S+)\s+\|\s+Position:.*Confidence:\s+([0-9.]+)")
BOARD = re.compile(r"BOARD:\s+(wedge measured|wedge by default|wedge not in this reading|wedge from the solved entry)")
darts, board = [], None
for line in log:
    m = BOARD.search(line)
    if m:
        board = m.group(1)
        continue
    m = SCORE.search(line)
    if m:
        if m.group(1) != "END":
            darts.append((m.group(1), float(m.group(2)), board))
        board = None
notices = [l for l in log if "ASSERTED WEDGE: " in l]
print("DARTS=%d" % len(darts))
print("BYDEFAULT=%d" % sum(1 for d in darts if d[2] == "wedge by default"))
print("AT_HALF_BYDEFAULT=%d" % sum(1 for d in darts if d[2] == "wedge by default" and abs(d[1] - 0.5) < 1e-6))
print("NOTICES=%d" % len(notices))
print("NOTICE_WARN=%d" % sum(1 for l in notices if l.startswith("[WARN]")))
print("ENDED=%s" % ("end-of-footage" if any("END OF FOOTAGE:" in l for l in log) else "not-end-of-footage"))
PY
}

if [ ! -x /app/build/opendartboard ]; then
  say "FAIL /app/build/opendartboard is not here, so the program half of this measures nothing" no
else
  for tag in measured asserted unsaid; do
    case $tag in
      measured) run_detector "$tag" ;;
      asserted) run_detector "$tag" OD_NUMBER_ANCHOR=off ;;
      unsaid)   run_detector "$tag" OD_NUMBER_ANCHOR=off OD_ASSERTED_WEDGE=unsaid ;;
    esac
    census "/run1487/$tag.log" > "/run1487/$tag.census"
    echo "  --- $tag ---"
    sed 's/^/      /' "/run1487/$tag.census"
    grep -a "ASSERTED WEDGE: " "/run1487/$tag.log" | sed 's/^/      | /'
    eval "$(sed "s/^/${tag}_/" "/run1487/$tag.census")"
  done

  if [ "$measured_ENDED" = end-of-footage ] && [ "$asserted_ENDED" = end-of-footage ] &&
     [ "$unsaid_ENDED" = end-of-footage ]; then
    say "OK   all three runs ended where the FOOTAGE ended, so every figure is of the whole clip" ok
  else
    say "FAIL a run stopped before the end of the footage, so its figures are of an opening" no
  fi

  # #708's rule: the needle is proved to exist before its absence means anything. The
  # reader-off run must really publish asserted darts, and MORE THAN ONE, or "once" is
  # a claim about a run where once and every time are the same number.
  if [ "$asserted_BYDEFAULT" -ge 2 ]; then
    say "OK   with the number reader off, $asserted_BYDEFAULT of $asserted_DARTS darts publish as an asserted wedge ($asserted_AT_HALF_BYDEFAULT of them at 0.5)" ok
  else
    say "FAIL with the number reader off only $asserted_BYDEFAULT darts were asserted, so 'once, not per dart' is not being asked" no
  fi

  # Asserted darts are MARKED -- said once, in words, at default level.
  if [ "$asserted_NOTICES" = 1 ]; then
    say "OK   the board said so ONCE over $asserted_BYDEFAULT asserted darts" ok
  else
    say "FAIL the board said so $asserted_NOTICES times over $asserted_BYDEFAULT asserted darts" no
  fi
  # No camera can be read on this run, so start-up already WARNED (#1449, #1501); the
  # notice points back at that rather than warning a second time.
  if [ "$asserted_NOTICE_WARN" = 0 ] && [ "$asserted_NOTICES" = 1 ]; then
    say "OK   ... at INFO, because the start-up WARNING already said every dart would be asserted" ok
  else
    say "FAIL the notice on a board start-up already warned about is not the one INFO line" no
  fi

  # Measured darts are NOT marked.
  if [ "$measured_BYDEFAULT" = 0 ] && [ "$measured_NOTICES" = 0 ] && [ "$measured_DARTS" -gt 0 ]; then
    say "OK   the clip as recorded ($measured_DARTS darts, wedges measured) carries no mark and no notice" ok
  else
    say "FAIL the control run carries $measured_BYDEFAULT asserted darts and $measured_NOTICES notices over $measured_DARTS darts" no
  fi

  # The falsifier on the program: the same asserted darts, and nothing said.
  if [ "$unsaid_NOTICES" = 0 ] && [ "$unsaid_BYDEFAULT" -ge 2 ]; then
    say "OK   OD_ASSERTED_WEDGE=unsaid publishes $unsaid_BYDEFAULT asserted darts and says nothing -- the board before #1487" ok
  else
    say "FAIL the falsifier run said $unsaid_NOTICES notices over $unsaid_BYDEFAULT asserted darts" no
  fi

  # And the notice changed no score: the reader-off run and its falsifier publish the
  # same SCORE strings and confidences, dart for dart.
  grep -aoE "SCORE: \S+ \| Position: \([0-9-]+,[0-9-]+\) \| Confidence: [0-9.]+" /run1487/asserted.log > /run1487/asserted.scores
  grep -aoE "SCORE: \S+ \| Position: \([0-9-]+,[0-9-]+\) \| Confidence: [0-9.]+" /run1487/unsaid.log > /run1487/unsaid.scores
  if [ -s /run1487/asserted.scores ] && cmp -s /run1487/asserted.scores /run1487/unsaid.scores; then
    say "OK   the notice moved no score: both reader-off runs publish the same $(wc -l < /run1487/asserted.scores) SCORE lines" ok
  else
    say "FAIL the reader-off run and its falsifier published different SCORE lines" no
    diff /run1487/asserted.scores /run1487/unsaid.scores | head -10
  fi
fi

echo
echo "=== 4. THE MUTATION PROOF: take the latch out, and take the mark out =========="
plant() { # $1 tag, $2 file under src/, $3 sed expression, $4 what must be there afterwards
  local tag="$1" file="$2" expr="$3" want="$4"
  local dir="/run1487/planted-$tag"
  rm -rf "$dir"; mkdir -p "$dir"
  cp -r "$SRC/src" "$SRC/testers" "$dir/"
  sed -i "$expr" "$dir/$file"
  if ! grep -qF "$want" "$dir/$file"; then
    say "FAIL the $tag plant did not land -- the line was not where this proof expects it, so it proves nothing" no
    return
  fi
  if ! build_check "$dir" "/run1487/planted_$tag"; then
    tail -20 "/run1487/planted_$tag.build.log"
    say "FAIL the $tag plant did not build, so the mutation proves nothing" no
    return
  fi
  "/run1487/planted_$tag" > "/run1487/planted_$tag.txt" 2>&1
  local rc=$? n
  n=$(grep -c '^FAIL ' "/run1487/planted_$tag.txt" || true)
  echo "    $tag: the planted tree answered rc=$rc with $n failed assertions"
  grep '^FAIL ' "/run1487/planted_$tag.txt" | head -3 | sed 's/^/      | /'
  if [ "$rc" != 0 ] && [ "$n" -gt 0 ]; then
    say "OK   the $tag mutation is fatal: the check goes red on it" ok
  else
    say "FAIL the $tag mutation changed nothing, so the assertions it should break measure nothing" no
  fi
}

# ONCE: the latch stops being read, so every asserted dart is said.
plant latch src/detector/geometry/detection/score_processing.hpp \
  's/if (unsaid || !published_asserted || already_said)/if (unsaid || !published_asserted)/' \
  'if (unsaid || !published_asserted)'

# THE MARK: the notice stops asking whether the dart was asserted at all.
plant mark src/detector/geometry/detection/score_processing.hpp \
  's/if (unsaid || !published_asserted || already_said)/if (unsaid || already_said)/' \
  'if (unsaid || already_said)'

echo
if [ "$FAILED" = 0 ]; then echo "i1487: PASS"; else echo "i1487: FAIL"; fi
exit $FAILED

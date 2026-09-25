#!/bin/bash
# #1628, inside the container: the dart the issue is about, on the real binary, both ways.
#
#   1. rig-20260922, dev window, OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 -- the
#      configuration #1618 measured at 67/84, the tree's default
#   2. the same, with OD_LONE_WIRE=clear -- the refused reselection, on the same binary
#
# ASSERTED: v7.2 (thrown S3) is matched in both; the geometry refused it and the vote had
# no consensus; #1517's fallback chose camera 1's S19 under 1 mm from the wire, which the
# default publishes (the reselection was measured 1:1 and refused) and OD_LONE_WIRE=clear
# replaces with camera 2's S3. REPORTED and not asserted: the TALLY and SWEEP lines, which are the
# rule's effect over the whole clip -- the four-window, two-configuration count is made
# over #1555's own runs (i1628_census.py pointed at them) and written in the PR.
set -u
BIN=/app/build/opendartboard
RUN=/run1628
T22=/app/testers/i1499_truth_rig20260922.md
A22=/app/testers/i1511_annotations/rig-20260922.csv
[ -x $BIN ] || { echo "FAIL no $BIN"; exit 1; }

replay() { # $1 name, $2.. env
    local out="$1"; shift
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    local d=/app/mocks/rig-20260922
    env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 "$@" \
        timeout 900 $BIN --cams "$d/cam_1.mp4,$d/cam_2.mp4,$d/cam_3.mp4" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    if [ $rc -ne 0 ] && [ $rc -ne 124 ]; then tail -5 "$RUN/$out.txt"; echo "FAIL $out did not finish"; exit 1; fi
    python3 /app/testers/i1628_census.py --log "$RUN/$out.txt" --truth $T22 --annotations $A22 \
        --fixture rig-20260922 --window dev --no-arrival 1.1 | tee "$RUN/census-$out.txt"
    [ ${PIPESTATUS[0]} -eq 0 ] || { echo "FAIL the $out census could not be read"; exit 1; }
}

echo "=== 1. rig-20260922 dev, both switches on, the tree's rule ======================"
replay tree
echo "=== 2. the same, OD_LONE_WIRE=clear ============================================"
replay pinned OD_LONE_WIRE=clear

echo "=== assertions ================================================================="
FAILS=0
check() { if eval "$1"; then echo "OK   $2"; else echo "FAIL $2"; FAILS=$((FAILS+1)); fi; }
T=$(grep "^I1628 DART .* v7.2 thrown=S3 " "$RUN/census-tree.txt" | head -1)
P=$(grep "^I1628 DART .* v7.2 thrown=S3 " "$RUN/census-pinned.txt" | head -1)
echo "tree:   $T"
echo "pinned: $P"
check '[ -n "$T" ] && [ -n "$P" ]' "v7.2 is matched, and is a no-consensus lone reading, in both runs"
check 'echo "$T" | grep -q "path=vote "' "the geometry did not decide v7.2 (the vote published it)"
check 'echo "$T" | grep -q "fallback=cam1:S19 "' "#1517's fallback chose camera 1's S19"
check 'echo "$T" | grep -q " near=1 " && [ "$(echo "$T" | sed -n "s/.*margin=\([0-9.]*\).*/\1/p" | cut -d. -f1)" = "0" ]' "camera 1's S19 is under 1 mm from the wedge wire"
check 'echo "$T" | grep -q "published=cam1:S19 reselected=0 " && echo "$T" | grep -q "after=wedge"' "the default publishes camera 1's S19 (#1517's fallback, unchanged)"
check 'echo "$T" | grep -q " cam2=S3/"' "camera 2 read S3 in this window"
check 'echo "$P" | grep -q "published=cam2:S3 reselected=1 " && echo "$P" | grep -q "after=exact"' "OD_LONE_WIRE=clear publishes camera 2's S3"
check 'grep -q "LONE-WIRE: camera 0.s S19 .*the reselection is off" "$RUN/tree.txt"' "the default log says the S19 was inside the sigma and why it stands"
check 'grep -q "LONE-WIRE: camera 0.s S19 .*publishes instead" "$RUN/pinned.txt"' "the pinned log says it was replaced"
echo "failures=$FAILS"
[ $FAILS -eq 0 ] || exit 1
echo "ALL HELD"
exit 0

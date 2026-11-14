#!/bin/bash
# turnaus#1992: the crossing census's own arithmetic, held -- and three mutations of it.
#
# i1992_crossing_census.py --synthetic plants a board through a known homography and a
# known kappa and asks the census's parts what they must answer: the residual's sign and
# split, the lens round trip, a flat board fitted exactly, the true model reading zero, a
# kappa-0 fit unable to absorb a planted kappa and the right one removing it, the scan
# recovering it, the even wires predicted from the odd, a doubles-only flat fit reading
# the treble OUTWARD under barrel distortion, the wire naming, and a line meeting a conic.
#
# Then three mutations of a scratch copy, each predicted before it is run, by name:
#   sign      the radial residual subtracted the other way: `residual: sign and split`
#             and `holdout: a doubles-only flat fit reads the treble outward`
#   no-lens   undistort returning its argument: the four checks that need the lens gone
#   naming    every crossing named one wire on: `naming: every crossing is given its own wire`
# A mutation that turns a different set red is a FAIL here, with both sets printed.
#
# Python in $OD_IMAGE (the host has none, #1560's rule), tree read-only, no network, no
# detector binary: OD_SKIP_BUILD changes nothing about it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1992-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
set -u
FAILED=0
S=$(mktemp -d)
echo "==== 1. the tree ==============================================================="
if python3 testers/i1992_crossing_census.py --synthetic; then
  echo "OK   the tree passes whole"
else
  echo "FAIL the tree: its own synthetic check is red"
  FAILED=1
fi

mutate() { # name, needle, replacement, then the checks that must turn red (and no others)
  local name=$1 needle=$2 repl=$3; shift 3
  echo "==== mutation $name ============================================================"
  rm -rf "$S/m"; mkdir -p "$S/m"; cp testers/i1560_k1_census.py testers/i1992_crossing_census.py "$S/m/"
  if ! python3 - "$S/m/i1992_crossing_census.py" "$needle" "$repl" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
n = s.count(a)
open(p, "w").write(s.replace(a, b))
print("mutation: needle found %d time(s)" % n)
sys.exit(0 if n == 1 else 1)
PY
  then
    echo "FAIL mutation $name: the needle is not in the file exactly once"; FAILED=1; return
  fi
  local out got want
  out=$(python3 "$S/m/i1992_crossing_census.py" --synthetic)
  printf "%s\n" "$out"
  got=$(printf "%s\n" "$out" | awk -F": " "/^FAIL /{sub(/^FAIL /,\"\",\$1); print \$1 \": \" \$2}" | sort | tr "\n" "|")
  want=$(printf "%s\n" "$@" | sort | tr "\n" "|")
  if [ "$got" = "$want" ]; then
    echo "OK   mutation $name turns exactly the predicted $# check(s) red"
  else
    echo "FAIL mutation $name: predicted [$want] and got [$got]"; FAILED=1
  fi
}

mutate sign "    return r - truth_mm, d * truth_mm" "    return truth_mm - r, d * truth_mm" \
  "residual: sign and split" "holdout: a doubles-only flat fit reads the treble outward"
mutate no-lens "    return PP[0] + ux, PP[1] + uy" "    return p" \
  "lens: undistort inverts distort" "lens: the planted kappa removes it" \
  "lens: the scan recovers the planted kappa" "holdout: the even wires are predicted from the odd"
mutate naming "        c[\"wire\"] = int(round(advance * (th - theta20) / SECTOR)) % 20" \
  "        c[\"wire\"] = (int(round(advance * (th - theta20) / SECTOR)) + 1) % 20" \
  "naming: every crossing is given its own wire"

[ $FAILED -eq 0 ] && echo "I1992 check: the tree green, three mutations red as predicted"
exit $FAILED
' < /dev/null
RC=$?
echo "RUN=1992-crossings rc=$RC"
exit $RC

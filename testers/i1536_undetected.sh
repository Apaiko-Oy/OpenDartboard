#!/bin/bash
# #1536: the detector's own miss-rate, as a measured figure, off a 1555-bakeoff's logs.
#
#   bash testers/i1536_undetected.sh <run dir> [<out dir>]
#
# <run dir> holds a 1555 bakeoff's four detector logs (r18-dev.txt, r18-open.txt,
# r22-dev.txt, r22-open.txt, as i1555_inside.sh leaves them under /run1555). No replay is
# made: the census is re-run on those logs, which is free and deterministic, so the same
# logs give the same figures on any box.
#
# WHAT IT PRINTS, per fixture and per calibration window, and pooled (never across the
# two windows blind, #1551):
#   I1536 DETECTION  landed-and-not-detected and missed-and-not-detected, side by side and
#                    never summed (item 1 of the 2026-09-28 brief);
#   I1536 CAMERA-*   for every annotated arrival, each camera's state in the window its
#                    publication came from: C a constraint the solve used, X one it
#                    excluded, t a tip with no usable constraint, - nothing -- with the
#                    exclusion's own word (item 2). An arrival nothing published has no
#                    window and is seen by none; the unclaimed publications between its
#                    neighbours are shown beside it, unattributed.
#
# WHAT IT ASSERTS -- only that the instrument works, never the figure (item 3): the PLANT.
# OD_CENSUS_PLANT=<fixture>:<window>:<v.d> makes the census drop, before matching, the
# publication the matcher gave that arrival. Re-run so on one log, `undetected` and
# landed-and-not-detected must each move by exactly one, the newly undetected dart must
# be the planted one and no other, and missed-and-not-detected must not move. A plant
# that moved the figure by two, or named another dart, would mean the matcher reshuffled
# around the hole and the figure is not a count of darts.
set -u

RUN="${1:?usage: i1536_undetected.sh <run dir> [<out dir>]}"
OUT="${2:-$RUN/i1536}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
CENSUS="$HERE/i1555_census.py"
T18="$ROOT/mocks/rig-20260918/GROUND-TRUTH.md"
T22="$HERE/i1499_truth_rig20260922.md"
A18="$HERE/i1511_annotations/rig-20260918.csv"
A22="$HERE/i1511_annotations/rig-20260922.csv"
# The planted dart: rig-20260918's v3.2, a landed S7 every run of the bakeoff has
# detected. Overridable, because a future run that lost it would need another.
PLANT="${I1536_PLANT:-rig-20260918:dev:3.2}"

mkdir -p "$OUT" || exit 1
for f in "$T18" "$T22" "$A18" "$A22"; do
    [ -s "$f" ] || { echo "FAIL $f is missing"; exit 1; }
done

census() { # $1 log name, $2 window word, -> $OUT/census-$1.txt
    local name="$1" window="$2" log="$RUN/$1.txt"
    [ -s "$log" ] || { echo "FAIL $log is missing"; exit 1; }
    case "$name" in
        r18-*) python3 "$CENSUS" --log "$log" --truth "$T18" --annotations "$A18" \
                   --fixture rig-20260918 --window "$window" --min-matched 4 ;;
        r22-*) python3 "$CENSUS" --log "$log" --truth "$T22" --annotations "$A22" \
                   --fixture rig-20260922 --window "$window" --min-matched 2 \
                   --no-arrival 1.1 ;;
    esac > "$OUT/census-$name.txt"
    local rc=$?
    if [ $rc -ne 0 ]; then
        grep -h '^I1536 PLANT\|^I1555 CENSUS ' "$OUT/census-$name.txt"
        echo "FAIL the $name census exited $rc"
        exit 1
    fi
}

(unset OD_CENSUS_PLANT
 census r18-dev dev; census r18-open opening; census r22-dev dev; census r22-open opening
) || exit 1
python3 "$CENSUS" --pool "$OUT/census-r18-dev.txt" "$OUT/census-r18-open.txt" \
    "$OUT/census-r22-dev.txt" "$OUT/census-r22-open.txt" > "$OUT/pooled.txt" \
    || { echo "FAIL nothing to pool"; exit 1; }

echo "=== #1536: per fixture and window ============================================="
for name in r18-dev r18-open r22-dev r22-open; do
    grep -h '^I1555 ACCURACY \|^I1536 DETECTION \|^I1536 DETECTION-DART \|^I1536 CAMERA-TALLY ' \
        "$OUT/census-$name.txt"
done
echo "=== #1536: pooled ============================================================="
grep -h '^I1555 ACCURACY \|^I1536 ' "$OUT/pooled.txt"
echo "=== #1536: per camera, every arrival =========================================="
grep -h '^I1536 CAMERA-DART ' "$OUT"/census-r18-dev.txt "$OUT"/census-r18-open.txt \
    "$OUT"/census-r22-dev.txt "$OUT"/census-r22-open.txt

echo "=== #1536: the plant, $PLANT ======================================"
PF="${PLANT%%:*}"; PREST="${PLANT#*:}"; PW="${PREST%%:*}"; PD="${PREST#*:}"
case "$PF:$PW" in
    rig-20260918:dev) PNAME=r18-dev ;; rig-20260918:opening) PNAME=r18-open ;;
    rig-20260922:dev) PNAME=r22-dev ;; rig-20260922:opening) PNAME=r22-open ;;
    *) echo "FAIL the plant $PLANT names no census of this bakeoff"; exit 1 ;;
esac
mkdir -p "$OUT/plant" || exit 1
(export OD_CENSUS_PLANT="$PLANT"; OUT="$OUT/plant"; census "$PNAME" "$PW") || exit 1
grep -h '^I1536 PLANT ' "$OUT/plant/census-$PNAME.txt"
python3 - "$OUT/census-$PNAME.txt" "$OUT/plant/census-$PNAME.txt" "$PD" <<'PY'
import sys
base, planted, dart = sys.argv[1], sys.argv[2], "v" + sys.argv[3].lstrip("v")

def read(path):
    tally, det, undet = {}, {}, set()
    for line in open(path, errors="replace"):
        if line.startswith("I1555 ACCURACY-TALLY "):
            tally = dict(t.split("=", 1) for t in line.split() if "=" in t)
        elif line.startswith("I1536 DETECTION-TALLY "):
            det = dict(t.split("=", 1) for t in line.split() if "=" in t)
        elif line.startswith("I1555 ACCURACY-DART ") and " UNDETECTED " in line:
            undet.add(line.split()[2])
    return tally, det, undet

bt, bd, bu = read(base)
pt, pd, pu = read(planted)
if not (bt and pt and bd and pd):
    print("FAIL a census printed no ACCURACY-TALLY or DETECTION-TALLY line")
    sys.exit(1)
du = int(pt["undetected"]) - int(bt["undetected"])
dl = int(pd["landed_not_detected"]) - int(bd["landed_not_detected"])
dm = int(pd["missed_not_detected"]) - int(bd["missed_not_detected"])
new, gone = sorted(pu - bu), sorted(bu - pu)
print("I1536 PLANT-PROOF %s: undetected %s -> %s (%+d), landed-and-not-detected %s -> %s "
      "(%+d), missed-and-not-detected %s -> %s (%+d), arrivals %s -> %s; newly undetected "
      "%s, no longer undetected %s"
      % (dart, bt["undetected"], pt["undetected"], du, bd["landed_not_detected"],
         pd["landed_not_detected"], dl, bd["missed_not_detected"],
         pd["missed_not_detected"], dm, bt["arrivals"], pt["arrivals"],
         ",".join(new) or "none", ",".join(gone) or "none"))
ok = (du == 1 and dl == 1 and dm == 0 and new == [dart] and not gone
      and bt["arrivals"] == pt["arrivals"])
print(("OK   the plant moved the figure by exactly one and named %s" % dart) if ok else
      "FAIL the plant did not move the figure by exactly one onto %s" % dart)
sys.exit(0 if ok else 1)
PY
[ $? -eq 0 ] || exit 1
echo "I1536 DONE censuses under $OUT"
exit 0

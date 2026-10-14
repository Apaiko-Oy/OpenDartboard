#!/bin/bash
# turnaus#1838's harness: one fixture replayed twice by one probe binary, once on a quiet
# container and once under a load this harness makes on purpose, and the published darts
# compared reading by reading.
#
#   testers/run_all.sh 1838-replay-noise   run this
#   testers/i1838_run.sh                   run it against this checkout
#
# The load is made INSIDE the tester's own container (busy loops sharing its --cpus=2
# quota with the probe), bounded by `timeout` and killed by pid, so it is never borrowed
# from whatever else the box is doing and never outlives the replay it loads. No detector
# binary; OD_SKIP_BUILD changes nothing about it.
#
# The script ends on `exit`, never on an `echo`: #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1838"
docker rm -f "$(od_name i1838)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1838" --cpus=2 --network none -e OD_I1838_MUTATE="${OD_I1838_MUTATE:-}" \
  -e OD_I1838_LOADERS="${OD_I1838_LOADERS:-6}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1838 \
  "$OD_IMAGE" bash /app/testers/i1838_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: the load at the end is printed whatever happened.
echo "RUN=1838 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC

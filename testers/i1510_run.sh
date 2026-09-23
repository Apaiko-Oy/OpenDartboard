#!/bin/bash
# #1510: synthetic assertions and negative controls, then measured rig residuals.
# A pass requires synthetic controls and acceptance of all three rig models.
# OD_1510_REQUIRE_ACCEPTED=0 is explicitly measurement-only.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
RUN="$OD_RUNS_BASE/1510"
mkdir -p "$RUN"
od_run 1510 --cpus=2 --network none \
  -e OD_1510_REQUIRE_ACCEPTED="${OD_1510_REQUIRE_ACCEPTED:-1}" \
  -v "$OD_TREE_ROOT":/app:ro -v "$RUN":/out -w /out "$OD_IMAGE" \
  bash /app/testers/i1510_inside.sh 2>&1 | tee "$RUN/out.txt"
exit "${PIPESTATUS[0]}"

#!/bin/bash
# #1335: one command runs every tester in this repository and says which failed.
#
# Six slices merged on 2026-09-18, each gated only against the tree its own agent held,
# and nothing ran the whole of testers/ on the merged result -- there was no way to. Every
# harness named the worktree it was written in, so running a sibling's tester meant copying
# it to scratch and repointing it by hand, and three agents each did that separately while
# two testers sat red on main. This is the command a merge can be gated on.
#
#   testers/run_all.sh                    build, then every tester
#   testers/run_all.sh 1320 1317          only the testers whose label contains one of these
#   OD_SKIP_BUILD=1 testers/run_all.sh    measure the binary already in build/
#   OD_TESTER_TIMEOUT=1800 …              seconds one tester may take (default 1200)
#
# It builds first, and that is not convenience. Every detector tester measures
# build/opendartboard, and the numbers several of them assert belong to the DEV build:
# DEBUG_SEEK_VIDEO seeks a file source three seconds in, so a release binary calibrates on
# a different frame of the same clip and measures a different bull, a different wire count
# and a different camera census. #1319's agent measured both of this issue's "failures" on
# a release build of the same commit this passes on. A gate that does not build the thing
# it measures is gating on whatever was left in the tree.
#
# One tester at a time, deliberately: they run the detector under --cpus=2 with real
# footage and three of them race a clock, so two at once measure each other's load.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

T="$OD_TREE_ROOT/testers"
LOGS="$OD_RUNS_BASE/run_all"
TIMEOUT="${OD_TESTER_TIMEOUT:-1200}"

if ! command -v docker > /dev/null; then
  echo "run_all: docker is not on this machine, and every tester but one runs in $OD_IMAGE" >&2
  exit 2
fi
if ! docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
  echo "run_all: the image $OD_IMAGE is not on this machine; build it with 'docker compose build opendartboard'" >&2
  exit 2
fi

# ---- the testers, in the order a reader would want to see them fail --------------------
# label                       what runs it. Labels are matched as substrings by the
# arguments, so 'run_all.sh 1317' runs both of #1317's.
LABELS=(); CMDS=()
tester() { LABELS+=("$1"); shift; CMDS+=("$*"); }

tester address            "bash '$T/check_default_address.sh'"
tester 1258-choice        "bash '$T/i1258_check.sh'"
tester 1319-findings      "bash '$T/i1319_run.sh'"
tester 1320-speck         "bash '$T/i1320_run.sh'"
tester 1323-offaim        "bash '$T/i1323_run.sh'"
tester 1321-reason        "bash '$T/i1321_run.sh'"
tester 1318-webcam        "bash '$T/i1318_run.sh'"
tester 1338-partial       "bash '$T/i1338_run.sh'"
tester 1331-framing       "bash '$T/i1331_run.sh'"
tester 1339-denominator   "bash '$T/i1339_run.sh'"
tester 1340-floor         "bash '$T/i1340_run.sh'"
tester 1393-carve         "bash '$T/i1393_run.sh'"
tester 1394-windows       "bash '$T/i1394_run.sh'"
tester 1345-figures       "bash '$T/i1345_run.sh'"
tester 1358-window        "bash '$T/i1358_run.sh'"
tester 1355-bounds        "bash '$T/i1355_run.sh'"
tester 1317-partial       "bash '$T/i1317_run.sh'"
tester 899-recover        "bash '$T/i899_run.sh'"
tester 1274-announce      "bash '$T/i1274_run.sh' announce '$T/phases1274/1274-announce.sh'"
tester 1249-control       "bash '$T/i1249_run.sh' '$T/i1249_control.sh'"
tester 1258-control       "bash '$T/i1258_run.sh' '$T/i1258_control.sh'"
tester 1259-control       "bash '$T/i1259_run.sh' '$T/i1259_control.sh'"
tester 1259-pairing       "bash '$T/i1259_check.sh'"
tester 1305-update       "bash '$T/i1305_run.sh'"
tester 1305-manifest     "bash '$T/i1305_check.sh'"
tester 1276-control       "bash '$T/i1276_run.sh' '$T/i1276_control.sh'"
tester 1276-takeout       "bash '$T/i1276_check.sh'"
tester 1257-control       "bash '$T/i1257_run.sh' control '$T/i1257_control.sh'"
tester 1257-resolution    "bash '$T/i1257_run.sh' resolution '$T/i1257_resolution.sh'"
tester 1188-subscribers   "bash '$T/i1247_run.sh' subscribers '$T/phases1247/1188-subscribers.sh' /runs"
tester 822-unreachable    "NET=bridge bash '$T/i1247_run.sh' unreachable '$T/phases1247/822-unreachable.sh' /run822"
tester 892-control        "bash '$T/i1247_run.sh' control '$T/phases1247/892-control.sh' /run892"
tester 895-blind          "bash '$T/i1247_run.sh' blind '$T/phases1247/895-blind.sh' /run895"
tester 895-dark           "bash '$T/i1247_run.sh' dark '$T/phases1247/895-dark.sh' /run895"
tester 891-contest        "bash '$T/i891_run.sh' contest '$T/phases891/contest.sh'"
tester 891-horizon        "bash '$T/i891_run.sh' horizon '$T/phases891/horizon.sh'"
tester 891-givenup        "bash '$T/i891_run.sh' givenup '$T/phases891/givenup.sh'"
tester 891-givenup-nobeat "bash '$T/i891_run.sh' givenup-nobeat '$T/phases891/givenup-nobeat.sh'"
tester 891-unreachable    "bash '$T/i891_run.sh' unreachable '$T/phases891/unreachable.sh'"
# Last, because it builds a second binary under AddressSanitizer and takes longer than
# everything above it together.
tester 1317-asan          "bash '$T/i1317_run.sh' '$T/phases1317/1317-asan.sh'"

# ---- which of them this run is about ---------------------------------------------------
WANTED=("$@")
chosen() {
  [ ${#WANTED[@]} -eq 0 ] && return 0
  local w
  for w in "${WANTED[@]}"; do
    case "$1" in *"$w"*) return 0 ;; esac
  done
  return 1
}

PICK=()
for i in "${!LABELS[@]}"; do
  chosen "${LABELS[$i]}" && PICK+=("$i")
done
if [ ${#PICK[@]} -eq 0 ]; then
  echo "run_all: nothing matches ${WANTED[*]}; the labels are: ${LABELS[*]}" >&2
  exit 2
fi

mkdir -p "$LOGS"
echo "tree:    $OD_TREE_ROOT"
echo "runs:    $OD_RUNS_BASE"
echo "logs:    $LOGS"
echo "testers: ${#PICK[@]} of ${#LABELS[@]}"

# ---- the binary every tester below measures --------------------------------------------
if [ "${OD_SKIP_BUILD:-0}" = "1" ]; then
  echo "build:   skipped (OD_SKIP_BUILD=1); measuring whatever is in build/"
else
  echo "build:   $OD_TREE_ROOT/build/opendartboard, with the dev defines"
  if ! docker run --rm --name "$(od_name build)" --cpus=4 -e HOME=/root \
      -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
        cmake -S /app -B /app/build -DCMAKE_PREFIX_PATH=/usr/local \
          -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" \
          -DAPP_VERSION=0.0.0-dev &&
        cmake --build /app/build -- -j4 --no-print-directory' > "$LOGS/build.log" 2>&1; then
    tail -20 "$LOGS/build.log"
    echo "run_all: the build failed; nothing below it would be measuring this tree" >&2
    exit 2
  fi
fi
echo

FAILED=()
PASSED=0
SUITE0=$(date +%s)
for i in "${PICK[@]}"; do
  label="${LABELS[$i]}"
  log="$LOGS/$label.log"
  printf '%-22s ' "$label"
  t0=$(date +%s)
  timeout "$TIMEOUT" bash -c "${CMDS[$i]}" > "$log" 2>&1
  rc=$?
  t1=$(date +%s)
  took=$((t1 - t0))
  if [ "$rc" = 0 ]; then
    printf 'PASS  %4ds\n' "$took"
    PASSED=$((PASSED + 1))
  else
    if [ "$rc" = 124 ]; then
      printf 'FAIL  %4ds  (no answer in %ss)  %s\n' "$took" "$TIMEOUT" "$log"
    else
      printf 'FAIL  %4ds  (rc=%s)  %s\n' "$took" "$rc" "$log"
    fi
    # The tester said why, in its own words. Show the first few so the gate is readable
    # without opening a file.
    grep -E '^FAIL |^\[ERROR\]' "$log" | head -4 | sed 's/^/                       | /'
    FAILED+=("$label")
  fi
done
SUITE1=$(date +%s)

echo
echo "$PASSED passed, ${#FAILED[@]} failed in $(( (SUITE1 - SUITE0) / 60 ))m$(( (SUITE1 - SUITE0) % 60 ))s"
if [ ${#FAILED[@]} -ne 0 ]; then
  echo "FAILED: ${FAILED[*]}"
  exit 1
fi
echo "every tester green"

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
#   OD_TESTER_TIMEOUT=1800 …              seconds one tester may take (default 1200), and
#                                         it governs every tester, `slow` ones included
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
LABELS=(); CMDS=(); LIMITS=()
tester() { LABELS+=("$1"); LIMITS+=("$TIMEOUT"); shift; CMDS+=("$*"); }

# A tester whose honest cost is more than the default, with the measured number beside it
# (#1341). Before this, 1317-asan -- the memory-safety check, and the most expensive thing
# in this file -- was given the same 1200 s as a tester that runs the detector for forty
# seconds, and on a loaded box it reported `no answer in 1200s`, which is not a finding and
# reads exactly like one. Write the measurement into the comment beside the call: a number
# nobody measured is how the 1200 itself got here. OD_TESTER_TIMEOUT still wins where it is
# set, so a run that wants everything to fail fast still can.
slow() { [ -n "${OD_TESTER_TIMEOUT:-}" ] || LIMITS[$(( ${#LABELS[@]} - 1 ))]="$1"; }

tester address            "bash '$T/check_default_address.sh'"

# #1452: and this one is about release.yml rather than about the detector, so it costs no
# container and no build either. It asks whether a pull request can still reach the Windows
# job and still publish nothing when it does -- both of which can stop being true with no
# diff to the trigger, because GitHub skips a job whose `needs` was skipped and says so in
# grey. It compiles nothing and reads no C++: #1452 rejected a lint for nonstandard
# identifiers on the grounds that a check which looks like a Windows build and is not one
# is worse than none, and this is not that check.
tester 1452-pr-build      "bash '$T/i1452_pr_build_check.sh'"

# The census first, because it is about this list itself and costs no container: a tester
# this file does not name is outside the gate, which is #1335's own shape one level down
# (#1371). Then the pure checks -- a compile and a few milliseconds each, so they are the
# cheapest place for a reader to learn the build is broken.
tester census             "bash '$T/census.sh'"
# Then the leak check, for the same reason and with the same cost: it starts containers but
# every one of them is `sleep`, and what it measures -- that a killed harness leaves nothing
# behind -- is a property of every tester below it (#1341).
tester leaks              "bash '$T/leak_check.sh'"
tester 1346-vote          "bash '$T/unit_check.sh' 1346"
tester 1347-sector        "bash '$T/unit_check.sh' 1347"
tester 1349-background    "bash '$T/unit_check.sh' 1349"
tester 1350-vote-line     "bash '$T/unit_check.sh' 1350"
tester 1351-ledger        "bash '$T/unit_check.sh' 1351"
tester 1363-anchor        "bash '$T/unit_check.sh' 1363"

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
tester 1392-annulus       "bash '$T/i1392_run.sh'"
tester 1393-carve         "bash '$T/i1393_run.sh'"
tester 1394-windows       "bash '$T/i1394_run.sh'"
tester 1345-figures       "bash '$T/i1345_run.sh'"
tester 1358-window        "bash '$T/i1358_run.sh'"
tester 1355-bounds        "bash '$T/i1355_run.sh'"
tester 1348-quorum        "bash '$T/i1348_run.sh'"
tester 1372-cached        "bash '$T/i1372_run.sh'"
tester 1389-floor         "bash '$T/i1389_run.sh'"
tester 1317-partial       "bash '$T/i1317_run.sh'"
tester 1330-ownership     "bash '$T/i1330_run.sh'"
tester 899-recover        "bash '$T/i899_run.sh'"
tester 1274-announce      "bash '$T/i1274_run.sh' announce '$T/phases1274/1274-announce.sh'"
tester 1249-control       "bash '$T/i1249_run.sh' '$T/i1249_control.sh'"
tester 1258-control       "bash '$T/i1258_run.sh' '$T/i1258_control.sh'"
tester 1259-control       "bash '$T/i1259_run.sh' '$T/i1259_control.sh'"
tester 1259-pairing       "bash '$T/i1259_check.sh'"
tester 1305-update       "bash '$T/i1305_run.sh'"
tester 1305-manifest     "bash '$T/i1305_check.sh'"
# #1408 is the writing half of the same path and needs no container and no build: openssl,
# a throwaway key and the script release.yml really calls. It sits here beside the reading
# half rather than with the pure checks, because what it is about is the pair.
tester 1408-signing      "bash '$T/i1408_sign_check.sh'"
tester 1276-control       "bash '$T/i1276_run.sh' '$T/i1276_control.sh'"
tester 1276-takeout       "bash '$T/i1276_check.sh'"
tester 1366-position      "bash '$T/i1366_run.sh'"
tester 1257-control       "bash '$T/i1257_run.sh' control '$T/i1257_control.sh'"
tester 1257-resolution    "bash '$T/i1257_run.sh' resolution '$T/i1257_resolution.sh'"
# Of the five phases under phases1247/, one computes a verdict and four record (#1412).
# 1188-subscribers runs check_subscribers.py, which exits 0 when every check held and 1
# when one did not -- until #1412 the phase script ended on `echo "CHECK_RC=$?"`, so the
# container exited 0 whatever the check said and this label could not go red. The other
# four run the detector through a situation and leave a transcript for a reader; they
# assert nothing, so a PASS beside them means "it ran to the end" and not "it held". Each
# says so in its own first lines, including what its exit status does and does not carry.
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
tester 1282-footage       "bash '$T/i1282_run.sh'"
tester 1303-launcher      "bash '$T/i1303_check.sh'"
tester 1306-install      "bash '$T/i1306_check.sh'"
tester 1334-networkless   "bash '$T/i1334_run.sh'"
# #1334's other half measures what systemd does with templates/*.service.template, so it
# needs a live manager and a live journal where everything here runs in a container with
# no init. It is deliberately outside this list and carries a marker saying so.
# #1388: added at the end rather than beside 899-recover, which is the lifecycle it
# extends, because it builds a second binary from a scratch copy of the tree and takes
# several minutes -- 1317-asan's reason, and it belongs in 1317-asan's half of the list.
tester 1388-budget        "bash '$T/i1388_run.sh'"
# Last, because it builds a second binary under AddressSanitizer and takes longer than
# everything above it together.
tester 1317-asan          "bash '$T/i1317_run.sh' '$T/phases1317/1317-asan.sh'"
# MEASURED 2026-09-19 on the 4-core box, to completion, rc=0: wall 601 s with
# host_busy_pct=97.0 and the load average going 3.2 -> 12.9 under it, which is to say the
# 601 s is already a busy-box number and not a quiet-box one. Two ASan builds is most of
# it: this tree's, and the base commit's for the half of #1317's question that asks what
# the OLD build did on the same footage.
#
# 2400 rather than 1200 because 1200 is the number that failed. The baseline of 2026-09-18
# reported `no answer in 1200s` -- the only red of thirty, and not a finding -- so the
# honest budget is twice the limit that was actually exceeded rather than twice the cost
# measured here. Raising it does not make a HUNG run cheap, and it is not meant to: a
# timeout bounds a run that is stuck, and this one only ever bounded a run that was slow.
slow 2400

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
  if ! od_run build --cpus=4 -e HOME=/root \
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

# #1341: run_all is the last thing still alive when a tester is killed outright, so it
# sweeps this tree's containers on the way out, whichever way it goes out. Installed here
# rather than at the top because the build above runs under od_run's own traps.
trap 'od_sweep; trap - TERM; trap - EXIT; kill -TERM $$' TERM
trap 'od_sweep; trap - INT;  trap - EXIT; kill -INT  $$' INT
trap 'od_sweep' EXIT

FAILED=()
PASSED=0
SUITE0=$(date +%s)
for i in "${PICK[@]}"; do
  label="${LABELS[$i]}"
  log="$LOGS/$label.log"
  printf '%-22s ' "$label"
  t0=$(date +%s)
  timeout "${LIMITS[$i]}" bash -c "${CMDS[$i]}" > "$log" 2>&1
  rc=$?
  t1=$(date +%s)
  took=$((t1 - t0))
  # #1341: a harness reaps its own container however it dies short of SIGKILL, and this is
  # what catches the SIGKILL. It is also the only place a leak is REPORTED rather than
  # merely prevented, which matters: a container quietly reaped is a fault nobody learns
  # about, and this one was met twice by hand before anybody wrote it down.
  leaked="$(od_sweep)"
  if [ "$rc" = 0 ]; then
    printf 'PASS  %4ds\n' "$took"
    PASSED=$((PASSED + 1))
  else
    if [ "$rc" = 124 ]; then
      printf 'FAIL  %4ds  (no answer in %ss)  %s\n' "$took" "${LIMITS[$i]}" "$log"
    else
      printf 'FAIL  %4ds  (rc=%s)  %s\n' "$took" "$rc" "$log"
    fi
    # The tester said why, in its own words. Show the first few so the gate is readable
    # without opening a file.
    grep -E '^FAIL |^\[ERROR\]' "$log" | head -4 | sed 's/^/                       | /'
    FAILED+=("$label")
  fi
  if [ -n "$leaked" ]; then
    printf '%s\n' "$leaked" >> "$log"
    printf '%s\n' "$leaked" | sed 's/^/                       | /'
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

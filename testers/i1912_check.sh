#!/bin/bash
# turnaus#1912: darts never wait behind an END's log and frames uploads. Two checks, one
# container, the predictions stated in each source's header:
#
#   1. i1912_schedule_check.cpp  PURE. The scheduling rule on src/communication/duty_gate.hpp,
#                                the class the client uses: a dart queued mid-upload is posted
#                                while the upload is in flight and settled before it resumes.
#                                Two mutations inside (#1787's inline duties; a gate never
#                                asked), each caught by the assertion its prediction names.
#   2. i1912_wire_check.cpp      THE #1787 STUB HARNESS. The real TurnausClient against
#                                testers/i1912_stub.py: the END's log and frames answer
#                                arrive, a dart offered 2 s into a 10 s frames upload is
#                                answered under 1 s, and the push thread's posts share a
#                                connection (keep-alive); a 503 on one dart is said once at
#                                INFO with its backoff, and its ANSWER_MS names tries=2. Run TWICE: on this tree, and on
#                                ace2ade's src/ (the tree #1912 was cut from) as the control,
#                                which is PREDICTED to fail the 1 s with the dart answered
#                                only after the hold ends, and to open one connection per post.
#
# The include paths and the link are i1787_check.sh's. No detector binary, so OD_SKIP_BUILD
# changes nothing beyond build/_deps. The stub is stdlib python inside $OD_IMAGE, on
# 127.0.0.1 in a --network none container.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1912_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

# The control's sources: the parent tree's src/, exported read-only beside this run.
BEFORE="$OD_RUNS_BASE/i1912_before"
rm -rf "$BEFORE" && mkdir -p "$BEFORE"
if ! git -C "$OD_TREE_ROOT" archive ace2ade src | tar -x -C "$BEFORE"; then
  echo "i1912_check: could not export ace2ade's src/ for the control" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1912-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -v "$BEFORE":/before:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  DEPS="-I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src"
  LIBS="$(pkg-config --cflags --libs opencv4) -lpthread"
  RC=0

  echo "==== i1912_schedule_check ======================================================"
  g++ -std=c++17 -O1 -Wall -Wextra -I src -o /tmp/i1912_schedule_check testers/i1912_schedule_check.cpp -lpthread \
      || { echo "COMPILE_FAILED i1912_schedule_check"; exit 2; }
  /tmp/i1912_schedule_check < /dev/null
  rc=$?; echo "i1912_schedule_check rc=$rc"; [ $rc -eq 0 ] || RC=1

  wire() {  # <label> <src root> <port>
    local label=$1 root=$2 port=$3 work=/tmp/wire-$1
    mkdir -p "$work"
    g++ -std=c++17 -O1 -Wall -Wextra -I "$root/src" -I "$root/src/utils" $DEPS -o "$work/drive" \
        testers/i1912_wire_check.cpp "$root/src/communication/turnaus_client.cpp" $LIBS \
        || { echo "COMPILE_FAILED i1912_wire_check ($label)"; return 2; }
    STUB_PORT=$port STUB_TRANSCRIPT="$work/stub.jsonl" FRAMES_HOLD_S=10 \
        python3 testers/i1912_stub.py > "$work/stub.out" 2>&1 &
    local stub=$!
    for i in $(seq 1 50); do grep -q listening "$work/stub.out" 2>/dev/null && break; sleep 0.1; done
    HOME="$work" OD_KEEP_FRAMES=on "$work/drive" "$port" "$work" > "$work/drive.out" 2>&1
    local drc=$?
    kill $stub 2>/dev/null; wait $stub 2>/dev/null
    grep -E "WIRE_|PASS|FAIL|ANSWER_MS|not delivered|TURNAUS: frames" "$work/drive.out"
    python3 - "$work/stub.jsonl" "$label" <<"PY"
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
label = sys.argv[2]
req = [r for r in rows if r["event"] == "request"]
def first(pred):
    return next((r for r in rows if pred(r)), None)
held = first(lambda r: r["event"] == "frames_hold_begun")
ended = first(lambda r: r["event"] == "frames_hold_ended")
d13 = first(lambda r: r["event"] == "request" and r.get("sector") == "D13")
logs = [r for r in req if r["path"] == "/api/v1/autoscorer/log"]
push = [r for r in req if r["path"] in ("/api/v1/autoscorer/detections", "/api/v1/autoscorer/takeouts")]
ok = True
def say(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + "[%s] %s" % (label, what))
    ok = ok and cond
say(any(r["log_has_marker"] for r in logs), "the END\x27s log arrived (%d chunk(s), marker line in it)" % len(logs))
say(held is not None and ended is not None, "the END\x27s frames answer arrived and was taken")
say(d13 is not None and held is not None and ended is not None and held["t"] < d13["t"] < ended["t"],
    "D13 reached the stub while the frames upload was in flight")
conns = sorted(set(r["connection"] for r in push))
print("WIRE_PUSH_POSTS %d on %d connection(s)" % (len(push), len(conns)))
# S20, T19 (refused once, so twice), the takeout and D13: five posts, from the one push thread.
say(len(push) >= 4 and len(conns) < len(push), "the push thread\x27s %d posts shared a kept connection" % len(push))
sys.exit(0 if ok else 1)
PY
    local prc=$?
    local src=0
    if [ "$label" = after ]; then
      # The retry is said once, at INFO, with the reason and the backoff; and the dart it
      # was about still has its one ANSWER_MS line, naming the second try.
      if grep -qE "INFO.*TURNAUS: dart [0-9A-Z]+ not delivered on try 1 \(HTTP 503\); trying again in [0-9]+ ms" "$work/drive.out" \
         && [ "$(grep -cE "TURNAUS: dart [0-9A-Z]+ not delivered" "$work/drive.out")" = 1 ] \
         && grep -qE "ANSWER_MS [0-9]+ dart [0-9A-Z]+ HTTP 202 COUNTED tries=2" "$work/drive.out"; then
        echo "PASS [after] the 503 and its retry are said once at INFO, and ANSWER_MS names tries=2"
      else
        echo "FAIL [after] the 503 and its retry were not said as #1912 says them"; src=1
      fi
    fi
    [ $drc -eq 0 ] && [ $prc -eq 0 ] && [ $src -eq 0 ]
  }

  echo "==== i1912_wire_check, this tree ==============================================="
  wire after /app 8912; rc=$?; echo "i1912_wire_check rc=$rc"; [ $rc -eq 0 ] || RC=1

  echo "==== i1912_wire_check, the control: ace2ade (before #1912) ====================="
  echo "PREDICTION: FAIL on the 1 s and on D13 inside the hold; one connection per push post"
  wire before /before 8913; rc=$?
  echo "i1912_wire_check control rc=$rc"
  if [ $rc -eq 0 ]; then
    echo "FAIL the control passed: the harness cannot see a dart waiting behind an upload"; RC=1
  else
    echo "PASS the control fails as predicted"
  fi
  exit $RC
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
exit $RC

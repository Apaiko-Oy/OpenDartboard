#!/bin/bash
# turnaus#1838, inside the container. Everything here runs against /app, which is the tree,
# and writes to /run1838.
#
# The claim: with the detection windows pinned to cycles (OD_WINDOW_UNIT=cycles, #1826)
# AND the motion clock on the footage (OD_MOTION_CLOCK=capture, which the probe now winds:
# testers/replay_clock.hpp), one probe binary replaying mocks/rig-20260918 publishes the
# same darts on a quiet container and on a loaded one. The windows pin alone does not say
# that: the cooldown, the spike window, the safety timeout and the body-window vote are
# milliseconds of od_clock::now_ms(), and on the default motion clock that is the wall.
#
# Four things are asserted, and three of them are about the instrument:
#   1. the probe ran on the capture clock and WOUND it -- motionMs at the end is the
#      footage's length, not 0 (#1535's capture-clock probe ran on a clock stuck at 0);
#   2. the load was real -- the loaded replay took measurably longer than the quiet one,
#      because a "loaded" run the load never reached is a second quiet run;
#   3. both replays published darts and ran the same number of cycles;
#   4. the two replays' I1492DART readings are identical, and if they are not, every
#      (dart, camera) that differs is named with both readings.
#
# A third replay, on the WALL motion clock under the same load, is printed and NOT
# asserted: it is the noise the pin removes, and whether one box's load reaches it is a
# fact about the box (README rule 5).
#
# OD_I1838_MUTATE=wall runs the asserted loaded replay on the wall clock -- the guard
# taken away -- for the mutation proof recorded in the commit.
#
# It never ends on an `echo` (#1479): the last statement is `exit $FAILED`.
set -u
export OD_WINDOW_UNIT=cycles

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
OUT=/run1838
PROBE="$OUT/probe"
CLIPS="$SRC/mocks/rig-20260918/cam_1.mp4 $SRC/mocks/rig-20260918/cam_2.mp4 $SRC/mocks/rig-20260918/cam_3.mp4"
LOADERS="${OD_I1838_LOADERS:-6}"

echo "=== building the tip probe (i1492_tip_probe.cpp, the probe 1492-tips and 1494-figure replay with) ==="
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$PROBE" "$SRC/testers/i1492_tip_probe.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  "$SRC"/src/detector/geometry/detection/score_processing.cpp \
  "$SRC"/src/detector/geometry/detection/dart_processing.cpp \
  "$SRC"/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > "$OUT/build.log" 2>&1
if [ $? -ne 0 ]; then
  tail -40 "$OUT/build.log"
  echo "FAIL the tip probe did not build; nothing below measures anything"
  exit 2
fi

# replay <tag> <clock> <loaded 0|1>: one whole replay, its seconds into $OUT/<tag>.secs
replay() {
  local tag="$1" clock="$2" loaded="$3" pids=() i t0 t1 rc
  if [ "$loaded" = 1 ]; then
    # The load: busy loops sharing this container's CPU quota with the probe. Each is
    # bounded by `timeout` and killed by its own pid below; none outlives this function.
    for i in $(seq "$LOADERS"); do
      timeout 1200 sh -c 'while :; do :; done' &
      pids+=($!)
    done
  fi
  t0=$(date +%s.%N)
  OD_MOTION_CLOCK="$clock" OD_TIP_CENSUS=1 "$PROBE" "" $CLIPS > "$OUT/rows-$tag.txt" 2> "$OUT/pipeline-$tag.log"
  rc=$?
  t1=$(date +%s.%N)
  for i in "${pids[@]}"; do kill "$i" 2> /dev/null; done
  for i in "${pids[@]}"; do wait "$i" 2> /dev/null; done
  echo "$t0 $t1" | awk '{printf "%.1f\n", $2 - $1}' > "$OUT/$tag.secs"
  [ "$rc" -eq 0 ] || { tail -20 "$OUT/pipeline-$tag.log"; say "FAIL the $tag replay exited $rc" no; }
  echo "  $tag: motion clock $clock, $([ "$loaded" = 1 ] && echo "under $LOADERS busy loops" || echo quiet)," \
    "$(cat "$OUT/$tag.secs") s, $(grep '^I1492END ' "$OUT/rows-$tag.txt")"
}

LOADED_CLOCK=capture
[ "${OD_I1838_MUTATE:-}" = wall ] && { LOADED_CLOCK=wall; echo "MUTATION: the loaded replay runs on the wall clock"; }

echo
echo "=== the replays ===================================================================="
replay quiet capture 0
replay loaded "$LOADED_CLOCK" 1
replay loaded-wall wall 1

python3 - "$OUT" <<'PY'
import sys
out = sys.argv[1]

def read(tag):
    rows, end = {}, {}
    for line in open(f"{out}/rows-{tag}.txt"):
        f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
        if line.startswith("I1492DART "):
            rows[(int(f["dart"]), int(f["cam"]))] = line.split(None, 1)[1].strip()
        elif line.startswith("I1492END "):
            end = f
    return rows, end

def secs(tag):
    return float(open(f"{out}/{tag}.secs").read())

def differing(a, b):
    return [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]

def name(a, b, keys, indent="       "):
    for k in keys:
        print(f"{indent}dart {k[0]} cam {k[1]}:")
        print(f"{indent}  quiet:  {a.get(k, '(no such reading)')}")
        print(f"{indent}  loaded: {b.get(k, '(no such reading)')}")

failed = False
def say(ok, text):
    global failed
    print(("OK   " if ok else "FAIL ") + text)
    failed |= not ok

q, qe = read("quiet")
l, le = read("loaded")
w, we = read("loaded-wall")
print()

# 1. the clock the claim is about is the one that ran, and it was wound
cycles = int(qe.get("cycles", 0))
ms = int(qe.get("motionMs", 0))
played = cycles * 1000.0 / 30.0
wound = qe.get("motionClock") == "capture" and cycles > 0 and played <= ms <= played + 5000
say(wound, f"the quiet replay ran on the capture clock and wound it: {cycles} cycles, motionMs={ms} "
           f"(the {played:.0f} ms they play at 30 fps, plus up to 5000 for the calibration seek and average)")

# 2. the load reached the replay
ratio = secs("loaded") / max(secs("quiet"), 0.1)
say(ratio >= 1.3, f"the load reached the replay: loaded {secs('loaded'):.1f} s against quiet {secs('quiet'):.1f} s "
                  f"({ratio:.2f}x, at least 1.30x asked)")

# 3. both replays measured something, over the same footage
say(int(qe.get("darts", 0)) > 0 and qe.get("cycles") == le.get("cycles"),
    f"both replays published darts over the same cycles: quiet {qe.get('darts')} darts / {qe.get('cycles')} cycles, "
    f"loaded {le.get('darts')} darts / {le.get('cycles')} cycles")

# 4. the claim
d = differing(q, l)
say(not d, f"one binary, one fixture, quiet and loaded on the capture clock: {len(q)} readings against {len(l)}, "
           f"{len(d)} differ" + ("" if not d else " -- the darts that differ:"))
name(q, l, d)

print()
dw = differing(q, w)
print(f"  NOT ASSERTED -- the same load on the WALL motion clock (what OD_WINDOW_UNIT=cycles alone leaves):")
print(f"  {len(w)} readings against the quiet replay's {len(q)}, {len(dw)} differ, "
      f"{secs('loaded-wall'):.1f} s, darts {we.get('darts')}")
name(q, w, dw[:12], indent="    ")
if len(dw) > 12:
    print(f"    ... and {len(dw) - 12} more")
sys.exit(1 if failed else 0)
PY
[ $? -eq 0 ] || FAILED=1

echo
if [ "$FAILED" = 0 ]; then echo "i1838: PASS"; else echo "i1838: FAIL"; fi
exit $FAILED

#!/bin/bash
# #1586, inside the container: the cause census of TOO-FEW-CONSTRAINTS and the composite
# rescue's before/after, on ONE binary.
#
# Eight whole-clip replays, #1555's shape and cost apiece: both ground-truthed fixtures,
# both calibration windows (#1551: `dev` is the registry build's 3 s seek, `opening` is
# OD_SEEK_VIDEO=off), each once with the rescue live (OD_AXIS_RESCUE=on, opt-in) and
# once on the DEFAULT binary (the `pin` arm), which is the pre-#1586 exclusion. The
# before/after is measured on the same build in the same container, so no dart's verdict
# can move for any reason but the rescue.
#
# Every run is read by two censuses: testers/i1586_census.py (why each refused dart was
# refused, per camera, per reason) and testers/i1555_census.py (the published verdict per
# dart against the truth -- run, never edited: #1587 owns it).
#
# WHAT IS ASSERTED:
#   - the default restores the old exclusion: no default-arm run prints a single
#     I1586RESCUE line (the rescue never looked);
#   - the live arm really rescued something (a rescue that never fires is not a repair);
#   - TOO-FEW-CONSTRAINTS over matched darts falls with the rescue on, pooled, and rises
#     in no run.
# WHAT IS REPORTED, as the issue's acceptance verdict on the opt-in path: pooled
# published exact before/after, and every dart whose verdict changes, by name, with
# REGRESSED on any dart exact on the default and not with the rescue on. These are NOT
# asserted because the rescue is not the default: the default binary is the pre-#1586
# one, byte for byte, so nothing published regresses -- and the measurement is exactly
# why the rescue is not the default (shaft_axis.hpp, AxisParams::rescue_composite).
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u

BIN=/app/build/opendartboard
RUN=/run1586
T18=/app/mocks/rig-20260918/GROUND-TRUTH.md
T22=/app/testers/i1499_truth_rig20260922.md
A18=/app/testers/i1511_annotations/rig-20260918.csv
A22=/app/testers/i1511_annotations/rig-20260922.csv

if [ ! -x $BIN ]; then echo "FAIL no $BIN"; exit 1; fi
for f in $T18 $T22 $A18 $A22; do
    if [ ! -s $f ]; then echo "FAIL $f is missing"; exit 1; fi
done

run_detector() { # $1 fixture dir, $2 output basename, $3.. extra env as VAR=value
    local dir="/app/mocks/$1" out="$2"
    shift 2
    local cams="$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4"
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 "$@" \
        timeout 900 $BIN --cams "$cams" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    rm -f "$RUN/$out.out"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    if [ $rc -ne 0 ] && [ $rc -ne 124 ]; then
        tail -5 "$RUN/$out.txt"
        echo "FAIL the $out run did not finish"
        exit 1
    fi
}

censuses() { # $1 log basename, $2 fixture, $3 window
    local out="$1" fixture="$2" window="$3" T A NA
    if [ "$fixture" = rig-20260918 ]; then T=$T18; A=$A18; NA=""; else T=$T22; A=$A22; NA="--no-arrival 1.1"; fi
    python3 /app/testers/i1586_census.py --log "$RUN/$out.txt" --truth $T --annotations $A \
        --fixture $fixture --window $window $NA > "$RUN/cause-$out.txt" || return 1
    python3 /app/testers/i1555_census.py --log "$RUN/$out.txt" --truth $T --annotations $A \
        --fixture $fixture --window $window --min-matched 2 $NA > "$RUN/bake-$out.txt" || return 1
    grep -h '^I1586 \(CAUSE\|TALLY\)' "$RUN/cause-$out.txt"
    grep -h '^I1555 TALLY' "$RUN/bake-$out.txt"
}

n=0
for spec in rig-20260918:dev:r18-dev: rig-20260918:opening:r18-open:OD_SEEK_VIDEO=off \
            rig-20260922:dev:r22-dev: rig-20260922:opening:r22-open:OD_SEEK_VIDEO=off; do
    IFS=: read -r fixture window name seek <<< "$spec"
    for arm in live pin; do
        n=$((n + 1))
        echo "=== $n. $fixture, $window window, rescue $arm ======================================"
        extra=()
        [ -n "$seek" ] && extra+=("$seek")
        [ $arm = live ] && extra+=(OD_AXIS_RESCUE=on)
        run_detector $fixture "$name-$arm" "${extra[@]}"
        censuses "$name-$arm" $fixture $window || { echo "FAIL the $name-$arm census could not be read"; exit 1; }
        echo "rescue lines: $(grep -c 'I1586RESCUE' "$RUN/$name-$arm.txt") tried, $(grep -c 'I1586RESCUE .*rescued=1' "$RUN/$name-$arm.txt") rescued"
    done
done

echo "=== the cause census, pooled (the default arm is the pre-#1586 binary) ==============="
python3 /app/testers/i1586_census.py --pool "$RUN"/cause-*-pin.txt | tee "$RUN/pool-pin.txt"
echo "--- and with the rescue live"
python3 /app/testers/i1586_census.py --pool "$RUN"/cause-*-live.txt | tee "$RUN/pool-live.txt"

echo "=== assertions ========================================================================"
python3 - "$RUN" <<'PY'
import re, sys, os
run = sys.argv[1]
fail = 0
def bad(msg):
    global fail
    fail += 1
    print("FAIL " + msg)

pair = re.compile(r"^I1555 PAIR v(\d+\.\d+) thrown=(\S+) .* published=(\S+) (\S+) path=(\S+) "
                  r"conf=(\S+) degraded=\d outcome=(\S+)")
def pairs(path):
    out = {}
    for line in open(path, errors="replace"):
        m = pair.search(line)
        if m:
            out[m.group(1)] = {"thrown": m.group(2), "pub": m.group(3), "verdict": m.group(4),
                               "path": m.group(5), "conf": m.group(6), "outcome": m.group(7)}
    return out

tot = {"live": [0, 0, 0], "pin": [0, 0, 0]}
regressed = []   # refused, published exact, matched
for name in ("r18-dev", "r18-open", "r22-dev", "r22-open"):
    rescues_pin = sum(1 for l in open(os.path.join(run, name + "-pin.txt"), errors="replace")
                      if "I1586RESCUE" in l)
    if rescues_pin:
        bad("%s: the default run printed %d I1586RESCUE line(s) -- the default binary is "
            "not the old exclusion" % (name, rescues_pin))
    live, pin = pairs(os.path.join(run, "bake-%s-live.txt" % name)), pairs(os.path.join(run, "bake-%s-pin.txt" % name))
    ref = {k: sum(1 for p in d.values() if p["outcome"] == "TOO-FEW-CONSTRAINTS") for k, d in (("live", live), ("pin", pin))}
    ex = {k: sum(1 for p in d.values() if p["verdict"] == "exact") for k, d in (("live", live), ("pin", pin))}
    for k, d in (("live", live), ("pin", pin)):
        tot[k][0] += ref[k]; tot[k][1] += ex[k]; tot[k][2] += len(d)
    print("I1586 BEFORE-AFTER %s matched=%d/%d refused %d -> %d | published exact %d -> %d"
          % (name, len(pin), len(live), ref["pin"], ref["live"], ex["pin"], ex["live"]))
    if ref["live"] > ref["pin"]:
        bad("%s: TOO-FEW-CONSTRAINTS rose %d -> %d" % (name, ref["pin"], ref["live"]))
    for key in sorted(set(pin) | set(live), key=lambda s: tuple(int(x) for x in s.split("."))):
        a, b = pin.get(key), live.get(key)
        if a is None or b is None:
            print("I1586 CHANGED %s v%s matched in only one arm: pin=%s live=%s"
                  % (name, key, a and a["pub"], b and b["pub"]))
            if a is not None and a["verdict"] == "exact":
                regressed.append("%s v%s" % (name, key))
                print("I1586 REGRESSED %s v%s: exact on the default and unmatched with the rescue on" % (name, key))
            continue
        if (a["pub"], a["outcome"]) != (b["pub"], b["outcome"]):
            print("I1586 CHANGED %s v%s thrown=%s | pin %s %s (%s, %s) -> live %s %s (%s, %s)"
                  % (name, key, a["thrown"], a["pub"], a["verdict"], a["path"], a["outcome"],
                     b["pub"], b["verdict"], b["path"], b["outcome"]))
        if a["verdict"] == "exact" and b["verdict"] != "exact":
            regressed.append("%s v%s" % (name, key))
            print("I1586 REGRESSED %s v%s: exact %s on the default, %s (%s) with the rescue on"
                  % (name, key, a["pub"], b["pub"], b["verdict"]))

print("I1586 POOLED-BEFORE-AFTER matched %d/%d | TOO-FEW-CONSTRAINTS %d -> %d | published exact %d -> %d"
      % (tot["pin"][2], tot["live"][2], tot["pin"][0], tot["live"][0], tot["pin"][1], tot["live"][1]))
rescued = sum(1 for n in ("r18-dev", "r18-open", "r22-dev", "r22-open")
              for l in open(os.path.join(run, n + "-live.txt"), errors="replace")
              if "I1586RESCUE" in l and "rescued=1" in l)
if rescued == 0:
    bad("the live arm rescued no axis anywhere -- the repair never fired")
if not tot["live"][0] < tot["pin"][0]:
    bad("pooled TOO-FEW-CONSTRAINTS did not fall (%d -> %d)" % (tot["pin"][0], tot["live"][0]))
met = tot["live"][1] > tot["pin"][1] and not regressed
print("I1586 ACCEPTANCE on the opt-in path: published exact %d -> %d, %d regressed (%s) -- %s"
      % (tot["pin"][1], tot["live"][1], len(regressed), ", ".join(regressed) or "none",
         "MET" if met else "NOT MET, which is why OD_AXIS_RESCUE is opt-in"))
print("I1586 VERDICT %s (%d failure(s))" % ("PASS" if not fail else "FAIL", fail))
sys.exit(1 if fail else 0)
PY
RC=$?
echo "I1586 DONE logs and censuses under $RUN"
exit $RC

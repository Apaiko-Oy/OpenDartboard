#!/bin/bash
# #1796: what runs inside the container for testers/i1796_check.sh. Read that file first.
#
# THE SHAPE OF A PI, IN /tmp. One directory the way the unit has one (STATE_DIRECTORY,
# #1660), two binaries beside each other the way the deb puts them in /usr/local/bin, a
# credential whose base_url names the deployment, a channel file, a camera file -- and the
# launcher CMake built from this tree, started the way the unit starts it: an input nobody
# answers, every argument the detector's, in order.
set -u

TREE=/app
WORK=/tmp/i1796
FIX="$WORK/fixtures"
WWW="$WORK/www"
LOG="$WORK/access.log"
PORT=8796
STATE="$WORK/state"      # the unit's /var/lib/opendartboard
BIN="$WORK/bin"          # the deb's /usr/local/bin
MANIFEST=stable-linux-arm64
# The four releases this board lives through, named by version because the stub's
# behaviour is decided by its own version (testers/i1306_stub.cpp): A is the deb's, B is
# the first release over the wire, BAD will not start, C stops itself on schedule once.
A=v1.0.0
B=v1.1.0
BAD=v1.3.0
C=v1.2.0

rm -rf "$WORK" && mkdir -p "$FIX" "$WWW/updates/opendartboard" "$WWW/rel" "$WORK/stubs" "$STATE" "$BIN" "$WORK/argv" "$WORK/logs"

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
fact() { sed -n "s/^$2=//p" "$1/update/state.txt"; }

if [ "${MODE:-}" = "--mutate" ]; then
  # A tree CMake can configure whose src/ is a copy with the suffix dropped: everything
  # else is a symlink into /app, so nothing of the worktree is written.
  rm -rf /tmp/mutated-tree && mkdir -p /tmp/mutated-tree
  for entry in /app/* /app/.[!.]*; do [ -e "$entry" ] && ln -s "$entry" "/tmp/mutated-tree/$(basename "$entry")"; done
  rm -f /tmp/mutated-tree/src /tmp/mutated-tree/build && cp -r /app/src /tmp/mutated-tree/src
  python3 - <<'PY' || { echo MUTATION_FAILED; exit 2; }
path = "/tmp/mutated-tree/src/update/update_check.hpp"
text = open(path, encoding="utf-8").read()
gone = '    inline std::string platformSuffix() { return std::string("-") + OD_UPDATE_PLATFORM; }\n'
assert gone in text, "platformSuffix() is not where the mutation expects it"
open(path, "w", encoding="utf-8").write(text.replace(gone, "    inline std::string platformSuffix() { return std::string(); }\n"))
print("MUTATED: platformSuffix() is empty whatever OD_UPDATE_PLATFORM says; a Linux build asks the Windows path")
PY
  TREE=/tmp/mutated-tree
fi
SRC="$TREE/src"
DEPS="-I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"

# ---- the stubs: one per version, the way the real artefact carries its own -------------
for V in "$A" "$B" "$BAD"; do
  g++ -O1 -std=c++17 -Wall -Wextra -DAPP_VERSION="\"$V\"" \
      /app/testers/i1306_stub.cpp -o "$WORK/stubs/stub-$V" || { echo COMPILE_FAILED; exit 2; }
done
# C is #1797's stub: exit 60 on its first start, 0 on its second.
g++ -O1 -std=c++17 -Wall -Wextra -DAPP_VERSION="\"$C\"" \
    /app/testers/i1797_stub.cpp -o "$WORK/stubs/stub-$C" || { echo COMPILE_FAILED; exit 2; }

# ---- real tar.gz archives, real digests, real signatures ---------------------------------
python3 /app/testers/i1796_fixtures.py "$FIX" "$WORK/stubs" "$MANIFEST" "$A" "$B" "$BAD" "$C" || { echo FIXTURES_FAILED; exit 2; }
cp "$FIX"/rel/*.tar.gz "$WWW/rel/"
ANCHOR=$(tr -d '\n\r' < "$FIX/anchor.hex")

# ---- the launcher, built by THIS TREE'S CMakeLists.txt and nothing else -------------------
# Its own build directory, the fixture's anchor compiled in (there is no runtime way, and
# there must never be one: update_keys.hpp), the platform the Pi's build defines, and the
# version the deb's launcher would carry. Only the launcher target is built: one file.
echo "---- building the Linux launcher target with cmake ----"
mkdir -p "$WORK/build" && cp -r /app/build/_deps "$WORK/build/_deps"
cmake -S "$TREE" -B "$WORK/build" -DCMAKE_PREFIX_PATH=/usr/local -DAPP_VERSION="$A" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DOD_UPDATE_PLATFORM=linux-arm64 \
  -DOD_UPDATE_ANCHOR_CURRENT="$ANCHOR" > "$WORK/cmake-configure.log" 2>&1 \
  || { echo CONFIGURE_FAILED; tail -30 "$WORK/cmake-configure.log"; exit 2; }
grep -o "update manifest path: .*" "$WORK/cmake-configure.log" | sed 's/^/     | cmake: /'
cmake --build "$WORK/build" --target opendartboard-launcher -- -j2 --no-print-directory > "$WORK/cmake-build.log" 2>&1 \
  || { echo BUILD_FAILED; tail -30 "$WORK/cmake-build.log"; exit 2; }
cp "$WORK/build/opendartboard-launcher" "$BIN/opendartboard-launcher"
cp "$WORK/stubs/stub-$A" "$BIN/opendartboard" && chmod 0755 "$BIN/opendartboard"
echo "     | $(ls -l "$BIN/opendartboard-launcher" | awk '{print $5}') bytes, linked against: $(ldd "$BIN/opendartboard-launcher" | awk '{print $1}' | tr '\n' ' ')"
SHIPPED_SHA=$(sha "$BIN/opendartboard")

# ---- the pure check, both ways --------------------------------------------------------------
echo
echo "---- the path a build asks for, pure, compiled with and without OD_UPDATE_PLATFORM ----"
g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" $DEPS -DOD_UPDATE_PLATFORM='"linux-arm64"' \
    /app/testers/i1796_path_check.cpp -o "$WORK/path-arm64" || { echo COMPILE_FAILED; exit 2; }
g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" $DEPS \
    /app/testers/i1796_path_check.cpp -o "$WORK/path-bare" || { echo COMPILE_FAILED; exit 2; }
"$WORK/path-arm64" < /dev/null; note $? "the pure check, OD_UPDATE_PLATFORM=linux-arm64"
"$WORK/path-bare" < /dev/null; note $? "the pure check, undefined (the Windows path)"

# ---- the state directory, with what a paired board holds in it ----------------------------
# The credential's base_url is the deployment the launcher resolves (ADR-0077 §5, step 3:
# no --turnaus, no OD_TURNAUS_URL), so the harness measures the credential path too.
export STATE_DIRECTORY="$STATE"
cat > "$STATE/credentials.json" <<JSON
{"token": "od_live_THISMUSTSURVIVE_1796", "base_url": "http://127.0.0.1:$PORT", "station": "Pi in the corner"}
JSON
printf '{"version":1,"channel":"stable"}\n' > "$STATE/channel.json"
printf '{"version": 1, "cameras": ["/dev/video0", "/dev/video2", "/dev/video4"]}\n' > "$STATE/cameras.json"
fingerprint() { ( cd "$STATE" && find . -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum ); }
BEFORE=$(fingerprint)
echo "the state directory holds:"
echo "$BEFORE" | sed 's/^/     | /'

# ---- a deployment, on a port of its own ------------------------------------------------------
( cd "$WWW" && python3 -m http.server "$PORT" --bind 127.0.0.1 > "$LOG" 2>&1 & echo $! > "$WORK/www.pid" )
sleep 1
stop_serving() { [ -f "$WORK/www.pid" ] && kill "$(cat "$WORK/www.pid")" 2> /dev/null; }
trap stop_serving EXIT
serve() { cp "$FIX/$MANIFEST-$1.json" "$WWW/updates/opendartboard/$MANIFEST.json"; }
asked() { grep -c "GET /updates/opendartboard/$MANIFEST.json" "$LOG"; }
fetched() { grep -c "GET /rel/opendartboard-$1-linux-arm64.tar.gz" "$LOG"; }
age_state() { sed -i 's/^last_stopped=.*/last_stopped=1000000/' "$STATE/update/state.txt"; }

# The artefact's host, for a transport with no TLS: apply_update.hpp says why this is a
# harness pin and not a hole. The manifest is fetched from the credential's base_url.
export OD_UPDATE_ARTEFACT_BASE="http://127.0.0.1:$PORT"
export OD_STUB_BAD="$BAD"
ARGS=(--autocams --width 1280 --height 720 --fps 30 --model /usr/local/share/opendartboard/models/dart.param --log-file "$WORK/logs/opendartboard-20261011T060000Z.log")

# One carry: the launcher as the unit starts it, with a 60 s ceiling that nothing below
# should come near. `ran.<label>` is the sequence of versions that really ran.
carry() { # <label> <stdin>
  local label="$1" input="$2"
  rm -f "$WORK/ran.$label"
  OD_STUB_RAN_TO="$WORK/ran.$label" OD_STUB_ARGV_TO="$WORK/argv/$label" \
    timeout 60 "$BIN/opendartboard-launcher" "${ARGS[@]}" < "$input" > "$WORK/$label.out" 2>&1
  RC=$?
  echo "     | rc=$RC; ran: $(tr '\n' ' ' < "$WORK/ran.$label" 2>/dev/null)"
  sed 's/^/     | /' "$WORK/$label.out"
}

# ---- 0. the first boot: no state, the channel publishes the deb's own version ----------------
echo
echo "==== 0. first boot: no state, the manifest names $A, which the deb shipped ===="
serve "$A"
T0=$(date +%s)
carry firstboot /dev/null
[ "$RC" -eq 0 ]; note $? "the launcher exits 0 (the detector ended cleanly)"
[ "$(cat "$WORK/ran.firstboot" 2>/dev/null)" = "$A" ]; note $? "the deb's own detector ran ($A), from $BIN"
[ "$(asked)" -eq 1 ]; note $? "with no state the launcher MAY check: one manifest request, at /updates/opendartboard/$MANIFEST.json"
[ "$(fetched "$A")" -eq 0 ]; note $? "and fetched nothing: the channel names the version the board has"
[ ! -e "$STATE/opendartboard" ]; note $? "nothing was installed under the state directory"
[ -f "$STATE/update/state.txt" ] && [ -z "$(fact "$STATE" detector_version)" ]; note $? "the state file exists and names no installed version (the deb's is the launcher's own)"
[ "$(sha "$BIN/opendartboard")" = "$SHIPPED_SHA" ]; note $? "dpkg's /usr/local/bin/opendartboard is untouched"
head -1 "$WORK/argv/firstboot" | grep -q "^$BIN/opendartboard$"; note $? "argv[0] is the shipped detector"

# ---- 1. a release over the wire: the state names it, previous holds the deb's copy -----------
echo
echo "==== 1. the channel publishes $B: installed, started, the state names it ===="
age_state
serve "$B"
rm -f "$WORK/fifo" && mkfifo "$WORK/fifo"
sleep 90 > "$WORK/fifo" &
HOLDER=$!
BEGAN=$(date +%s)
carry fresh "$WORK/fifo"            # a service's stdin: an open pipe nobody writes
kill "$HOLDER" 2> /dev/null; wait "$HOLDER" 2> /dev/null
ELAPSED=$(( $(date +%s) - BEGAN ))
[ "$RC" -eq 0 ]; note $? "the launcher exits 0 in ${ELAPSED} s on an input nobody answers: it never waited at a console"
! grep -q "Enter" "$WORK/fresh.out"; note $? "  and it asked nothing"
[ "$(cat "$WORK/ran.fresh" 2>/dev/null)" = "$B" ]; note $? "$B ran, and only $B"
[ "$(fetched "$B")" -eq 1 ]; note $? "the tar.gz was fetched once, from the manifest's path on the harness's host"
[ "$(fact "$STATE" detector_version)" = "$B" ] && [ "$(fact "$STATE" previous_version)" = "$A" ]
note $? "update/state.txt: detector_version=$B previous_version=$A"
[ "$(sha "$STATE/opendartboard")" = "$(sha "$WORK/stubs/stub-$B")" ]; note $? "$STATE/opendartboard is release $B's detector, byte for byte"
[ "$(sha "$STATE/update/previous/opendartboard")" = "$SHIPPED_SHA" ]; note $? "update/previous/opendartboard is a COPY of the deb's detector"
[ "$(sha "$BIN/opendartboard")" = "$SHIPPED_SHA" ]; note $? "dpkg's /usr/local/bin/opendartboard is still there and untouched (a copy, never a move)"
[ ! -e "$STATE/update/staging" ] && [ ! -e "$STATE/update/download.archive" ]; note $? "staging/ and download.archive are gone"
grep -q "Version $B was installed. The previous version $A was kept" "$WORK/fresh.out" \
  && grep -q "Versio $B asennettiin. Edellinen versio $A s" "$WORK/fresh.out"
note $? "the launcher said so, in Finnish and English"
grep -qx "opendartboard $B" "$WORK/fresh.out" && grep -qx "opendartboard-launcher $A" "$WORK/fresh.out"
note $? "and named both versions at the end"
head -1 "$WORK/argv/fresh" | grep -q "^$STATE/opendartboard$"; note $? "argv[0] is the installed detector"
printf '%s\n' "${ARGS[@]}" | diff -q - <(tail -n +2 "$WORK/argv/fresh") > /dev/null
note $? "every argument arrived in order, --log-file and its file included"

# ---- 2. a release whose detector exits at once is rolled back ------------------------------
echo
echo "==== 2. the channel publishes $BAD, which exits 3 at once: rolled back to $B ===="
age_state
serve "$BAD"
BEGAN=$(date +%s)
carry rollback /dev/null
ELAPSED=$(( $(date +%s) - BEGAN ))
[ "$RC" -eq 0 ]; note $? "the launcher exits 0: the version it went back to ended cleanly"
[ "$(tr '\n' ' ' < "$WORK/ran.rollback" 2>/dev/null)" = "$BAD $BAD $B " ]
note $? "$BAD was started twice (kAllowedFailedStarts), then $B: three starts in one carry"
[ "$ELAPSED" -lt 60 ]; note $? "inside the unit's StartLimitIntervalSec (${ELAPSED} s), and one unit start, not three"
grep -q "Version $BAD did not start (0 s, exit code 3)" "$WORK/rollback.out" && grep -q "Trying once more" "$WORK/rollback.out"
note $? "it said $BAD did not start, and tried once more"
grep -q "Going back to version $B, which worked" "$WORK/rollback.out" && grep -q "Palataan versioon $B, joka toimi" "$WORK/rollback.out"
note $? "the rollback sentence, in both languages"
[ "$(fact "$STATE" detector_version)" = "$B" ] && [ -z "$(fact "$STATE" previous_version)" ]
note $? "update/state.txt: detector_version=$B, nothing kept (the failed version is not)"
[ "$(sha "$STATE/opendartboard")" = "$(sha "$WORK/stubs/stub-$B")" ]; note $? "$STATE/opendartboard is $B again"
[ ! -e "$STATE/update/previous" ] && [ ! -e "$STATE/update/failed-opendartboard" ]; note $? "previous/ and failed-opendartboard are gone"
[ "$(fact "$STATE" failed_starts)" = "0" ]; note $? "failed_starts=0 against the version that runs"

# ---- 3. a scheduled stop is followed by one look and one restart, same arguments -------------
echo
echo "==== 3. the channel publishes $C, which stops on schedule (exit 60) once ===="
age_state
serve "$C"
BEFORE_ASKED=$(asked)
export OD_STUB_EXITS=60,0 OD_STUB_RUNS="$WORK/runs.scheduled"
rm -rf "$WORK/argv/scheduled" && mkdir -p "$WORK/argv/scheduled"   # #1797's stub writes argv.<n> into a directory
carry scheduled /dev/null
unset OD_STUB_EXITS OD_STUB_RUNS
[ "$RC" -eq 0 ]; note $? "the launcher exits 0: the second start ended cleanly"
[ "$(grep -c . "$WORK/runs.scheduled" 2>/dev/null)" = 2 ]; note $? "$C was started twice by one launcher run"
cmp -s "$WORK/argv/scheduled/argv.1" "$WORK/argv/scheduled/argv.2"; note $? "with identical argv, byte for byte"
[ $(( $(asked) - BEFORE_ASKED )) -eq 2 ]; note $? "two looks: the one that installed $C, and the one after the scheduled stop"
grep -q "stopped on schedule at 06:00 (exit code 60)" "$WORK/scheduled.out" && grep -q "pysähtyi ajastetusti" "$WORK/scheduled.out"
note $? "the scheduled sentence, in both languages, between the two starts"
[ "$(fact "$STATE" detector_version)" = "$C" ] && [ "$(fact "$STATE" previous_version)" = "$B" ] && [ "$(fact "$STATE" last_ending)" = "cleanly" ]
note $? "update/state.txt: detector_version=$C previous_version=$B last_ending=cleanly"

# ---- and the sixth criterion, measured over everything above ----------------------------------
echo
echo "---- the state directory's own files, after three installs and a rollback ----"
AFTER=$(fingerprint)
if [ "$BEFORE" = "$AFTER" ]; then
  echo "ok   credentials.json, channel.json and cameras.json are byte-identical: an update never touched them"
else
  echo "FAIL a file beside the launcher's own changed"; diff <(echo "$BEFORE") <(echo "$AFTER"); FAILURES=$((FAILURES + 1))
fi
grep -q THISMUSTSURVIVE "$STATE/credentials.json"; note $? "and the credential the board was paired with is still the one it holds"

stop_serving
echo
echo "the server was asked for:"
grep -oE '"GET [^"]+"' "$LOG" | sort | uniq -c | sed 's/^/     | /'
echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

#!/bin/bash
# #1305: what testers/i1305_run.sh runs inside the container. See that file.
set -u
VERSION=1.4.1-i1305
export OD_FIXTURE_MATCHING_VERSION="$VERSION"
python3 /app/testers/i1305_fixtures.py || exit 2
ANCHOR=$(tr -d '\n\r' < /app/testers/fixtures1305/anchor.hex)

echo "== building a binary with the fixture's key compiled in =="
# In /out so a second run of this harness does not rebuild the whole program; delete
# /home/mikko/opendartboard/runs1305/anchored to force one.
rm -rf /out/anchored/CMakeCache.txt
mkdir -p /out/anchored
[ -d /out/anchored/_deps ] || cp -r /app/build/_deps /out/anchored/_deps
cmake -S /app -B /out/anchored -DCMAKE_PREFIX_PATH=/usr/local -DAPP_VERSION="$VERSION" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DCMAKE_CXX_FLAGS="-DOD_UPDATE_ANCHOR_CURRENT=\\\"$ANCHOR\\\"" > /tmp/anchored-cfg.log 2>&1 \
  || { tail -20 /tmp/anchored-cfg.log; exit 2; }
cmake --build /out/anchored -- -j4 --no-print-directory > /tmp/anchored-build.log 2>&1 \
  || { tail -30 /tmp/anchored-build.log; exit 2; }
BIN=/out/anchored/opendartboard
echo "built $($BIN --version)"

CFG=/tmp/i1305-cfg
rm -rf "$CFG" && mkdir -p "$CFG"
CRED="$CFG/credentials.json"

fingerprint() {
  # Everything about the config directory that a run could change.
  ( cd "$CFG" && find . -type f -printf '%p %s ' -exec sha256sum {} \; | sort ) 2>/dev/null
  echo "---files: $(find "$CFG" -type f | wc -l)"
}

serve() {
  rm -rf /tmp/www && mkdir -p /tmp/www/updates/opendartboard
  [ -n "${1:-}" ] && cp "$1" "/tmp/www/updates/opendartboard/${2:-stable}.json"
  ( cd /tmp/www && python3 -m http.server 8305 --bind 127.0.0.1 > /tmp/www.log 2>&1 & echo $! > /tmp/www.pid )
  sleep 1
}
stop_serving() {
  if [ -f /tmp/www.pid ]; then kill "$(cat /tmp/www.pid)" 2>/dev/null; rm -f /tmp/www.pid; sleep 0.3; fi
}

run_case() {
  local title="$1"; shift
  echo
  echo "================================================================"
  echo "== $title"
  echo "================================================================"
  local before after
  before=$(fingerprint)
  "$BIN" --check-update --credentials "$CRED" --quiet "$@" 2>&1 | grep -v '^\[' 
  echo "exit=${PIPESTATUS[0]}"
  after=$(fingerprint)
  if [ "$before" = "$after" ]; then
    echo "FILESYSTEM: the config directory is unchanged across this run"
  else
    echo "FILESYSTEM: CHANGED -- this run wrote something"
    diff <(echo "$before") <(echo "$after")
  fi
}

stop_serving
run_case "1. nothing is listening" --turnaus http://127.0.0.1:8305

serve ""
run_case "2. a deployment that publishes no manifest" --turnaus http://127.0.0.1:8305
stop_serving

serve /app/testers/fixtures1305/matching.json
run_case "3. the channel publishes the version this board is running" --turnaus http://127.0.0.1:8305
stop_serving

serve /app/testers/fixtures1305/good.json
run_case "4. the channel publishes a different version" --turnaus http://127.0.0.1:8305
stop_serving

serve /app/testers/fixtures1305/tampered.json
run_case "5. one byte of that manifest is changed" --turnaus http://127.0.0.1:8305
stop_serving

serve /app/testers/fixtures1305/beta.json
run_case "6. a beta manifest served at the stable address" --turnaus http://127.0.0.1:8305
stop_serving

echo
echo "================================================================"
echo "== 7. the channel is set, which is the one thing that writes"
echo "================================================================"
fingerprint
"$BIN" --channel beta --credentials "$CRED" --quiet --check-update --turnaus http://127.0.0.1:8305 2>&1 | grep -v '^\['
echo "exit=${PIPESTATUS[0]}"
echo "-- channel.json now holds:"
cat "$CFG/channel.json"
echo "-- and a later run with no --channel reads it back (asks the beta address):"
serve ""
"$BIN" --check-update --credentials "$CRED" --quiet --turnaus http://127.0.0.1:8305 2>&1 | grep -v '^\['
stop_serving
echo "-- a third word is refused:"
"$BIN" --channel nightly --credentials "$CRED" --quiet 2>&1 | grep -v '^\['
echo "exit=${PIPESTATUS[0]}"
echo "-- and the file still says:"
cat "$CFG/channel.json"

echo
echo "== 8. the shipped build, which has no anchor compiled in =="
serve /app/testers/fixtures1305/good.json beta
/app/build/opendartboard --check-update --credentials "$CRED" --quiet --turnaus http://127.0.0.1:8305 2>&1 | grep -v '^\['
stop_serving
exit 0

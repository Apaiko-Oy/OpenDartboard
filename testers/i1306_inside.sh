#!/bin/bash
# #1306: what runs inside the container for testers/i1306_check.sh. Read that file first.
set -u

SRC=/app/src
WORK=/tmp/i1306
FIX="$WORK/fixtures"
WWW="$WORK/www"
LOG="$WORK/access.log"
PORT=8306
# The four releases this board lives through. The names are versions because the stub's
# behaviour is decided by its own version and not by an environment variable -- one carry
# starts two of them, so it has to be.
A=v1.0.0
B=v1.1.0
C=v1.2.0
BAD=v1.3.0

rm -rf "$WORK" && mkdir -p "$FIX" "$WWW/updates/opendartboard" "$WWW/rel" "$WORK/stubs" "$WORK/install"

if [ "${MODE:-}" = "--mutate" ]; then
  rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
  python3 - <<'PY' || { echo MUTATION_FAILED; exit 2; }
path = "/tmp/mutated/launcher/apply_update.hpp"
text = open(path, encoding="utf-8").read()
gone = """        if (digest != answer.published_sha256)
        {
            Application refused = stoppedAt(Step::DigestMismatch, digest);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
"""
assert gone in text, "the digest check is not where the mutation expects it"
open(path, "w", encoding="utf-8").write(text.replace(gone, ""))
print("MUTATED: the artefact's digest is no longer compared against the signed manifest")
PY
  SRC=/tmp/mutated
fi

# ---- the four stubs. Each carries its own version the way the real artefact does. -------
for V in "$A" "$B" "$C" "$BAD"; do
  g++ -O1 -std=c++17 -Wall -Wextra -DAPP_VERSION="\"$V\"" \
      /app/testers/i1306_stub.cpp -o "$WORK/stubs/stub-$V" || { echo COMPILE_FAILED; exit 2; }
done

# ---- real zips, real digests, real signatures -------------------------------------------
python3 /app/testers/i1306_fixtures.py "$FIX" "$WORK/stubs" "$A" "$B" "$C" "$BAD" || { echo FIXTURES_FAILED; exit 2; }
cp "$FIX"/rel/*.zip "$WWW/rel/"

# ---- the config directory, with something in it worth not clobbering --------------------
# #708's rule in the shape this slice needs it: the needle is planted and PROVED present
# before its absence means anything. These three files are what a paired board holds, and
# the sixth criterion is that an update leaves every byte of them alone.
export XDG_CONFIG_HOME="$WORK/config"
CFG="$XDG_CONFIG_HOME/opendartboard"
mkdir -p "$CFG"
cat > "$CFG/credentials.json" <<'JSON'
{"token": "od_live_THISMUSTSURVIVE_9f3c", "base_url": "https://turnaus.example", "station": "Pub room"}
JSON
cat > "$CFG/cameras.json" <<'JSON'
{"version": 1, "cameras": ["/dev/video0", "/dev/video1", "/dev/video2"]}
JSON
printf '{"version":1,"channel":"stable"}\n' > "$CFG/channel.json"
fingerprint() { ( cd "$CFG" && find . -type f -print0 | sort -z | xargs -0 sha256sum ); }
BEFORE=$(fingerprint)
echo "the config directory holds:"
echo "$BEFORE" | sed 's/^/     | /'
grep -q 'THISMUSTSURVIVE' "$CFG/credentials.json" || { echo "the needle was never planted"; exit 2; }

# ---- a deployment, on a port of its own --------------------------------------------------
( cd "$WWW" && python3 -m http.server "$PORT" --bind 127.0.0.1 > "$LOG" 2>&1 & echo $! > "$WORK/www.pid" )
sleep 1
stop_serving() { [ -f "$WORK/www.pid" ] && kill "$(cat "$WORK/www.pid")" 2> /dev/null; }
trap stop_serving EXIT

# ---- the board starts life on the first release ------------------------------------------
cp "$WORK/stubs/stub-$A" "$WORK/install/opendartboard"
chmod +x "$WORK/install/opendartboard"

# APP_VERSION is the LAUNCHER's version here, and it is $A because a board that has never
# updated answers with its launcher's version (install_layout.hpp says why that is true by
# construction of the release zip).
g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" \
    -I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src \
    -DAPP_VERSION="\"$A\"" \
    /app/testers/i1306_update_check.cpp -o "$WORK/check" || { echo COMPILE_FAILED; exit 2; }

"$WORK/check" "$FIX" "$WORK" "$WWW" "$LOG" "$PORT" "$A" "$B" "$C" "$BAD" < /dev/null
RC=$?

# ---- and the sixth criterion, measured over everything above -----------------------------
echo
echo "---- the config directory, after eleven downloads, four swaps and a rollback ----"
AFTER=$(fingerprint)
if [ "$BEFORE" = "$AFTER" ]; then
  echo "ok   credentials.json, cameras.json and channel.json are byte-identical: an update never touched them"
else
  echo "FAIL the config directory changed"
  diff <(echo "$BEFORE") <(echo "$AFTER")
  RC=1
fi
grep -q 'THISMUSTSURVIVE' "$CFG/credentials.json" \
  && echo "ok   and the credential the board was paired with is still the one it holds" \
  || { echo "FAIL the credential is gone"; RC=1; }

stop_serving
echo
echo "the server was asked for:"
grep -oE '"GET [^"]+"' "$LOG" | sort | uniq -c | sed 's/^/     | /'
exit $RC

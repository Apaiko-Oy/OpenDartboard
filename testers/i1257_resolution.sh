#!/bin/bash
# #1257: the address resolution order, run inside the container by i1257_run.sh.
#
# Three stubs on the container's loopback (the run has no network). Each phase runs --pair,
# which resolves the address and posts one pairing request to it without opening a camera,
# and the evidence is two-sided: the detector's own "TURNAUS: address" line, and which
# stub's transcript received the request. The token is never printed; of the credential
# file only its base_url is shown.
set -u
B=/app/build/opendartboard
CRED=/run1257/cfg/stored.json
strip() { sed -E 's/\x1b\[[0-9;]*m//g'; }

for p in 8897 8898 8899; do
  STUB_PORT=$p STUB_TRANSCRIPT=/run1257/stub$p.jsonl python3 /app/testers/turnaus_stub.py 2> /run1257/stub$p.err &
done
sleep 2
cat /run1257/stub*.err

counts() { for p in 8897 8898 8899; do printf 'stub%s=%s ' $p "$(wc -l < /run1257/stub$p.jsonl)"; done; echo; }

phase() { # phase <name> <command...>
  local name="$1"; shift
  echo "=== $name ==="
  echo "before: $(counts)"
  "$@" > /run1257/$name.out 2> /run1257/$name.err
  echo "rc=$?"
  strip < /run1257/$name.out | grep -a 'TURNAUS:'
  strip < /run1257/$name.err | grep -a 'TURNAUS:'
  echo "after:  $(counts)"
}

# The default: no flag, no environment variable, no credential file.
phase default env -u OD_TURNAUS_URL $B --pair 000000 --credentials /run1257/cfg/absent.json

# Establish a stored base_url: pair against stub 8899, named by the flag.
phase setup env -u OD_TURNAUS_URL $B --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext --credentials $CRED
echo "stored: $(grep -o '"base_url"[^,}]*' $CRED)"

# The stored base_url wins over the default.
phase stored env -u OD_TURNAUS_URL $B --pair 111111 --allow-plaintext --credentials $CRED

# OD_TURNAUS_URL wins over the stored base_url.
phase environment env OD_TURNAUS_URL=http://127.0.0.1:8898 $B --pair 111111 --allow-plaintext --credentials $CRED

# --turnaus wins over OD_TURNAUS_URL and the stored base_url.
phase flag env OD_TURNAUS_URL=http://127.0.0.1:8898 $B --pair 111111 --turnaus http://127.0.0.1:8897 --allow-plaintext --credentials $CRED

echo "stored after every phase: $(grep -o '"base_url"[^,}]*' $CRED)"
for p in 8897 8898 8899; do
  echo "--- stub$p transcript ---"
  python3 -c "
import json,sys
for line in open('/run1257/stub$p.jsonl'):
    e=json.loads(line); e.pop('at',None); print(json.dumps(e, sort_keys=True))"
done
kill %1 %2 %3 2>/dev/null
wait 2>/dev/null
exit 0

#!/bin/bash
# #1257: the parked domain is never an address in this tree, and the default is written once.
#
# The fallback address used to name a domain that is not Turnaus' - it is parked and for
# sale - so an unconfigured board would have sent a live pairing code to its owner and
# kept whatever credential came back. Production is turnaus.apaiko.fi.
#
# Two assertions, both over every file git sees in the working tree (tracked and
# untracked, ignored files such as build/ excluded):
#
#   1. the parked domain appears nowhere as a host. Matched as a host rather than a
#      substring: a subdomain of it counts, and so does a scheme or a path after it, but
#      turnaus.apaiko.fi - which does not contain it - and a longer label such as
#      myturnaus.fi or turnaus.fish do not.
#   2. under src/, the production address is written exactly once, in
#      src/communication/turnaus_address.hpp.
#
#   testers/check_default_address.sh        exit 0 when both hold, 1 otherwise
set -u
cd "$(dirname "$0")/.." || exit 2

fail=0

# Built from pieces so this file does not contain the literal it refuses.
PARKED_LABEL='turnaus'
PARKED_TLD='fi'
PARKED_HOST="(^|[^A-Za-z0-9-])${PARKED_LABEL}\\.${PARKED_TLD}(\$|[^A-Za-z0-9.-]|\\.[^A-Za-z0-9-]|\\.\$)"

hits=$(git grep --untracked -I -n -P "$PARKED_HOST" -- . ':!testers/check_default_address.sh')
if [ -n "$hits" ]; then
  echo "FAIL: the parked domain ${PARKED_LABEL}.${PARKED_TLD} appears as an address:"
  echo "$hits" | sed 's/^/  /'
  fail=1
else
  echo "ok: the parked domain appears nowhere as an address"
fi

PRODUCTION='turnaus\.apaiko\.fi'
prod_hits=$(git grep --untracked -I -n -P "$PRODUCTION" -- src)
prod_count=$(printf '%s' "$prod_hits" | grep -c . || true)
if [ "$prod_count" != "1" ] || ! printf '%s' "$prod_hits" | grep -q '^src/communication/turnaus_address.hpp:'; then
  echo "FAIL: the production address must be written once under src/, in turnaus_address.hpp; found $prod_count:"
  [ -n "$prod_hits" ] && echo "$prod_hits" | sed 's/^/  /'
  fail=1
else
  echo "ok: the production address is written once under src/ ($prod_hits)"
fi

if [ "$fail" = 0 ]; then
  echo "PASS"
else
  echo "FAILED"
fi
exit "$fail"

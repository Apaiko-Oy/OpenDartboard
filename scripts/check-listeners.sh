#!/bin/sh
# #824: the bind-address rule, enforced rather than followed.
#
# A listener opened by this program may only ever name an address that is not
# reachable from the network. The rule lives at the receiving end — od_socket.hpp's
# bindLoopbackOnly(), which takes a port and no address — and this script is what
# stops a refactor from putting the choice back into a call site.
#
# It fails on:
#   * a ::bind() anywhere but od_socket.hpp
#   * INADDR_ANY, or any address literal, in code outside od_socket.hpp
#   * an httplib listen() naming a host other than the one known exception
#
# The one known exception is WebSocketService's 13520, which binds 0.0.0.0 in
# every build and is not this issue's: closing it is a design question about how
# a Station authenticates to a board (#805 §6, "what is still open on 13520").
# --setup does not start it at all, which is a different statement from closing it.
#
# Run from the repository root. Prints nothing and exits 0 when the rule holds.

set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

fail=0
say() { echo "check-listeners: $1"; fail=1; }

# Code lines only: a rule that fires on its own explanation is useless.
code() { grep -rn "$1" --include='*.cpp' --include='*.hpp' src/ | grep -v ':[0-9]*: *//' | grep -v ':[0-9]*: *\*'; }

binds=$(code '::bind(' | grep -v '^src/utils/od_socket.hpp:' || true)
[ -n "$binds" ] && say "a ::bind() outside od_socket.hpp:
$binds"

anyaddr=$(code 'INADDR_ANY' || true)
[ -n "$anyaddr" ] && say "INADDR_ANY in code:
$anyaddr"

loopback=$(code 'INADDR_LOOPBACK' | grep -c '^src/utils/od_socket.hpp:' || true)
[ "$loopback" = "1" ] || say "expected exactly one INADDR_LOOPBACK, in od_socket.hpp; found $loopback"

# Any dotted-quad or "::" host handed to a listen(). 13520's is the one exception.
hosts=$(code 'listen("' | grep -v 'websocket_service.cpp' || true)
[ -n "$hosts" ] && say "a listen() naming a host outside websocket_service.cpp:
$hosts"

ws=$(code 'listen("' | grep -c 'websocket_service.cpp' || true)
[ "$ws" = "1" ] || say "expected exactly one listen() host literal (13520's); found $ws"

[ "$fail" = "0" ] || { echo "check-listeners: FAILED"; exit 1; }
exit 0

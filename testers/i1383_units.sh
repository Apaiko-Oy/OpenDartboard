#!/bin/bash
# #1383's measurement of templates/opendartboard.service.template, on real systemd.
#
# unrun-tester: it is not in run_all.sh because it cannot run where run_all.sh runs, for
# testers/i1334_units.sh's reason and in its shape. Every other tester measures
# build/opendartboard inside a Debian container with no init; this one measures what a
# MANAGER does with a unit file and a datagram, so it needs a live systemd and a live
# journal. It uses the calling user's session manager -- `systemctl --user`, no root,
# every unit named after this checkout and removed on the way out -- and exits 2 where
# there is none, so a box without systemd is not turned red by a unit file that is fine.
#
#   testers/i1383_units.sh
#
# THE DIVISION OF LABOUR, because this file is half of a criterion and says which half.
# That the DETECTOR sends a status naming its fault, and a different one when it can see,
# is testers/i1383_run.sh phases 6 and 7, against the real binary. What only a manager can
# answer is the other half: that the datagram this repository's own code sends is kept and
# shown by systemd, from a unit of the shape this repository ships. So it compiles
# src/utils/od_notify.hpp -- alone, no OpenCV -- into testers/i1383_notify_probe.cpp and
# puts THAT in front of the manager. A stand-in calling systemd-notify would have measured
# systemd rather than us.
#
# What it measures:
#
#   verify     The rendered template through `systemd-analyze verify`. NotifyAccess= is
#              new here and a key the manager does not accept on a Type=simple unit would
#              be reported. The control beside it misspells the key, because a check that
#              cannot fail proves nothing.
#
#   status     The probe as the main process of a Type=simple unit with NotifyAccess=main,
#              exactly as the template runs the detector. `systemctl show -p StatusText`
#              must come back with the sentence the probe sent, byte for byte. The control
#              is the same probe under NotifyAccess=none, where systemd sets no
#              $NOTIFY_SOCKET at all: the status must be EMPTY, or the assertion above is
#              about a manager that would have said anything.
#
#   budget     The shipped template must never name OD_MAX_CYCLES. Since #1383 a cycle
#              budget ENDS a blind run, and that is a tester's instruction: a board at a
#              venue must go on waiting for its camera. This is the line that keeps the
#              two apart.
#
#   watchdog   And it must never name WatchdogSec=. A missed watchdog makes systemd KILL
#              the service, which is the blind board exiting by a side door -- the one
#              option the maintainer refused. Written as a check rather than as a comment
#              because a comment does not fail.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1383/units"
UNITS="$HOME/.config/systemd/user"
TAG="od-$OD_TREE_TAG-1383"
TEMPLATE="$OD_TREE_ROOT/templates/opendartboard.service.template"
FAILED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

if [ "$(ps -p 1 -o comm=)" != "systemd" ] || ! systemctl --user is-system-running > /dev/null 2>&1; then
  echo "i1383_units: this box has no systemd session manager, so what systemd does with" >&2
  echo "             this unit file and this datagram cannot be measured here. Run it on" >&2
  echo "             the Pi, or on any box where 'systemctl --user is-system-running'" >&2
  echo "             answers." >&2
  exit 2
fi
for t in envsubst systemd-analyze systemctl; do
  command -v "$t" > /dev/null || { echo "i1383_units: $t is not on this machine" >&2; exit 2; }
done
# The probe is one translation unit and no library, so a compiler is the only other thing
# needed -- and the box with the systemd is not always the box with the toolchain. This
# one has no g++ at all, which is the ordinary case for a WSL checkout whose every build
# happens in a container. So: the host's compiler where there is one, and $OD_IMAGE's
# otherwise. The artefact is the same either way -- a static-enough C++17 binary with no
# OpenCV -- and it is RUN on the host, under the host's manager, because the manager is
# what is being measured.
COMPILE_IN_CONTAINER=0
if ! command -v g++ > /dev/null; then
  if command -v docker > /dev/null && docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
    COMPILE_IN_CONTAINER=1
  else
    echo "i1383_units: no g++ on this box and no $OD_IMAGE to borrow one from" >&2
    exit 2
  fi
fi

cleanup() {
  systemctl --user stop "$TAG-status.service" "$TAG-deaf.service" > /dev/null 2>&1
  systemctl --user reset-failed "$TAG-status.service" "$TAG-deaf.service" > /dev/null 2>&1
  rm -f "$UNITS/$TAG-"*.service
  systemctl --user daemon-reload > /dev/null 2>&1
}
trap cleanup EXIT

rm -rf "$RUN"; mkdir -p "$RUN/units" "$UNITS"
env WIDTH=1280 HEIGHT=720 FPS=30 envsubst < "$TEMPLATE" > "$RUN/units/opendartboard.service"

# ---- verify ----------------------------------------------------------------------------
echo "--- verify: the rendered unit through systemd-analyze ---"
printf '#!/bin/sh\nexit 0\n' > "$RUN/opendartboard"; chmod +x "$RUN/opendartboard"
sed "s#/usr/local/bin/#$RUN/#" "$RUN/units/opendartboard.service" > "$RUN/verify.service"
if out=$(systemd-analyze verify "$RUN/verify.service" 2>&1) && [ -z "$out" ]; then
  ok "systemd-analyze verify is silent on the unit this repository ships"
else
  fail "systemd-analyze verify complained: $out"
fi
sed 's/^NotifyAccess=main$/NotifyAcess=main/' "$RUN/verify.service" > "$RUN/misspelt.service"
if systemd-analyze verify "$RUN/misspelt.service" 2>&1 | grep -q "Unknown key name 'NotifyAcess'"; then
  ok "control: one letter out of that key is reported, so the verify above can speak"
else
  fail "control: systemd did not object to a misspelt NotifyAccess; the verify above proves nothing"
fi

# ---- status ------------------------------------------------------------------------------
echo
echo "--- status: this repository's own od_notify.hpp, in front of the manager ---"
if [ "$COMPILE_IN_CONTAINER" = 1 ]; then
  echo "     | no g++ here; borrowing $OD_IMAGE's to compile the probe"
  docker run --rm --name "$(od_name i1383-probe)" --cpus=2 --network none \
    -v "$OD_TREE_ROOT":/app -v "$RUN":/out "$OD_IMAGE" \
    g++ -std=c++17 -O1 -I /app/src -o /out/probe /app/testers/i1383_notify_probe.cpp \
    2> "$RUN/probe.build"
  BUILT=$?
else
  g++ -std=c++17 -O1 -I "$OD_TREE_ROOT/src" -o "$RUN/probe" \
    "$OD_TREE_ROOT/testers/i1383_notify_probe.cpp" 2> "$RUN/probe.build"
  BUILT=$?
fi
if [ "$BUILT" != 0 ] || [ ! -x "$RUN/probe" ]; then
  fail "src/utils/od_notify.hpp does not compile on its own:"
  sed 's/^/     | /' "$RUN/probe.build"
  exit $FAILED
fi
ok "src/utils/od_notify.hpp compiles alone, with no OpenCV and no systemd library"

SENTENCE="ERROR: this board cannot see -- the cameras did not open (tried: /dev/video0)"
probe_unit() {   # <name> <NotifyAccess=...>
  cat > "$UNITS/$TAG-$1.service" <<EOF
[Unit]
Description=#1383 notify probe ($1)

[Service]
Type=simple
$2
ExecStart=$RUN/probe ERROR "this board cannot see -- the cameras did not open (tried: /dev/video0)" 20
EOF
  systemctl --user daemon-reload
  systemctl --user restart "$TAG-$1.service"
  local waited=0
  while [ "$waited" -lt 50 ]; do
    [ -n "$(systemctl --user show "$TAG-$1.service" -p StatusText --value)" ] && break
    sleep 0.2; waited=$((waited + 1))
  done
  systemctl --user show "$TAG-$1.service" -p StatusText --value
}

GOT="$(probe_unit status 'NotifyAccess=main')"
echo "     | StatusText=$GOT"
if [ "$GOT" = "$SENTENCE" ]; then
  ok "systemd kept what the detector's own code sent, byte for byte, from a Type=simple unit"
else
  fail "the manager did not keep it. expected: $SENTENCE"
fi
systemctl --user stop "$TAG-status.service" > /dev/null 2>&1

DEAF="$(probe_unit deaf 'NotifyAccess=none')"
if [ -z "$DEAF" ]; then
  ok "control: with NotifyAccess=none there is no socket and the status is empty"
else
  fail "control: a unit with NotifyAccess=none still showed a status ($DEAF), so the check above measures nothing"
fi
systemctl --user stop "$TAG-deaf.service" > /dev/null 2>&1

# ---- what the template must never say ----------------------------------------------------
echo
echo "--- the two things this unit must not name ---"
# The DIRECTIVES, not the prose. #1430's rule, met again the first time this ran: the
# template carries a comment explaining that a cycle budget must never appear in it, and
# a whole-file grep read that explanation as the thing it forbids. A comment is prose.
DIRECTIVES="$RUN/directives"
grep -v '^[[:space:]]*#' "$TEMPLATE" > "$DIRECTIVES"
if grep -q "OD_MAX_CYCLES" "$DIRECTIVES"; then
  fail "the shipped unit names OD_MAX_CYCLES. A cycle budget ends a blind run since #1383;"
  fail "a board at a venue must go on waiting for a camera that may come back (#895)."
else
  ok "the shipped unit names no cycle budget, so #1383's ending cannot reach a venue"
fi
if grep -qE "^[[:space:]]*WatchdogSec=" "$DIRECTIVES"; then
  fail "the shipped unit names WatchdogSec=. A missed ping makes systemd kill the service,"
  fail "which is the blind board exiting by a side door -- the option that was refused."
else
  ok "and no WatchdogSec=, so nothing here turns a blind board into a restart loop"
fi

echo
echo "load_at_end=$(cut -d' ' -f1-3 /proc/loadavg)"
exit $FAILED

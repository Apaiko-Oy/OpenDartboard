#!/bin/bash
# #1334's measurement of templates/*.service.template, on real systemd.
#
# unrun-tester: it is not in run_all.sh because it cannot run where run_all.sh runs. Every
# other tester in this directory measures build/opendartboard inside $OD_IMAGE, a Debian
# container with no init; this one measures what *systemd* does with two unit files, so it
# needs a live manager and a live journal. It uses the calling user's session manager --
# `systemctl --user`, no root, nothing installed system-wide, every unit named after this
# checkout and removed on the way out -- and it says so and exits 2 where there is none.
# Putting it in the sweep would turn every non-systemd box red on a unit file that is fine.
#
#   testers/i1334_units.sh            measure everything below
#
# What it measures, and why each half exists:
#
#   verify       Both rendered units through `systemd-analyze verify`, with stand-in
#                binaries in place of the two the .deb installs, so the only thing left to
#                report would be a real fault. The control beside it puts
#                StartLimitIntervalSec in [Service] -- where it reads perfectly and is
#                *ignored* -- because a check that cannot fail proves nothing.
#
#   crash-loop   "A detector that exits repeatedly stops respawning and says so in the
#                journal, rather than looping silently at ten starts a second." The unit's
#                own [Service] and [Unit] stanzas, verbatim from the template, with
#                ExecStart replaced by a stand-in that stamps the clock and exits 1. The
#                number measured is the interval between consecutive starts, taken from the
#                stand-in's own stamps rather than from the journal's one-second
#                timestamps.
#
#                Two controls, because the first one corrects the issue's own wording. The
#                pre-#1334 stanza does NOT loop forever on a stock manager: the manager's
#                DefaultStartLimitBurst=5 stops it too, after about a second. What #1334
#                buys over that is the rate -- five-plus starts a second, each re-running
#                calibration, against one every two -- and the fact that the limit is now
#                the unit's own. The second control is why that second half matters: with
#                StartLimitIntervalSec=0, which is what an image setting
#                DefaultStartLimitIntervalSec=0 in system.conf gives every unit that does
#                not say otherwise, the pre-#1334 stanza really does loop silently and
#                forever, and it is measured here doing it.
#
#   networkless  "A board with no network still starts." Wants= is weak, but that is a
#                claim about systemd rather than about this unit, and After= on a target
#                whose wait-online provider hangs or fails delays the start either way. So:
#                a target of our own, a provider that fails after a delay, and a probe unit
#                carrying the template's exact Wants=/After= pair. It must still run. Two
#                sub-cases, because a Pi meets both: a provider that fails (NetworkManager
#                -wait-online on a board with no carrier) and no provider at all (a board
#                whose image never installed one, where the target is a no-op).
#
# The detector half of that criterion -- still opens its cameras, still spools -- is
# i1334_run.sh, which is in run_all.sh because it is an ordinary container tester.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1334/units"
UNITS="$HOME/.config/systemd/user"
TAG="od-$OD_TREE_TAG-1334"
FAILED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

if [ "$(ps -p 1 -o comm=)" != "systemd" ] || ! systemctl --user is-system-running > /dev/null 2>&1; then
  echo "i1334_units: this box has no systemd session manager, so what systemd does with" >&2
  echo "             these unit files cannot be measured here. Run it on the Pi, or on any" >&2
  echo "             box where 'systemctl --user is-system-running' answers." >&2
  exit 2
fi
for t in envsubst systemd-analyze systemd-run journalctl; do
  command -v "$t" > /dev/null || { echo "i1334_units: $t is not on this machine" >&2; exit 2; }
done

cleanup() {
  systemctl --user stop "$TAG-crash.service" "$TAG-control.service" "$TAG-nolimit.service" \
    "$TAG-probe.service" "$TAG-wait-online.service" "$TAG-network-online.target" > /dev/null 2>&1
  systemctl --user reset-failed "$TAG-crash.service" "$TAG-control.service" \
    "$TAG-nolimit.service" "$TAG-probe.service" "$TAG-wait-online.service" > /dev/null 2>&1
  rm -f "$UNITS/$TAG-"*.service "$UNITS/$TAG-"*.target
  systemctl --user daemon-reload > /dev/null 2>&1
}
trap cleanup EXIT

rm -rf "$RUN"; mkdir -p "$RUN/bin" "$RUN/units" "$UNITS"

# ---- the units this repository actually ships, rendered -------------------------------
# WIDTH/HEIGHT/FPS are the .deb's; nothing below depends on their values.
for f in opendartboard lock_cams; do
  env WIDTH=1280 HEIGHT=720 FPS=30 envsubst \
    < "$OD_TREE_ROOT/templates/$f.service.template" > "$RUN/units/$f.service"
done

# ---- verify ---------------------------------------------------------------------------
echo "--- verify: both rendered units through systemd-analyze ---"
printf '#!/bin/sh\nexit 0\n' > "$RUN/bin/opendartboard"
cp "$RUN/bin/opendartboard" "$RUN/bin/lock_cams.sh"
chmod +x "$RUN/bin/opendartboard" "$RUN/bin/lock_cams.sh"
mkdir -p "$RUN/verify"
for f in opendartboard lock_cams; do
  sed "s#/usr/local/bin/#$RUN/bin/#" "$RUN/units/$f.service" > "$RUN/verify/$f.service"
done
if out=$(systemd-analyze verify "$RUN/verify/opendartboard.service" "$RUN/verify/lock_cams.service" 2>&1) \
   && [ -z "$out" ]; then
  ok "systemd-analyze verify: silent on both units"
else
  fail "systemd-analyze verify complained: $out"
fi

# The control. Both keys belong in [Unit]; in [Service] they parse as copy and do nothing,
# which is the mistake this file is one edit away from at all times.
sed 's/^StartLimitIntervalSec=60$//; s/^StartLimitBurst=5$//; s/^RestartSec=2$/RestartSec=2\nStartLimitIntervalSec=60\nStartLimitBurst=5/' \
  "$RUN/verify/opendartboard.service" > "$RUN/verify/misplaced.service"
if systemd-analyze verify "$RUN/verify/misplaced.service" 2>&1 | grep -q "Unknown key name 'StartLimitIntervalSec' in section 'Service'"; then
  ok "control: the same keys in [Service] are reported ignored, so the check above can speak"
else
  fail "control: systemd did not object to StartLimitIntervalSec in [Service]; the verify above proves nothing"
fi

# ---- crash-loop -------------------------------------------------------------------------
# The stand-in stamps the clock and exits non-zero. Nice=-5 is dropped: a session manager
# may not lower a nice value, and this measurement is about Restart*, not about priority.
echo
echo "--- crash-loop: a detector that exits every time it is started ---"
cat > "$RUN/bin/exits.sh" <<EOF
#!/bin/sh
date +%s.%N >> "\$1"
exit 1
EOF
chmod +x "$RUN/bin/exits.sh"

build_crash_unit() {   # <unit-name> <stamp-file> <extra-[Unit]-lines...>
  local name="$1" stamps="$2"; shift 2
  {
    echo "[Unit]"
    echo "Description=#1334 crash-loop probe ($name)"
    printf '%s\n' "$@"
    echo
    echo "[Service]"
    echo "Type=simple"
    echo "ExecStart=$RUN/bin/exits.sh $stamps"
    echo "Restart=always"
    [ -n "${RESTARTSEC:-}" ] && echo "RestartSec=$RESTARTSEC"
    echo "StandardOutput=journal"
    echo "StandardError=journal"
  } > "$UNITS/$name.service"
}

# The stanza this branch ships, lifted out of the rendered unit rather than retyped, so a
# later edit to the template is what this measures.
tmpl_unit_limits=$(grep -E '^StartLimit' "$RUN/units/opendartboard.service")
tmpl_restartsec=$(grep -E '^RestartSec=' "$RUN/units/opendartboard.service" | cut -d= -f2)
echo "from the template: RestartSec=$tmpl_restartsec, $(echo "$tmpl_unit_limits" | tr '\n' ' ')"

tmpl_burst=$(echo "$tmpl_unit_limits" | sed -n 's/^StartLimitBurst=//p')

# shellcheck disable=SC2086
RESTARTSEC="$tmpl_restartsec" build_crash_unit "$TAG-crash" "$RUN/after.stamps" $tmpl_unit_limits
# Pre-#1334: no RestartSec, no limit of its own, so the manager's default decides.
RESTARTSEC="" build_crash_unit "$TAG-control" "$RUN/before.stamps"
# Pre-#1334 on an image that has turned the manager's default off. Nothing then stops it.
RESTARTSEC="" build_crash_unit "$TAG-nolimit" "$RUN/nolimit.stamps" "StartLimitIntervalSec=0"
systemctl --user daemon-reload

measure_loop() {   # <unit-name> <stamp-file> <seconds-to-watch> -> prints starts/span/rate
  local name="$1" stamps="$2" watch="$3" i
  : > "$stamps"
  systemctl --user reset-failed "$name.service" > /dev/null 2>&1
  local t0; t0=$(date +%s.%N)
  systemctl --user start "$name.service" > /dev/null 2>&1
  for ((i = 0; i < watch * 2; i++)); do
    sleep 0.5
    case "$(systemctl --user is-active "$name.service" 2>&1)" in failed|inactive) break ;; esac
  done
  systemctl --user stop "$name.service" > /dev/null 2>&1
  python3 - "$stamps" "$t0" <<'PY'
import sys
stamps = [float(l) for l in open(sys.argv[1]) if l.strip()]
t0 = float(sys.argv[2])
span = (stamps[-1] - stamps[0]) if len(stamps) > 1 else 0.0
gaps = [round(b - a, 2) for a, b in zip(stamps, stamps[1:])]
print("starts=%d span_s=%.2f rate_per_s=%s gaps=%s"
      % (len(stamps), span, ("%.1f" % (len(stamps) / span)) if span > 0 else "n/a", gaps))
PY
}

field() { echo "$1" | sed -n "s/.*$2=\([0-9.]*\).*/\1/p"; }

echo "control A -- the stanza before this branch, on a stock manager"
before=$(measure_loop "$TAG-control" "$RUN/before.stamps" 20)
echo "  $before  ActiveState=$(systemctl --user show -p ActiveState --value "$TAG-control.service")"

echo "control B -- the same stanza where the image has set DefaultStartLimitIntervalSec=0"
nolimit=$(measure_loop "$TAG-nolimit" "$RUN/nolimit.stamps" 5)
echo "  $nolimit  ActiveState=$(systemctl --user show -p ActiveState --value "$TAG-nolimit.service")"

echo "this branch"
after=$(measure_loop "$TAG-crash" "$RUN/after.stamps" 30)
echo "  $after"
after_state=$(systemctl --user show -p ActiveState --value "$TAG-crash.service")
echo "  ActiveState=$after_state NRestarts=$(systemctl --user show -p NRestarts --value "$TAG-crash.service")"

after_rate=$(field "$after" rate_per_s)
after_starts=$(field "$after" starts)
before_rate=$(field "$before" rate_per_s)
nolimit_rate=$(field "$nolimit" rate_per_s)
nolimit_starts=$(field "$nolimit" starts)

# It stopped, and it stopped exactly where the unit says it should. ActiveState rather than
# Result: on systemd 255 a unit rate-limited out of an auto-restart keeps the Result of the
# last real failure -- Result=exit-code -- and start-limit-hit is what a *manual* start
# reports. Measured, not assumed.
if [ "$after_state" = failed ] && [ "$after_starts" = "$tmpl_burst" ]; then
  ok "it stops: $after_starts starts, which is the unit's own StartLimitBurst, then ActiveState=failed"
else
  fail "it did not stop where the unit says (starts=$after_starts, burst=$tmpl_burst, ActiveState=$after_state)"
fi

if journalctl --user -u "$TAG-crash.service" --since "-5min" --no-pager 2>/dev/null \
     | grep -qi "start request repeated too quickly"; then
  ok "it says so: the journal carries \"Start request repeated too quickly\""
else
  fail "the journal does not say why it stopped"
  journalctl --user -u "$TAG-crash.service" --since "-5min" --no-pager 2>/dev/null | tail -5
fi

python3 - "$after_rate" "$before_rate" "$nolimit_rate" "$nolimit_starts" <<'PY' || FAILED=1
import sys
after, before, nolimit, nolimit_starts = sys.argv[1:5]
if not after:
    print("FAIL the branch's restart rate could not be measured"); sys.exit(1)
print("     %-64s %s starts/s" % ("this branch", after))
print("     %-64s %s starts/s" % ("control A (stock manager, no RestartSec)", before or "n/a"))
print("     %-64s %s starts/s over %s starts and still going"
      % ("control B (manager default turned off, no limit in the unit)", nolimit or "n/a", nolimit_starts))
rc = 0
if float(after) > 1.0:
    print("FAIL RestartSec did not slow the loop: %s starts a second" % after); rc = 1
if not nolimit or float(nolimit) < 2.0:
    print("FAIL control B did not reproduce the fast loop, so the number above it means nothing"); rc = 1
sys.exit(rc)
PY

# ---- networkless -------------------------------------------------------------------------
echo
echo "--- networkless: the template's Wants=/After= pair, with no connectivity ---"
cat > "$UNITS/$TAG-network-online.target" <<EOF
[Unit]
Description=#1334 stand-in for network-online.target
EOF
cat > "$UNITS/$TAG-wait-online.service" <<EOF
[Unit]
Description=#1334 stand-in for a wait-online provider that never sees a carrier
Before=$TAG-network-online.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'sleep 3; exit 1'
[Install]
WantedBy=$TAG-network-online.target
EOF

probe_unit() {   # <wants-the-provider: yes|no>
  local provider="$1"
  cat > "$UNITS/$TAG-probe.service" <<EOF
[Unit]
Description=#1334 probe carrying the template's network clause
Wants=$TAG-network-online.target
After=$TAG-network-online.target
[Service]
Type=oneshot
# %% because % is a systemd specifier: %s is the shell and %N the unit name, so an
# unescaped 'date +%s.%N' writes "/bin/bash.<unit>" and the stamp is not a clock at all.
ExecStart=/bin/sh -c 'date +%%s.%%N > $RUN/probe.ran'
EOF
  rm -f "$UNITS/$TAG-network-online.target.wants/$TAG-wait-online.service"
  rmdir "$UNITS/$TAG-network-online.target.wants" 2> /dev/null
  if [ "$provider" = yes ]; then
    mkdir -p "$UNITS/$TAG-network-online.target.wants"
    ln -sf "$UNITS/$TAG-wait-online.service" "$UNITS/$TAG-network-online.target.wants/"
  fi
  systemctl --user daemon-reload
}

run_probe() {   # <label> <provider: yes|no>
  rm -f "$RUN/probe.ran"
  probe_unit "$2"
  systemctl --user reset-failed "$TAG-probe.service" "$TAG-wait-online.service" > /dev/null 2>&1
  local t0; t0=$(date +%s.%N)
  systemctl --user start "$TAG-probe.service" > /dev/null 2>&1
  local rc=$?
  local t1; t1=$(date +%s.%N)
  local wait_result; wait_result=$(systemctl --user show -p Result --value "$TAG-wait-online.service" 2>/dev/null)
  local delay; delay=$(python3 -c "print('%.1f' % ($t1 - $t0))")
  if [ -s "$RUN/probe.ran" ]; then
    ok "$1: the unit ran (start rc=$rc, ${delay}s after the start request, wait-online Result=${wait_result:-not-loaded})"
  else
    fail "$1: the unit did NOT run -- Wants= blocked a board with no network (rc=$rc, ${delay}s)"
  fi
}

run_probe "provider present and failing (no carrier)" yes
run_probe "no provider at all (image never installed one)" no

echo
if [ "$FAILED" = 0 ]; then
  echo "i1334_units: every measurement above held"
else
  echo "i1334_units: see the FAIL lines above"
fi
exit "$FAILED"

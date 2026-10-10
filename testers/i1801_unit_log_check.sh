#!/bin/bash
# #1801: does the packaged unit start every board with --autocams and a log file of its own
# per start, and does the shipped tmpfiles.d rule prune the old ones and never the newest?
#
# unrun-tester: not in run_all.sh, for testers/i1660_check.sh's reason and in its shape --
# phase 1 asks a live systemd manager what it makes of the unit's ExecStart, escapes and all,
# and run_all.sh runs where there is none. It uses the calling user's session manager
# (`systemctl --user`, no root, one unit named after this checkout and removed on the way
# out) and exits 2 where there is none. Phase 3 needs docker and $OD_IMAGE.
#
#   testers/i1801_unit_log_check.sh
#   OD_1801_TEMPLATE=<a unit template> OD_1801_TMPFILES=<a tmpfiles.d file> testers/i1801_unit_log_check.sh
#       -- main before #1801 (`git show d5e3666:templates/opendartboard.service.template`)
#       must be red in phase 1; the rule with its `x` line deleted must be red in phase 2.
#
# THE DEFECT. The unit started the detector with a fixed `--cams /dev/video0,/dev/video1,
# /dev/video2` and no --log-file: a Pi whose cameras expose two nodes each faulted on its
# first boot, and no packaged board ever wrote the log the #1787 upload posts.
#
# What it measures:
#
#   1 unit     The template's ExecStart, rendered as `make deb` renders it and lifted
#              verbatim into a user unit, with /var/lib/opendartboard and the binary renamed
#              into this run. The binary is a stub that records its argv and its parent. Started
#              three times: three distinct files named opendartboard-<UTC>.log, the newest
#              alone in logs/current/ and the two before it moved up to logs/; the stub was
#              handed --autocams, no --cams, and --log-file naming that start's file; the
#              journal line names it; and the stub's parent is the manager itself, so the
#              shell exec'd it and NotifyAccess=main still reaches the detector.
#   2 prune    The shipped tmpfiles.d rule, renamed into this run and with its age cut to
#              2s, through `systemd-tmpfiles --clean` on this box: the old files go, the
#              newest and both directories stay. Control: the same rule without its `x`
#              line removes the newest too, so the exclusion is what keeps it.
#   3 older    Phase 2's two runs again inside $OD_IMAGE, whose systemd (247) is older
#              than Raspberry Pi OS Bookworm's (252), so the rule's syntax and the `x`
#              exclusion are not a property of this box's systemd alone.
#
# MEASURED while writing this, and the reason the newest file is excluded by place rather
# than by a lock: systemd 255 skips a file another process holds a BSD lock on, systemd 252
# (debian:12, what a Bookworm Pi runs) removes it anyway.
#
# It ends on an exit status, not on an echo (#1463).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1801/check"
TEMPLATE="${OD_1801_TEMPLATE:-$OD_TREE_ROOT/templates/opendartboard.service.template}"
TMPFILES="${OD_1801_TMPFILES:-$OD_TREE_ROOT/distributions/debian_arm64/tmpfiles.conf}"
UNITS="$HOME/.config/systemd/user"
TAG="od-$OD_TREE_TAG-1801"
FAILED=0
SKIPPED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

cleanup() {
  systemctl --user stop "$TAG.service" > /dev/null 2>&1
  systemctl --user reset-failed "$TAG.service" > /dev/null 2>&1
  rm -f "$UNITS/$TAG.service"
  systemctl --user daemon-reload > /dev/null 2>&1
}
trap cleanup EXIT

rm -rf "$RUN" && mkdir -p "$RUN/state" "$RUN/bin" || exit 2
STATE="$RUN/state"
NAME_RE='^opendartboard-[0-9]{8}T[0-9]{6}Z\.log$'

# ---- 1 unit ------------------------------------------------------------------------------
echo "--- 1 unit: the template's ExecStart, under a live manager ---"
env WIDTH=1280 HEIGHT=720 FPS=30 envsubst '${WIDTH} ${HEIGHT} ${FPS}' < "$TEMPLATE" > "$RUN/opendartboard.service"
# ExecStart= and every line it continues with a trailing backslash, exactly as written.
awk '/^ExecStart=/ {on=1} on {print} on && !/\\$/ {exit}' "$RUN/opendartboard.service" > "$RUN/execstart"
sed 's/^/     | /' "$RUN/execstart"

cat > "$RUN/bin/opendartboard" <<'EOF'
#!/bin/sh
# The detector's stand-in: what it was handed, who started it, and one line in the log it was given.
out="$OD_1801_OUT/start.$(ls "$OD_1801_OUT" | grep -c '^start\.')"
{ echo "parent $(cat /proc/$PPID/comm)"; for a in "$@"; do echo "arg $a"; done; } > "$out"
prev=""; for a in "$@"; do [ "$prev" = "--log-file" ] && echo "[stub] a line" >> "$a"; prev="$a"; done
exit 0
EOF
chmod +x "$RUN/bin/opendartboard"
mkdir -p "$RUN/out"

if [ "$(ps -p 1 -o comm=)" != "systemd" ] || ! systemctl --user is-system-running > /dev/null 2>&1; then
  echo "     | this box has no systemd session manager; phase 1 is not measured here"
  SKIPPED=1
else
  mkdir -p "$UNITS"
  {
    echo "[Unit]"
    echo "Description=#1801 per-start log probe"
    echo "[Service]"
    echo "Type=oneshot"
    echo "Environment=OD_1801_OUT=$RUN/out"
    sed -e "s#/var/lib/opendartboard#$STATE#g" -e "s#/usr/local/bin/opendartboard#$RUN/bin/opendartboard#g" "$RUN/execstart"
    echo "StandardOutput=append:$RUN/unit.out"
    echo "StandardError=append:$RUN/unit.out"
  } > "$UNITS/$TAG.service"
  systemctl --user daemon-reload
  for n in 0 1 2; do
    [ "$n" = 0 ] || sleep 1.2   # one file per start is one per second at most; RestartSec=2 holds a board to that
    systemctl --user start "$TAG.service" || fail "start $n: the unit did not run"
    cur[$n]="$(ls "$STATE/logs/current" 2>/dev/null | tr '\n' ' ')"
    cur[$n]="${cur[$n]% }"
  done
  sed 's/^/     | journal: /' "$RUN/unit.out" 2>/dev/null

  # Three starts, three names, each the right shape, the newest alone in current/.
  names="$( { ls "$STATE/logs" 2>/dev/null; ls "$STATE/logs/current" 2>/dev/null; } | grep '\.log$')"
  echo "$names" | sed 's/^/     | file: /'
  [ "$(echo "$names" | grep -cE "$NAME_RE")" = 3 ] && [ "$(echo "$names" | sort -u | grep -c .)" = 3 ] \
    && ok "three starts wrote three files, each named opendartboard-<UTC start>.log" \
    || fail "three starts did not write three distinctly named opendartboard-<UTC>.log files"
  [ "$(ls "$STATE/logs/current" 2>/dev/null | grep -c .)" = 1 ] && [ "$(ls "$STATE/logs/current")" = "${cur[2]}" ] \
    && ok "logs/current/ holds the newest start's file and no other" \
    || fail "logs/current/ holds [$(ls "$STATE/logs/current" 2>/dev/null | tr '\n' ' ')], not the third start's [${cur[2]}]"
  [ "$(ls "$STATE/logs" 2>/dev/null | grep -c '\.log$')" = 2 ] \
    && ok "the two starts before it were moved up into logs/" \
    || fail "logs/ holds [$(ls "$STATE/logs" 2>/dev/null | tr '\n' ' ')], not the first two starts' files"

  for n in 0 1 2; do
    s="$RUN/out/start.$n"
    if [ ! -f "$s" ]; then fail "start $n: the stub never ran"; continue; fi
    want="$STATE/logs/current/${cur[$n]}"
    grep -qx "arg --autocams" "$s" && ! grep -qx "arg --cams" "$s" \
      && ok "start $n: the detector was handed --autocams and no --cams" \
      || fail "start $n: the detector's cameras were [$(grep -A1 -E 'arg --(auto)?cams' "$s" | tr '\n' ' ')]"
    [ "$(awk '$2=="--log-file" {getline; print substr($0,5)}' "$s")" = "$want" ] \
      && ok "start $n: --log-file is that start's own file" \
      || fail "start $n: --log-file was [$(awk '$2=="--log-file" {getline; print substr($0,5)}' "$s")], not $want"
    grep -qx "arg --width" "$s" && grep -qx "arg 1280" "$s" && grep -qx "arg --model" "$s" \
      || fail "start $n: the detector's own flags did not arrive as separate arguments"
    [ "$(awk '$1=="parent" {print $2}' "$s")" = systemd ] \
      && ok "start $n: the detector's parent is the manager, not the shell: it was exec'd, so it is the main process" \
      || fail "start $n: the detector's parent was [$(awk '$1=="parent" {print $2}' "$s")], not the manager; the shell did not exec it"
    grep -qx "opendartboard: this start logs to $want" "$RUN/unit.out" \
      && ok "start $n: the journal names the file" \
      || fail "start $n: no journal line names $want"
  done
fi

# ---- 2 prune -----------------------------------------------------------------------------
# The rule as shipped, renamed into a directory of this run's and with its age cut to 2s.
# A file's ctime counts and cannot be set back, so the files are made and then waited out.
prune_fixture() {   # <dir> -> logs/ two old files, logs/current/ the newest
  rm -rf "$1" && mkdir -p "$1/logs/current"
  echo old > "$1/logs/opendartboard-20261001T000000Z.log"
  echo old > "$1/logs/opendartboard-20261002T000000Z.log"
  echo newest > "$1/logs/current/opendartboard-20261003T000000Z.log"
}
render_rule() {     # <state dir> <out> [drop-x]
  sed -e '/^[[:space:]]*#/d' -e "s#/var/lib/opendartboard#$1#g" -e 's/ 7d$/ 2s/' "$TMPFILES" > "$2"
  [ "${3:-}" = drop-x ] && sed -i '/^x /d' "$2"
  return 0
}
judge_prune() {     # <dir> <label>
  local left; left="$(cd "$1" && find . -type f | sort | tr '\n' ' ')"
  echo "     | $2: left [$left]"
  [ ! -e "$1/logs/opendartboard-20261001T000000Z.log" ] && [ ! -e "$1/logs/opendartboard-20261002T000000Z.log" ] \
    && ok "$2: the files in logs/ older than the age were pruned" \
    || fail "$2: an old file in logs/ survived the age"
  [ -f "$1/logs/current/opendartboard-20261003T000000Z.log" ] \
    && ok "$2: the newest file, in logs/current/, was not touched" \
    || fail "$2: the newest file was pruned"
  [ -d "$1/logs" ] && [ -d "$1/logs/current" ] \
    && ok "$2: both directories stay" || fail "$2: a directory was removed"
}
judge_control() {   # <dir> <label>
  [ ! -e "$1/logs/current/opendartboard-20261003T000000Z.log" ] \
    && ok "$2 control: without the x line the newest file goes too, so the exclusion is what keeps it" \
    || fail "$2 control: the newest file survived without the x line; the check above proves nothing"
}

echo
echo "--- 2 prune: the shipped rule through this box's systemd-tmpfiles ---"
sed 's/^/     | /' "$TMPFILES" | grep -v '| #'
if ! command -v systemd-tmpfiles > /dev/null; then
  echo "     | no systemd-tmpfiles here; phase 2 is not measured"
  SKIPPED=1
else
  echo "     | $(systemd-tmpfiles --version | head -1)"
  grep -qE '^e[[:space:]]+/var/lib/opendartboard/logs[[:space:]]' "$TMPFILES" \
    || fail "the rule does not age /var/lib/opendartboard/logs"
  prune_fixture "$RUN/prune"; prune_fixture "$RUN/prune-ctl"
  render_rule "$RUN/prune" "$RUN/rule.conf"; render_rule "$RUN/prune-ctl" "$RUN/rule-ctl.conf" drop-x
  sleep 3
  systemd-tmpfiles --user --clean "$RUN/rule.conf" || fail "systemd-tmpfiles refused the rule"
  systemd-tmpfiles --user --clean "$RUN/rule-ctl.conf"
  judge_prune "$RUN/prune" "this box"
  judge_control "$RUN/prune-ctl" "this box"
fi

# ---- 3 older ---------------------------------------------------------------------------
echo
echo "--- 3 older: the same two runs inside $OD_IMAGE ---"
if ! docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
  echo "     | the image $OD_IMAGE is not on this machine; phase 3 is not measured"
  SKIPPED=1
else
  mkdir -p "$RUN/older"
  render_rule /w/prune "$RUN/older/rule.conf"; render_rule /w/prune-ctl "$RUN/older/rule-ctl.conf" drop-x
  docker rm -f "$(od_name 1801-older)" > /dev/null 2>&1
  docker run --rm --name "$(od_name 1801-older)" --network none -v "$RUN/older":/w "$OD_IMAGE" bash -c '
    '"$(declare -f prune_fixture)"'
    prune_fixture /w/prune; prune_fixture /w/prune-ctl
    echo "     | $(systemd-tmpfiles --version | head -1)"
    sleep 3
    systemd-tmpfiles --clean /w/rule.conf || echo "systemd-tmpfiles refused the rule" > /w/refused
    systemd-tmpfiles --clean /w/rule-ctl.conf
    chown -R '"$(id -u):$(id -g)"' /w'
  [ -e "$RUN/older/refused" ] && fail "$OD_IMAGE: systemd-tmpfiles refused the rule"
  judge_prune "$RUN/older/prune" "$OD_IMAGE"
  judge_control "$RUN/older/prune-ctl" "$OD_IMAGE"
fi

echo
if [ "$FAILED" = 0 ] && [ "$SKIPPED" = 1 ]; then
  echo "i1801: nothing failed, but not every phase could be measured on this box"
  exit 2
fi
if [ $FAILED = 0 ]; then echo "i1801: PASS"; else echo "i1801: FAIL"; fi
exit $FAILED

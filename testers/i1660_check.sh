#!/bin/bash
# #1660: under opendartboard.service, where does the board keep its credential?
#
# unrun-tester: not in run_all.sh, for testers/i1383_units.sh's reason and in its shape --
# phase 3 asks a live systemd manager what it does with this unit's keys, and run_all.sh
# runs where there is none. It uses the calling user's session manager (`systemctl
# --user`, no root, every unit and directory named after this checkout and removed on the
# way out) and exits 2 where there is none, so a box without systemd is not turned red by
# a unit file that is fine.
#
#   testers/i1660_check.sh
#   OD_1660_SRC=<dir holding an od_paths.hpp> testers/i1660_check.sh   # a mutant, or main's
#
# THE DEFECT. The unit sets no User=; systemd gives such a unit neither $HOME nor
# $XDG_CONFIG_HOME; od_paths::configDir() then answered "", main.cpp joined "" onto
# "credentials.json", and a relative path in a unit whose working directory is `/` is
# /credentials.json. pairingBlock()'s NoConfigDir tested for an EMPTY path, which that
# never was, so the guard existed and could not fire.
#
# What it measures, against src/utils/od_paths.hpp compiled alone (i1660_paths_probe.cpp):
#
#   1 env      The probe under `env -i` and five variations of it. The unit's own
#              environment -- nothing but $STATE_DIRECTORY -- must resolve to that
#              directory; NO environment at all must resolve to NO path, never to the bare
#              leaf; and the answers every tester already relies on ($HOME, $XDG_CONFIG_HOME)
#              must not have moved.
#   2 template The rendered unit through `systemd-analyze verify`, and its directives:
#              StateDirectory=opendartboard, mode 0700, and a WorkingDirectory= that is
#              that same directory. Directives, not prose (#1430): the comments above them
#              name every one of these words.
#   3 unit     The template's own three lines, lifted out and renamed for this checkout, in
#              a real unit under a real manager, with $HOME and $XDG_CONFIG_HOME taken
#              away as a system unit has neither. The manager must create the directory
#              0700, export it, start the probe IN it, and the probe must resolve its
#              credential there.
#
# It ends on an exit status, not on an echo (#1463).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1660/check"
SRC="${OD_1660_SRC:-$OD_TREE_ROOT/src/utils}"
TEMPLATE="${OD_1660_TEMPLATE:-$OD_TREE_ROOT/templates/opendartboard.service.template}"
UNITS="$HOME/.config/systemd/user"
TAG="od-$OD_TREE_TAG-1660"
FAILED=0
SKIPPED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

cleanup() {
  systemctl --user stop "$TAG.service" > /dev/null 2>&1
  systemctl --user reset-failed "$TAG.service" > /dev/null 2>&1
  rm -f "$UNITS/$TAG.service"
  systemctl --user daemon-reload > /dev/null 2>&1
  # The state directory the manager made for phase 3. StateDirectory= is kept across stops
  # by design, so it is removed here or it would answer the next run's mode question.
  rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/$TAG-state"
}
trap cleanup EXIT

rm -rf "$RUN"; mkdir -p "$RUN"
cp "$SRC/od_paths.hpp" "$RUN/od_paths.hpp" || { echo "no od_paths.hpp in $SRC" >&2; exit 2; }
cp "$OD_TREE_ROOT/testers/i1660_paths_probe.cpp" "$RUN/"
# od_paths.hpp names od_platform_first.hpp on Windows only; nothing else is needed.

# ---- the probe ---------------------------------------------------------------------------
# The host's compiler where there is one and $OD_IMAGE's otherwise; the probe is RUN on the
# host either way, because phase 3's manager is the host's.
if command -v g++ > /dev/null; then
  g++ -std=c++17 -Wall -Wextra -O1 -I "$RUN" -o "$RUN/probe" "$RUN/i1660_paths_probe.cpp" 2> "$RUN/probe.build"
  BUILT=$?
elif command -v docker > /dev/null && docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
  docker run --rm --name "$(od_name i1660-probe)" --cpus=1 --network none -v "$RUN":/out "$OD_IMAGE" \
    g++ -std=c++17 -Wall -Wextra -O1 -I /out -o /out/probe /out/i1660_paths_probe.cpp 2> "$RUN/probe.build"
  BUILT=$?
else
  echo "i1660_check: no g++ on this box and no $OD_IMAGE to borrow one from" >&2
  exit 2
fi
if [ "$BUILT" != 0 ] || [ ! -x "$RUN/probe" ]; then
  fail "od_paths.hpp does not compile with the probe:"
  sed 's/^/     | /' "$RUN/probe.build"
  exit 1
fi

field() { sed -n "s/^$1=//p"; }   # field <name> < probe output

# ---- 1 env -------------------------------------------------------------------------------
echo "--- 1 env: the probe under env -i, from an empty working directory ---"
mkdir -p "$RUN/cwd"
probe() { (cd "$RUN/cwd" && env -i "$@" "$RUN/probe"); }
expect() { # expect <what> <expected dir> <expected credentials> [VAR=value ...]
  local what="$1" want_dir="$2" want_cred="$3"; shift 3
  local out; out="$(probe "$@")"
  local dir cred
  dir="$(echo "$out" | field dir)"; cred="$(echo "$out" | field credentials)"
  echo "     | env -i $* -> dir=[$dir] credentials=[$cred]"
  if [ "$dir" = "$want_dir" ] && [ "$cred" = "$want_cred" ]; then
    ok "$what"
  else
    fail "$what: expected dir=[$want_dir] credentials=[$want_cred]"
  fi
}
expect "the unit's environment: \$STATE_DIRECTORY and nothing else is where the credential goes" \
  /var/lib/opendartboard /var/lib/opendartboard/credentials.json STATE_DIRECTORY=/var/lib/opendartboard
expect "no environment at all is NO path -- never the bare leaf, which is the working directory" \
  "" ""
expect "a manager's word beats a home: \$STATE_DIRECTORY wins over \$HOME" \
  /var/lib/opendartboard /var/lib/opendartboard/credentials.json STATE_DIRECTORY=/var/lib/opendartboard HOME=/root
expect "several state directories: the first is taken" \
  /var/lib/a /var/lib/a/credentials.json STATE_DIRECTORY=/var/lib/a:/var/lib/b
expect "a relative \$STATE_DIRECTORY is the working directory by another name, and is ignored" \
  /root/.config/opendartboard /root/.config/opendartboard/credentials.json STATE_DIRECTORY=relative HOME=/root
expect "unchanged: \$HOME alone is ~/.config/opendartboard, which every container tester reads" \
  /root/.config/opendartboard /root/.config/opendartboard/credentials.json HOME=/root
expect "unchanged: \$XDG_CONFIG_HOME beats \$HOME" \
  /x/opendartboard /x/opendartboard/credentials.json XDG_CONFIG_HOME=/x HOME=/root
if [ -z "$(ls -A "$RUN/cwd")" ]; then
  ok "and the probe left nothing in the working directory it ran in"
else
  fail "something was written into the working directory: $(ls -A "$RUN/cwd" | tr '\n' ' ')"
fi

# ---- 2 template --------------------------------------------------------------------------
echo
echo "--- 2 template: what the unit says, and whether systemd accepts it ---"
env WIDTH=1280 HEIGHT=720 FPS=30 envsubst < "$TEMPLATE" > "$RUN/opendartboard.service"
DIRECTIVES="$RUN/directives"
grep -v '^[[:space:]]*#' "$RUN/opendartboard.service" > "$DIRECTIVES"
key() { sed -n "s/^[[:space:]]*$1=//p" "$DIRECTIVES" | tail -1; }
STATE_NAME="$(key StateDirectory)"
STATE_MODE="$(key StateDirectoryMode)"
WORKDIR="$(key WorkingDirectory)"
echo "     | StateDirectory=[$STATE_NAME] StateDirectoryMode=[$STATE_MODE] WorkingDirectory=[$WORKDIR] User=[$(key User)]"
[ "$STATE_NAME" = "opendartboard" ] && ok "StateDirectory=opendartboard: systemd makes /var/lib/opendartboard and exports it" \
  || fail "the unit does not name StateDirectory=opendartboard, so a board under it has no \$STATE_DIRECTORY"
[ "$STATE_MODE" = "0700" ] && ok "StateDirectoryMode=0700: the directory holding a bearer token is root's alone" \
  || fail "StateDirectoryMode is [$STATE_MODE], not 0700"
[ "$WORKDIR" = "/var/lib/$STATE_NAME" ] && ok "WorkingDirectory= is that directory, so score_token and cache/ are not in /" \
  || fail "WorkingDirectory=[$WORKDIR] is not /var/lib/$STATE_NAME; what the detector keeps relatively lands elsewhere"
if command -v systemd-analyze > /dev/null; then
  printf '#!/bin/sh\nexit 0\n' > "$RUN/opendartboard"; chmod +x "$RUN/opendartboard"
  sed "s#/usr/local/bin/#$RUN/#" "$RUN/opendartboard.service" > "$RUN/verify.service"
  if out=$(systemd-analyze verify "$RUN/verify.service" 2>&1) && [ -z "$out" ]; then
    ok "systemd-analyze verify is silent on the unit this repository ships"
  else
    fail "systemd-analyze verify complained: $out"
  fi
  sed 's/^StateDirectoryMode=/StateDirectoryMod=/' "$RUN/verify.service" > "$RUN/misspelt.service"
  if systemd-analyze verify "$RUN/misspelt.service" 2>&1 | grep -q "Unknown key name 'StateDirectoryMod'"; then
    ok "control: one letter out of that key is reported, so the verify above can speak"
  else
    fail "control: systemd did not object to a misspelt StateDirectoryMode; the verify above proves nothing"
  fi
else
  echo "     | no systemd-analyze here; the unit was read but not verified"
  SKIPPED=1
fi

# ---- 3 unit ------------------------------------------------------------------------------
echo
echo "--- 3 unit: the template's own lines, under a live manager, with no HOME ---"
if [ "$(ps -p 1 -o comm=)" != "systemd" ] || ! systemctl --user is-system-running > /dev/null 2>&1; then
  echo "     | this box has no systemd session manager; phase 3 is not measured here"
  SKIPPED=1
elif [ -z "$STATE_NAME" ]; then
  fail "phase 3 has nothing to run: the template names no StateDirectory="
else
  # The template's lines, renamed for this checkout. A user manager's StateDirectory= is
  # under ~/.local/state rather than /var/lib, which is what %S says for either manager;
  # the template's /var/lib/ is the system manager's %S, and phase 2 held it to the name.
  mkdir -p "$UNITS"
  {
    echo "[Unit]"
    echo "Description=#1660 config-directory probe"
    echo "[Service]"
    echo "Type=oneshot"
    grep -E '^[[:space:]]*StateDirectory(Mode)?=' "$DIRECTIVES" | sed "s/=opendartboard\$/=$TAG-state/"
    [ -n "$WORKDIR" ] && echo "WorkingDirectory=%S/$TAG-state"
    # A system unit with no User= has neither; a user manager hands out both, so they are
    # taken away here and the probe sees what the detector sees on a Pi.
    echo "ExecStart=/usr/bin/env -u HOME -u XDG_CONFIG_HOME $RUN/probe"
    echo "StandardOutput=file:$RUN/unit.out"
  } > "$UNITS/$TAG.service"
  sed 's/^/     | /' "$UNITS/$TAG.service"
  systemctl --user daemon-reload
  systemctl --user start "$TAG.service"
  STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/$TAG-state"
  OUT="$(cat "$RUN/unit.out" 2>/dev/null)"
  echo "$OUT" | sed 's/^/     | probe: /'
  [ "$(echo "$OUT" | field dir)" = "$STATE_DIR" ] \
    && ok "under the manager, with no HOME, configDir() is the directory the manager made" \
    || fail "under the manager configDir() is [$(echo "$OUT" | field dir)], not $STATE_DIR"
  [ "$(echo "$OUT" | field credentials)" = "$STATE_DIR/credentials.json" ] \
    && ok "and the credential is kept in it" \
    || fail "the credential would be kept at [$(echo "$OUT" | field credentials)]"
  [ "$(echo "$OUT" | field cwd)" = "$STATE_DIR" ] \
    && ok "the unit starts in it, so a relative file is kept there too, not in /" \
    || fail "the unit started in [$(echo "$OUT" | field cwd)], not $STATE_DIR"
  MODE="$(stat -c %a "$STATE_DIR" 2>/dev/null)"
  [ "$MODE" = "700" ] && ok "the manager made it mode 700" || fail "the directory is mode [$MODE], not 700"
fi

echo
echo "load_at_end=$(cut -d' ' -f1-3 /proc/loadavg)"
if [ "$FAILED" = 0 ] && [ "$SKIPPED" = 1 ]; then
  echo "i1660_check: nothing failed, but not every phase could be measured on this box"
  exit 2
fi
exit $FAILED

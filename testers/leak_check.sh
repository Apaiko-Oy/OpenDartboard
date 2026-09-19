#!/bin/bash
# #1341: a harness that is killed does not leave its container running, and a name that is
# somehow still in use is reported as itself. Proved by killing a run six ways and looking,
# rather than by having done it once by hand.
#
#   testers/leak_check.sh
#
# It costs about two minutes and no build: every container it starts is `sleep`.
#
# WHY THIS EXISTS AS A CHECK. The leak was met twice by hand on 2026-09-18 and reasoned
# about twice more -- `--rm` was believed to cover the friendly signal, and on this box it
# covers nothing at all (see tester_paths.sh for the eight measurements). A belief about
# which signals reach a container is exactly the kind of thing that is true until Docker,
# the kernel or coreutils changes under it, so it is measured on every run instead.
#
# The six kills are the ones that really happen here:
#
#   group TERM    what run_all.sh's `timeout` sends. coreutils timeout puts the tester in
#                 its own process group and signals the GROUP, so the harness and the
#                 docker client are hit together.
#   group KILL    `timeout -s KILL`, and `--kill-after` once anybody adds one.
#   TERM/INT/HUP  on the harness alone: a Ctrl-C, a closed terminal, a supervisor.
#   KILL          on the harness alone. Nothing can trap it, so this is the case the
#                 watchdog exists for and the only one worth arguing about.
#
# Everything here runs under a tree tag of its own, for two reasons that both matter: this
# check's own sweep then cannot reach a real tester's container -- it would otherwise reap
# whatever 1317-asan happens to be running -- while run_all.sh's sweep, which matches the
# tree tag as a prefix, still reaches anything this check leaves behind.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

OD_TREE_TAG="$OD_TREE_TAG-leakcheck"
WORK="$OD_RUNS_BASE/leakcheck"
rm -rf "$WORK" 2> /dev/null
mkdir -p "$WORK"

PASS=0
FAILED=0
ok()  { echo "OK   $*"; PASS=$((PASS + 1)); }
bad() { echo "FAIL $*"; FAILED=$((FAILED + 1)); }

# The thing being killed: an ordinary harness, in that it starts its container the one way
# a harness is allowed to, through od_run.
VICTIM="$WORK/victim.sh"
cat > "$VICTIM" <<EOF
set -u
. "$OD_TREE_ROOT/testers/tester_paths.sh"
OD_TREE_TAG="$OD_TREE_TAG"
od_run "\$1" --network none "\$OD_IMAGE" sleep 600
EOF

# A victim started with `&` is an ASYNCHRONOUS command, and a shell sets SIGINT and SIGQUIT
# to ignore in one of those -- POSIX, so that a Ctrl-C at the terminal does not reach the
# background job. A signal ignored on entry cannot be trapped or reset from inside bash
# either, so a plain `setsid bash victim &` produces a harness that ignores its own SIGINT,
# does not die, and leaves its container up because nothing killed anything. That reads as
# a hole in the trap and is a hole in the test: this check measured it on 2026-09-19 and
# spent the finding working out which it was. So the victim is launched through something
# that puts the dispositions back to default and then execs, which is what a Ctrl-C at a
# terminal really hands a harness.
LAUNCH="$WORK/launch.py"
cat > "$LAUNCH" <<'PYEOF'
import os, signal, sys
for name in ("SIGINT", "SIGQUIT", "SIGHUP", "SIGTERM"):
    signal.signal(getattr(signal, name), signal.SIG_DFL)
os.setsid()                       # its own session, so a group kill here is not a group kill there
os.execvp("bash", ["bash"] + sys.argv[1:])
PYEOF

up_within() {   # up_within <name> <seconds>
  local n="$1" s="$2" i=0
  while [ "$i" -lt "$s" ]; do
    [ "$(docker inspect -f '{{.State.Running}}' "$n" 2> /dev/null)" = "true" ] && return 0
    sleep 1
    i=$((i + 1))
  done
  return 1
}

gone_within() { # gone_within <name> <seconds>
  local n="$1" s="$2" i=0
  while [ "$i" -lt "$s" ]; do
    docker inspect "$n" > /dev/null 2>&1 || return 0
    sleep 1
    i=$((i + 1))
  done
  return 1
}

# A kill, and then the only question worth asking: is anything left?
kill_case() {   # kill_case <phase> <how> <signal>
  local phase="$1" how="$2" sig="$3" name pid pgid mine
  name="$(od_name "$phase")"
  docker rm -f "$name" > /dev/null 2>&1

  # Its own session, so a kill aimed at the group is the group kill `timeout` really
  # sends and not a kill of this check.
  python3 "$LAUNCH" "$VICTIM" "$phase" > /dev/null 2>&1 &
  pid=$!
  # Forgotten as a job, so the shell does not announce its death as
  # `leak_check.sh: line NN: 796944 Killed ...` in the middle of a gate log, where it reads
  # like a fault rather than like the point of the exercise.
  disown "$pid" 2> /dev/null

  if ! up_within "$name" 60; then
    bad "$how: $name never came up, so this case proved nothing"
    kill -9 "$pid" > /dev/null 2>&1
    docker rm -f "$name" > /dev/null 2>&1
    return
  fi

  case "$how" in
    group-*)
      pgid="$(ps -o pgid= -p "$pid" 2> /dev/null | tr -d ' ')"
      mine="$(ps -o pgid= -p $$ 2> /dev/null | tr -d ' ')"
      if [ -z "$pgid" ] || [ "$pgid" = "$mine" ]; then
        bad "$how: the victim shares this check's process group, so the group kill would"
        echo "     take this check with it. Refusing to send it."
        kill -9 "$pid" > /dev/null 2>&1
        docker rm -f "$name" > /dev/null 2>&1
        return
      fi
      kill -s "$sig" -- "-$pgid" > /dev/null 2>&1
      ;;
    *)
      kill -s "$sig" "$pid" > /dev/null 2>&1
      ;;
  esac

  # The trap reaps at once; the watchdog polls, so it is allowed a few seconds.
  if gone_within "$name" 25; then
    ok "$how: nothing left behind"
  else
    bad "$how: $name is still $(docker inspect -f '{{.State.Status}}' "$name" 2> /dev/null)"
    docker rm -f "$name" > /dev/null 2>&1
  fi
  while kill -0 "$pid" 2> /dev/null; do sleep 1; done
}

echo "--- killed six ways, and what is left each time ---"
kill_case p-group-term  group-TERM    TERM
kill_case p-group-kill  group-KILL    KILL
kill_case p-term        harness-TERM  TERM
kill_case p-int         harness-INT   INT
kill_case p-hup         harness-HUP   HUP
kill_case p-kill        harness-KILL  KILL

echo
echo "--- a run nobody kills still answers with what the container answered ---"
# The trap is bought by backgrounding the container and waiting on it, and a shell that
# returns the wrong status for that is worse than the leak: every tester in run_all.sh is
# read as an exit code and nothing else (#1335).
od_run "p-rc" --network none "$OD_IMAGE" bash -c 'exit 7'
RC=$?
if [ "$RC" = 7 ]; then
  ok "od_run returns the container's own exit status ($RC)"
else
  bad "od_run returned $RC where the container exited 7"
fi
od_run "p-rc0" --network none "$OD_IMAGE" true
RC=$?
if [ "$RC" = 0 ]; then
  ok "od_run returns 0 for a container that succeeded"
else
  bad "od_run returned $RC for a container that succeeded"
fi

echo
echo "--- a name already in use is reported as itself, not as rc=125 from Docker ---"
CLASH="$(od_name p-clash)"
docker rm -f "$CLASH" > /dev/null 2>&1
docker run -d --name "$CLASH" --network none "$OD_IMAGE" sleep 600 > /dev/null 2>&1
OUT="$(bash "$VICTIM" p-clash 2>&1)"
RC=$?
if [ "$RC" = 125 ] && printf '%s' "$OUT" | grep -q "still here" && printf '%s' "$OUT" | grep -q "docker rm -f $CLASH"; then
  ok "a taken name is refused in words, with the remedy, and with Docker's own rc=125"
else
  bad "a taken name was not reported as itself: rc=$RC"
  printf '%s\n' "$OUT" | sed 's/^/     | /'
fi
docker rm -f "$CLASH" > /dev/null 2>&1

echo
echo "--- the backstop reports what it reaps ---"
STRAY="$(od_name p-stray)"
docker rm -f "$STRAY" > /dev/null 2>&1
docker run -d --name "$STRAY" --network none "$OD_IMAGE" sleep 600 > /dev/null 2>&1
SWEPT="$(od_sweep)"
if printf '%s' "$SWEPT" | grep -q "LEAKED $STRAY" && ! docker inspect "$STRAY" > /dev/null 2>&1; then
  ok "od_sweep names what it reaped rather than reaping it quietly"
else
  bad "od_sweep did not report and reap $STRAY"
  printf '%s\n' "$SWEPT" | sed 's/^/     | /'
  docker rm -f "$STRAY" > /dev/null 2>&1
fi

echo
echo "--- no two phases of one harness share a container name ---"
A="$(od_phase "$OD_TREE_ROOT/testers/phases1317/1317-partial.sh")"
B="$(od_phase "$OD_TREE_ROOT/testers/phases1317/1317-asan.sh")"
if [ "$A" != "$B" ] && [ -n "$A" ] && [ -n "$B" ]; then
  ok "i1317's two phases reduce to different names ($A, $B)"
else
  bad "i1317's two phases both reduce to '$A'"
fi
for harness in i1317_run.sh i1305_check.sh; do
  if grep -q 'od_run "[^"$]*\$' "$OD_TREE_ROOT/testers/$harness"; then
    ok "$harness builds its container name out of what it was handed"
  else
    bad "$harness names its container with a literal, so two phases of it would collide"
  fi
done

echo
rm -rf "$WORK" 2> /dev/null
echo "$PASS passed, $FAILED failed"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
[ "$FAILED" = 0 ] || exit 1
exit 0

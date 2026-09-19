# Sourced by every harness in this directory. No tester names a worktree.
#
# Until #1335 each harness carried the absolute path of the tree it was written in
# (/home/mikko/opendartboard/i1320, i1318, ...), so running a sibling's tester meant
# copying it to scratch and repointing it by hand. Three agents each invented that
# workaround separately, and while they did, two testers went red on main and nobody
# saw it. A tester now runs from whatever checkout it is in.
#
#   OD_TREE_ROOT   the checkout, derived from this file's own location
#   OD_TREE_TAG    that checkout's name, reduced to what a container name may hold
#   OD_RUNS_BASE   where run directories go: beside the tree, named after it, so two
#                  checkouts on one box cannot write into each other's run. Override
#                  with OD_RUNS.
#   OD_IMAGE       the image every harness runs in. Override with OD_IMAGE.
#   od_name        a container name unique to this checkout
#   od_still       a still JPEG for the phases that hand the detector a frozen frame
#
# Sourced, never executed: it defines and sets, and runs nothing.

OD_TREE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OD_TREE_TAG="$(basename "$OD_TREE_ROOT")"
OD_TREE_TAG="${OD_TREE_TAG//[^A-Za-z0-9_.-]/-}"   # a container name holds no more than this
OD_RUNS_BASE="${OD_RUNS:-$(dirname "$OD_TREE_ROOT")/runs-$OD_TREE_TAG}"
OD_IMAGE="${OD_IMAGE:-od-amd64:bullseye}"

# A container name carrying the checkout, so two trees running the same tester at once
# do not collide on the name each reaps by.
od_name() { echo "od-$OD_TREE_TAG-$1"; }

# The blind-camera phases hand the detector a JPEG where a camera should be. The file
# used to be /tmp/still892.jpg -- which a reboot removes -- and then one run directory's
# copy of it, which only exists on the box that ran that issue. It is a frame of the
# shipped mocks, so it is made from them, once per box, and cached beside the runs.
od_still() {
  local dest="$1"
  local cache="${OD_STILL:-$OD_RUNS_BASE/fixtures/still.jpg}"
  if [ ! -s "$cache" ]; then
    mkdir -p "$(dirname "$cache")" || return 1
    od_run still --network none \
      -v "$OD_TREE_ROOT":/app -v "$(dirname "$cache")":/fixtures "$OD_IMAGE" bash -c '
        g++ -std=c++17 -O1 -o /tmp/still /app/testers/still_frame.cpp \
          $(pkg-config --cflags --libs opencv4) || exit 1
        /tmp/still /app/mocks/cam_1.mp4 /fixtures/'"$(basename "$cache")"' 30' > /dev/null 2>&1 || return 1
  fi
  cp "$cache" "$dest"
}

# The phase a harness was handed, reduced to something a container name and a run
# directory may both carry (#1341). A harness that takes a phase script must put this in
# BOTH, or its two phases collide: before #1341 i1317_run.sh named its container
# od-<checkout>-i1317-partial whichever script it was given, so the ASan phase could not
# start while the partial phase's container was up, and -- quieter, and there for longer --
# the two phases wrote into one run directory, so the ASan run wiped the partial run's
# output and the summary line said RUN=partial about both.
#
#   od_phase testers/phases1317/1317-asan.sh   ->  asan
#   od_phase testers/phases891/givenup-nobeat.sh -> givenup-nobeat
#
# The leading issue number goes, because the harness's own name already carries it and
# od-i1341-i1317-1317-asan reads like a mistake. A phase script that does not start with
# one keeps its whole name.
od_phase() {
  local b
  b="$(basename "$1")"
  b="${b%.sh}"
  b="$(echo "$b" | sed 's/^[0-9][0-9]*-//')"
  b="${b//[^A-Za-z0-9_.-]/-}"
  echo "$b"
}

# ---- a container a killed harness does not leave behind (#1341) -------------------------
#
# `--rm` is not enough, and on this box it is not enough for ANY kill. Measured
# 2026-09-19, eight ways, every one of them leaving the container Up five seconds later:
#
#   kill                    harness   docker client   container
#   ----------------------  --------  --------------  ---------------
#   timeout's TERM          dies      SURVIVES        running
#   timeout -s KILL         dies      dies            running
#   kill -9 on the harness  dies      dies            running
#   kill -TERM on it        dies      dies            running
#
# Two mechanisms, and the friendly one is the surprise. `docker run` attached proxies a
# TERM to the container rather than dying of it -- and the container's process is PID 1 in
# its namespace, where the kernel applies no default action for a signal the process has
# installed no handler for. So the TERM reaches the container and is discarded, and the
# client sits waiting for a process that will never notice. A harder kill takes the client
# out but never touched the container to begin with: the daemon holds it, `--rm` is the
# daemon removing it AFTER IT EXITS, and nothing has asked it to exit.
#
# So a container is stopped by name, by somebody still alive to do it, and there are three
# of those in descending order of how much of the kill they survive:
#
#   od_run        a trap in the harness. Survives TERM, INT and HUP -- which is what
#                 run_all.sh's `timeout` sends -- and every ordinary and error exit.
#                 Cannot survive SIGKILL, because nothing can.
#   od_run        a watchdog beside it, in its own session so a process-GROUP kill misses
#                 it, holding the harness's pid and the container's name and reaping the
#                 second when the first is gone. This is the half that survives SIGKILL.
#   run_all.sh    a sweep after every tester, which reports what it had to reap. The
#                 backstop, and the only one that can say a leak happened at all.
#
# od_run is how a harness starts a container. It takes the SUFFIX od_name takes and then
# the arguments of `docker run`, and it adds `--rm --name`:
#
#   od_run "i1317-asan" --cpus=2 --network none -v "$RUN":/run1317 "$OD_IMAGE" bash inside.sh
#
# The container runs in the background with the shell waiting on it, which is not a detail:
# bash defers a trap until the foreground command it is running returns, and `docker run`
# attached to a container that is ignoring its TERM never returns. Backgrounded, the wait
# is interruptible and the trap fires at once.

OD_GUARDED=""                       # container names this shell has claimed, not yet let go
OD_GUARD_DIR="$OD_RUNS_BASE/guards" # where a watchdog leaves its pid so it can be called off
OD_GUARD_MAX_S="${OD_GUARD_MAX_S:-7200}"   # a watchdog never outlives this, whatever happens

# Reap one container. Says nothing, and is content when there was nothing there.
od_reap() { docker rm -f "$1" > /dev/null 2>&1; }

# Reap everything this shell still holds, and call off the watchdogs that were holding it
# for us. Idempotent: the EXIT trap and a signal trap may both reach it.
od_reap_guarded() {
  local n w
  for n in $OD_GUARDED; do
    w="$OD_GUARD_DIR/$n.watch"
    if [ -f "$w" ]; then
      kill "$(cat "$w" 2>/dev/null)" > /dev/null 2>&1
      rm -f "$w" 2>/dev/null
    fi
    docker rm -f "$n" > /dev/null 2>&1
  done
  OD_GUARDED=""
}

# Installed by the first od_run. A signal trap reaps and then dies of the signal it caught,
# so the harness's exit status still says what killed it.
od_install_traps() {
  [ "${OD_TRAPS_INSTALLED:-0}" = 1 ] && return 0
  OD_TRAPS_INSTALLED=1
  trap 'od_reap_guarded' EXIT
  trap 'od_reap_guarded; trap - INT;  kill -INT  $$' INT
  trap 'od_reap_guarded; trap - TERM; trap - EXIT; kill -TERM $$' TERM
  trap 'od_reap_guarded; trap - HUP;  trap - EXIT; kill -HUP  $$' HUP
}

# The half a trap cannot do. Its own session, so a kill aimed at the harness's process
# group -- which is what `timeout` aims -- does not take it with the harness. It holds a
# pid and a name and nothing else, it polls, and it gives up after OD_GUARD_MAX_S so a
# reused pid cannot leave it watching forever.
od_watchdog() {
  local name="$1"
  mkdir -p "$OD_GUARD_DIR" 2>/dev/null || return 0
  setsid bash -c '
    name="$1"; me="$2"; flag="$3"; limit="$4"
    echo $$ > "$flag"
    end=$(( $(date +%s) + limit ))
    while kill -0 "$me" 2> /dev/null; do
      sleep 2
      if [ "$(date +%s)" -gt "$end" ]; then rm -f "$flag"; exit 0; fi
    done
    docker rm -f "$name" > /dev/null 2>&1
    rm -f "$flag"
  ' od-watchdog "$name" "$$" "$OD_GUARD_DIR/$name.watch" "$OD_GUARD_MAX_S" \
    < /dev/null > /dev/null 2>&1 &
}

# A name already in use is reported as itself. Before #1341 this was Docker's own
# `Conflict. The container name "/od-fork-i1317-partial" is already in use`, arriving as
# rc=125 four seconds into a retry, which reads like a broken harness rather than like the
# timeout twenty minutes earlier that really caused it.
od_claim() {
  local name="$1" state
  state="$(docker inspect -f '{{.State.Status}} since {{.State.StartedAt}}' "$name" 2> /dev/null)" || return 0
  [ -z "$state" ] && return 0
  echo "FAIL $name: a container from an earlier run is still here ($state)." >&2
  echo "     Nothing new can take that name, so this run cannot start. Stop it and re-run:" >&2
  echo "       docker rm -f $name" >&2
  echo "     A run that ends any way but SIGKILL now reaps its own container, so meeting" >&2
  echo "     this means something was killed outright -- or another checkout is using the" >&2
  echo "     name, which it should not be, because the name carries the checkout (#1335)." >&2
  return 1
}

# Start a container, guarded. Returns what the container returned, or 125 when the name was
# already taken -- Docker's own code for that, so a reader who knows it still recognises it.
od_run() {
  local name rc cli
  name="$(od_name "$1")"; shift
  od_claim "$name" || return 125
  od_install_traps
  OD_GUARDED="$OD_GUARDED $name"
  od_watchdog "$name"
  docker run --rm --name "$name" "$@" &
  cli=$!
  wait "$cli"
  rc=$?
  # Let this one go: the trap must not reap a name a later phase is about to take.
  local kept="" n
  for n in $OD_GUARDED; do [ "$n" = "$name" ] || kept="$kept $n"; done
  OD_GUARDED="$kept"
  local w="$OD_GUARD_DIR/$name.watch"
  if [ -f "$w" ]; then
    kill "$(cat "$w" 2> /dev/null)" > /dev/null 2>&1
    rm -f "$w" 2> /dev/null
  fi
  return $rc
}

# What run_all.sh sweeps with between testers, and the only place a leak is REPORTED rather
# than merely prevented. Prints one line per container it had to reap; prints nothing when
# there was nothing, which is the ordinary case.
od_sweep() {
  local n found=0
  for n in $(docker ps -a --filter "name=^od-$OD_TREE_TAG-" --format '{{.Names}}' 2> /dev/null); do
    echo "LEAKED $n -- reaping it; a harness died without stopping its own container"
    docker rm -f "$n" > /dev/null 2>&1
    found=1
  done
  return $found
}

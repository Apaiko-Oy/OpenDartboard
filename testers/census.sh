#!/bin/bash
# #1371: every tester program in this directory is reached by the command that runs the
# testers, or says in one line why it is kept unrun.
#
# #1335 made one command run every tester so a merge could be gated on it. What it could
# not make true is that the command names every tester there IS: six check programs written
# as the proof of slices that have since merged sat in this directory naming nothing and
# named by nothing, and an unregistered check cannot be told from a registered one by
# looking. That is this check. It is the census shape Turnaus uses for the same problem
# (RouteResponsesAreAssertedTest, HardcodedStringsTest): the list is asserted EMPTY, there
# is no grandfather list, and the only way out is a marker beside the file saying why.
#
#   testers/census.sh
#
# A program is reached if run_all.sh names it, or if anything run_all.sh reaches names it.
# That transitive step is the whole difficulty and a grep of run_all.sh alone gets it
# wrong in both directions: i1362_broken_ring_footage.cpp appears nowhere in run_all.sh
# and is compiled on every run by the phase script for 1321-reason, while i822_run.sh is
# named only by i892_run.sh, which nothing names either -- a dead chain that reads as
# coverage.
#
# A directory is not a program and is not in the census; its contents are, one by one,
# which is how a phase script under phases1247/ earns its place. The census asks about
# files on disk rather than files in git, so a check dropped in here and never committed
# is reported too.
#
# The marker is one comment line anywhere in the file, in whatever syntax that file
# comments in:
#
#   unrun-tester: needs a real Windows console; nothing here can run it.
#
# A marker on a program something DOES run is reported too. A rule whose exemptions are
# never revisited rots, and a stale marker is how it starts.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

T="$OD_TREE_ROOT/testers"
MARKER='unrun-tester:'
ROOT='run_all.sh'

# This file spells the marker out in its own prose, so a marker search finds one here that
# is documentation rather than an exemption. It is therefore not read for a marker at all --
# which leaves it an ordinary program that must be reached, so deleting its run_all.sh entry
# fails by name here. Nothing else is exempt from anything.
SELF="$(basename "${BASH_SOURCE[0]}")"

if [ ! -f "$T/$ROOT" ]; then
  echo "FAIL census: $T/$ROOT is not there, so nothing can be reached from it" >&2
  exit 2
fi

# ---- the population: every program under testers/, at any depth -------------------------
PROGS=()
while IFS= read -r p; do
  PROGS+=("$p")
done < <(cd "$T" && find . -type f \
  \( -name '*.sh' -o -name '*.cpp' -o -name '*.py' -o -name '*.ps1' -o -name '*.html' \) \
  -printf '%P\n' | sort)

if [ ${#PROGS[@]} -eq 0 ]; then
  echo "FAIL census: no tester programs found under $T, which cannot be right" >&2
  exit 2
fi

# ---- what the root reaches, transitively ------------------------------------------------
REACH=" $ROOT "
changed=1
while [ "$changed" = 1 ]; do
  changed=0
  for f in "${PROGS[@]}"; do
    case "$REACH" in *" $f "*) continue ;; esac
    base="${f##*/}"
    for r in $REACH; do
      if grep -qF -e "$base" "$T/$r" 2>/dev/null; then
        REACH="$REACH$f "
        changed=1
        break
      fi
    done
  done
done

# ---- the three answers ------------------------------------------------------------------
UNNAMED=()
MARKED=()
STALE=()
for f in "${PROGS[@]}"; do
  reached=0
  case "$REACH" in *" $f "*) reached=1 ;; esac
  marked=0
  reason=""
  if [ "$f" != "$SELF" ] && reason="$(grep -m1 -F -e "$MARKER" "$T/$f" 2>/dev/null)"; then
    reason="${reason#*"$MARKER"}"
    # a marker with nothing after it is not a reason
    if [ -n "$(echo "$reason" | tr -d '[:space:]')" ]; then
      marked=1
    fi
  fi
  if [ "$reached" = 1 ]; then
    if [ "$marked" = 1 ]; then
      STALE+=("$f")
    fi
  elif [ "$marked" = 1 ]; then
    MARKED+=("$f |$reason")
  else
    UNNAMED+=("$f")
  fi
done

echo "census: ${#PROGS[@]} tester programs under testers/"
echo "        $(( ${#PROGS[@]} - ${#UNNAMED[@]} - ${#MARKED[@]} )) reached from $ROOT"
echo "        ${#MARKED[@]} kept unrun, each saying why:"
for m in "${MARKED[@]+"${MARKED[@]}"}"; do
  printf '          %s\n' "$m"
done

RC=0
for f in "${UNNAMED[@]+"${UNNAMED[@]}"}"; do
  echo "FAIL census: testers/$f is run by nothing and says nothing about why."
  echo "             Name it from run_all.sh or from a script run_all.sh reaches, delete it,"
  echo "             or write '$MARKER <why>' in it as a comment."
  RC=1
done
for f in "${STALE[@]+"${STALE[@]}"}"; do
  echo "FAIL census: testers/$f carries '$MARKER' but something does run it."
  echo "             The marker is stale: take it out."
  RC=1
done

if [ "$RC" = 0 ]; then
  echo "every tester program is run by something, or says why not"
fi
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC

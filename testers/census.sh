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
# ---- what NAMES means, and why it is a question about syntax (#1430) --------------------
#
# Until #1430 a name was looked for with `grep -qF` over the whole file, so a filename
# written in a SENTENCE counted as a call to it. The rule and the files it misjudges had
# never met on one branch -- this check is #1371's, on issue-1371; the two testers it
# accused are #1282's and #1306's -- so it took the whole-tree integration of nineteen
# branches to show it, and every one of those branches was green alone.
#
# It accused two markers that are true. i1282_run.sh says in prose that the Windows half
# of its criterion "is testers/i1282_windows.sh, which needs a Windows toolchain and is
# run by hand"; i1306_fixtures.py says in its module docstring that "testers/
# i1306_windows.sh gets fixtures around archives PowerShell's Compress-Archive really
# produced". Nothing runs either program. The third victim had already been worked around
# instead of reported: i1334_run.sh writes its sibling's name as `i1334_units` with the
# extension left off, and says in the same breath that it is left off to keep this check
# quiet. A checker that edits the prose around it is the wrong way round.
#
# So the rule is one line:
#
#   A FILE NAMES A TESTER WHEN THE NAME IS IN ITS CODE. A NAME IN A COMMENT IS PROSE.
#
# which restores the symmetry the old rule had collapsed: the marker IS a comment and is
# read only out of comments; a call is code and is read only out of code. Prose can no
# longer speak for either. Comments are stripped per language before the search -- `#`
# and a Python docstring, `//` and `/* */`, `<# #>`, `<!-- -->` -- by od_code below, which
# blanks them in place so nothing else about the file moves. It costs 2.5 s: this check
# was 18.4 s over 144 files and is 20.9 s, against run_all.sh's 1200 s budget for it.
#
# Three things that decision does, two of them on purpose and one of them a price:
#
#   A name held in a variable or assembled from parts STILL COUNTS, which is the half a
#   repair could most easily lose. `SCRIPT="${1:-$OD_TREE_ROOT/testers/i1282_inside.sh}"`
#   is code and is read as code, and so is a name run_all.sh passes to a harness as an
#   argument. Nothing here tries to decide whether the code really REACHES the name:
#   "a filename in a position where a command is built" is not a question a lexer can
#   answer, and a check that guessed would be wrong in the silent direction.
#
#   A COMMENTED-OUT INVOCATION does not count, and now it does not count for a reason.
#   The old rule got this right by accident -- it could not see `bash` either -- and the
#   obvious repair, looking for the name after a `bash`/`python3`/`g++`, would have got it
#   WRONG: `# bash "$T/i1234_run.sh"` is an invocation by that test and a comment by this
#   one. Stripping comments first is what makes the accident deliberate.
#
#   An `echo` that names a tester without running it does count, and that is the price of
#   a lexical rule. It is the safe direction and it is worth saying which direction that
#   is. Reading prose as a call is the SILENT error: a program nothing runs reads as
#   covered, which is #1371's whole subject, and it only ever makes a noise when the
#   program also carries a marker -- which is how this was found at all. Missing a real
#   call is the LOUD error: the program is reported by name as run by nothing, and the
#   answer is one line of run_all.sh. On this tree there is no `echo` site: the reach set
#   moved in one direction only.
#
# ---- what it moved, measured on detector-integration-w128 -------------------------------
#
# 139 of 144 reached before, 132 after. Nothing entered the reach set and SEVEN left it,
# and only two of them were the defect. The other five were the defect hiding a finding:
# each is a program run by nothing, reading as covered because some reachable file wrote
# its name in a sentence, which is the exact thing #1371 was built to report.
#
#   i1282_windows.sh, i1306_windows.sh   the two true markers this check was refusing.
#   i1303_windows.sh                     a hand-run Windows harness whose own prose says
#                                        "It is NOT part of testers/run_all.sh, and
#                                        deliberately so" -- and which #1306's marker
#                                        already called "#1303's pair". It now says it in
#                                        the marker's syntax instead of only in English.
#   i1303_console.ps1, i1303_detached.cpp,
#   i1306_windows_check.cpp              the MSVC and PowerShell halves those two hand-run
#                                        harnesses stage and compile. They were reached
#                                        only THROUGH a file that was itself reached by
#                                        prose. A marker is per file here, deliberately:
#                                        letting reachability flow out of a marked program
#                                        would make one marker a hatch for everything it
#                                        names, which is the allow-list #1371 removed.
#   i1274_control.sh                     i1249_control.sh with /run1249 changed to
#                                        /run1274, byte for byte otherwise, and run_all.sh
#                                        already runs that command as 1249-control.
#
# All five were given markers stating that, rather than an exemption stating nothing.
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

# ---- the code of a file, with its comments blanked out (#1430) ---------------------------
#
# One pass per file, written out beside the census's own run so the fixpoint below greps a
# stripped copy rather than re-stripping 144 files on every sweep. Comments are blanked and
# nothing is deleted: a line keeps its number and, where it can, its columns, so anything
# that ever wants to report a site by line still can.
#
# The languages are the five this directory writes testers in, and each is lexed rather
# than pattern-matched, because the patterns are the trap. `${f##*/}` and `$#` are not
# comments, `"http://x"` is not one either, and `# don't` must not open a quote. So each
# scanner tracks the string it is inside and only then asks whether a delimiter starts a
# comment. A Python docstring is read as a comment ONLY where it stands as a statement of
# its own, which is what i1306_fixtures.py's is; a triple-quoted string assigned to
# something is data a program may well run, and stays.
#
# An unknown extension is lexed as a shell script. Nothing reaches that today -- the
# population below is five extensions and this handles all five -- and `#` is the comment
# most of them would use.
OD_STRIP_AWK='
BEGIN { BLOCK = 0; DELIM = "" }

function sh_line(l,   i, c, p, q, out) {
  out = ""; q = ""
  for (i = 1; i <= length(l); i++) {
    c = substr(l, i, 1)
    if (q != "") { out = out c; if (c == q) { q = "" } ; continue }
    if (c == "'"'"'" || c == "\"") { q = c; out = out c; continue }
    if (c == "#") {
      p = (i == 1) ? "" : substr(l, i - 1, 1)
      if (i == 1 || p == " " || p == "\t" || p == ";" || p == "(" || p == "&" || p == "|") { return out }
    }
    out = out c
  }
  return out
}

function cpp_line(l,   i, c, n, q, out) {
  out = ""; q = ""; i = 1
  while (i <= length(l)) {
    c = substr(l, i, 1); n = substr(l, i + 1, 1)
    if (BLOCK) {
      if (c == "*" && n == "/") { BLOCK = 0; i += 2; continue }
      i++; continue
    }
    if (q != "") {
      out = out c
      if (c == "\\") { out = out n; i += 2; continue }
      if (c == q) { q = "" }
      i++; continue
    }
    if (c == "\"" || c == "'"'"'") { q = c; out = out c; i++; continue }
    if (c == "/" && n == "/") { return out }
    if (c == "/" && n == "*") { BLOCK = 1; i += 2; continue }
    out = out c; i++
  }
  return out
}

function html_line(l,   a, b, out) {
  out = ""
  while (length(l) > 0) {
    if (BLOCK) {
      b = index(l, "-->")
      if (b == 0) { return out }
      l = substr(l, b + 3); BLOCK = 0; continue
    }
    a = index(l, "<!--")
    if (a == 0) { return out l }
    out = out substr(l, 1, a - 1)
    l = substr(l, a + 4); BLOCK = 1
  }
  return out
}

function ps1_line(l,   a, b, out) {
  out = ""
  while (length(l) > 0) {
    if (BLOCK) {
      b = index(l, "#>")
      if (b == 0) { return out }
      l = substr(l, b + 2); BLOCK = 0; continue
    }
    a = index(l, "<#")
    if (a == 0) { return out sh_line(l) }
    out = out sh_line(substr(l, 1, a - 1))
    l = substr(l, a + 2); BLOCK = 1
  }
  return out
}

function py_line(l,   t, d, rest) {
  if (BLOCK) {
    d = index(l, DELIM)
    if (d == 0) { return "" }
    BLOCK = 0
    return py_line(substr(l, d + 3))
  }
  t = l
  sub(/^[ \t]*/, "", t)
  sub(/^[rRuUbBfF]+/, "", t)
  if (substr(t, 1, 3) == "\"\"\"" || substr(t, 1, 3) == "'"'"''"'"''"'"'") {
    DELIM = substr(t, 1, 3)
    rest = substr(t, 4)
    d = index(rest, DELIM)
    if (d == 0) { BLOCK = 1; return "" }
    return py_line(substr(rest, d + 3))
  }
  return sh_line(l)
}

{
  if      (mode == "cpp")  { print cpp_line($0) }
  else if (mode == "py")   { print py_line($0) }
  else if (mode == "ps1")  { print ps1_line($0) }
  else if (mode == "html") { print html_line($0) }
  else                     { print sh_line($0) }
}
'

od_code() {
  local mode
  case "$1" in
    *.cpp|*.c|*.h|*.hpp) mode=cpp ;;
    *.py)                mode=py ;;
    *.ps1|*.psm1)        mode=ps1 ;;
    *.html|*.htm)        mode=html ;;
    *)                   mode=sh ;;
  esac
  awk -v mode="$mode" "$OD_STRIP_AWK" "$1" 2>/dev/null
}

CODE="$(mktemp -d "${TMPDIR:-/tmp}/od-census.XXXXXX")" || {
  echo "FAIL census: no temporary directory to strip comments into" >&2
  exit 2
}
trap 'rm -rf "$CODE"' EXIT

for f in "${PROGS[@]}"; do
  d="$CODE/${f%/*}"
  [ "${f%/*}" = "$f" ] && d="$CODE"
  mkdir -p "$d"
  od_code "$T/$f" > "$CODE/$f"
done

# ---- what the root reaches, transitively ------------------------------------------------
REACH=" $ROOT "
changed=1
while [ "$changed" = 1 ]; do
  changed=0
  for f in "${PROGS[@]}"; do
    case "$REACH" in *" $f "*) continue ;; esac
    base="${f##*/}"
    for r in $REACH; do
      if grep -qF -e "$base" "$CODE/$r" 2>/dev/null; then
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

#!/bin/bash
# #1303: what runs inside the container for testers/i1303_check.sh. Read that file first.
set -u

SRC=/app/src
WORK=/tmp/i1303
rm -rf "$WORK" && mkdir -p "$WORK"

if [ "${MODE:-}" = "--mutate" ]; then
  rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
  python3 - <<'PY' || { echo MUTATION_FAILED; exit 2; }
path = "/tmp/mutated/launcher/launcher.hpp"
text = open(path).read()
gone = "        if (somebodyIsThere)\n"
assert gone in text, "the interactive-console gate is not where the mutation expects it"
open(path, "w").write(text.replace(gone, "        if (true)\n"))
print("MUTATED: the window is now held open for everybody, console or not")
PY
  SRC=/tmp/mutated
fi

g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" \
    /app/testers/i1303_launcher_check.cpp -o "$WORK/check" || { echo COMPILE_FAILED; exit 2; }
g++ -O1 -std=c++17 -Wall -Wextra \
    /app/testers/i1303_stub.cpp -o "$WORK/stub" || { echo COMPILE_FAILED; exit 2; }
g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" \
    "$SRC/launcher/main.cpp" -o "$WORK/launcher" || { echo COMPILE_FAILED; exit 2; }

FAILURES=0
note() { # note <passed 0|1> <what>
  if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi
}

echo "---- the decisions, against real child processes ----"
"$WORK/check" "$WORK/stub" "$WORK" < /dev/null
note $? "the in-process check"

echo
echo "---- the real launcher binary, started four ways ----"
export OD_DETECTOR="$WORK/stub"

# Each run: the stub writes a marker, so "it started the detector anyway" is a file on
# disk rather than an inference from an exit code.
run_one() { # run_one <label> <seconds> <stub-exit> -- the caller supplies stdin
  MARKER="$WORK/$1.argv"
  export STUB_ARGV_TO="$MARKER"
  export STUB_EXIT="$3"
  rm -f "$MARKER"
}

# -- redirected input: a scheduled task, a `< NUL` start --------------------------------
run_one redirected 10 0
BEGAN=$(date +%s.%N)
timeout 10 "$WORK/launcher" --cams 0,1,2 --allow-plaintext < /dev/null > "$WORK/redirected.out" 2>&1
RC=$?
ELAPSED=$(python3 -c "print('%.2f' % ($(date +%s.%N) - $BEGAN))")
echo "     redirected: rc=$RC in ${ELAPSED}s"
sed 's/^/     | /' "$WORK/redirected.out"
[ "$RC" -eq 0 ]; note $? "input redirected from /dev/null: the launcher exits 0 and does not hang"
[ -s "$WORK/redirected.argv" ]; note $? "  and it started the detector anyway"
grep -q -- "--allow-plaintext" "$WORK/redirected.argv"; note $? "  with the arguments the install carries"
grep -q "Ohjelma" "$WORK/redirected.out" && grep -q "program" "$WORK/redirected.out"
note $? "  and it said what happened, in Finnish and in English"
grep -q "Enter" "$WORK/redirected.out" && note 1 "  it asked nothing" || note 0 "  it asked nothing"

# -- an open pipe nobody writes to: a service. No EOF is ever coming. ---------------------
# This is the run the mutation breaks, and the reason the fifo exists: /dev/null answers
# end-of-input immediately, so a launcher that asks would still not hang on it.
run_one openpipe 10 4
rm -f "$WORK/fifo" && mkfifo "$WORK/fifo"
sleep 20 > "$WORK/fifo" &
HOLDER=$!
BEGAN=$(date +%s.%N)
timeout 10 "$WORK/launcher" --cams 0,1,2 < "$WORK/fifo" > "$WORK/openpipe.out" 2>&1
RC=$?
ELAPSED=$(python3 -c "print('%.2f' % ($(date +%s.%N) - $BEGAN))")
kill "$HOLDER" 2> /dev/null
wait "$HOLDER" 2> /dev/null
echo "     open pipe: rc=$RC in ${ELAPSED}s"
sed 's/^/     | /' "$WORK/openpipe.out"
[ "$RC" -ne 124 ]; note $? "input an open pipe nobody writes to: the launcher does not wait for ever"
[ "$RC" -eq 41 ]; note $? "  and a faulted detector still reports as one (exit code 41)"
[ -s "$WORK/openpipe.argv" ]; note $? "  and it started the detector anyway"

# -- no stdin at all --------------------------------------------------------------------
run_one nostdin 10 0
BEGAN=$(date +%s.%N)
timeout 10 bash -c "exec 0<&-; exec '$WORK/launcher' --cams 0,1,2" > "$WORK/nostdin.out" 2>&1
RC=$?
ELAPSED=$(python3 -c "print('%.2f' % ($(date +%s.%N) - $BEGAN))")
echo "     no stdin: rc=$RC in ${ELAPSED}s"
[ "$RC" -eq 0 ]; note $? "no input handle at all: the launcher exits 0 and does not hang"
[ -s "$WORK/nostdin.argv" ]; note $? "  and it started the detector anyway"

# -- THE CONTROL: a real terminal, where it must wait -------------------------------------
# script(1) puts the launcher on a pseudo-terminal, which is what isInteractiveConsole()
# answers true to on POSIX. Without this run, every "it did not pause" above is satisfied
# by a launcher that can never pause.
run_one terminal 10 0
rm -f "$WORK/fifo2" && mkfifo "$WORK/fifo2"
sleep 20 > "$WORK/fifo2" &
HOLDER=$!
BEGAN=$(date +%s.%N)
timeout 6 script -qec "$WORK/launcher --cams 0,1,2" /dev/null < "$WORK/fifo2" > "$WORK/terminal.out" 2>&1
RC=$?
ELAPSED=$(python3 -c "print('%.2f' % ($(date +%s.%N) - $BEGAN))")
kill "$HOLDER" 2> /dev/null
wait "$HOLDER" 2> /dev/null
echo "     terminal, nothing typed: rc=$RC in ${ELAPSED}s"
sed 's/^/     | /' "$WORK/terminal.out"
[ "$RC" -eq 124 ]; note $? "CONTROL: on a terminal with nothing typed, the launcher really does wait"
grep -q "Enter" "$WORK/terminal.out"; note $? "  and it said what to press"
[ -s "$WORK/terminal.argv" ]; note $? "  and it had started the detector first"

# -- and a terminal where Enter is pressed ------------------------------------------------
run_one typed 10 0
BEGAN=$(date +%s.%N)
printf '\n' | timeout 10 script -qec "$WORK/launcher --cams 0,1,2" /dev/null > "$WORK/typed.out" 2>&1
RC=$?
ELAPSED=$(python3 -c "print('%.2f' % ($(date +%s.%N) - $BEGAN))")
echo "     terminal, Enter pressed: rc=$RC in ${ELAPSED}s"
[ "$RC" -eq 0 ]; note $? "on a terminal where Enter is pressed, the window closes and the exit code is the detector's"

echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

#!/bin/bash
# #1531: what runs inside the container for testers/i1531_run.sh. Read that file first --
# it carries the argument, including why the mutation #1531 asked for turns nothing red.
set -u

SRC=/app/src
JSON=/app/build/_deps/nlohmann_json-src/include
CORPUS=/app/testers/corpus1531
CHECK=/app/testers/i1531_corpus_check.cpp
MODE="${MODE:-}"

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }

build_and_run() {
  # $1 source root, $2 where to put the output
  g++ -O2 -std=c++17 -Wall -Wextra -I "$1" -I "$JSON" "$CHECK" -o /tmp/i1531_check 2> "$2.compile" || {
    echo "COMPILE_FAILED"
    tail -5 "$2.compile"
    return 125
  }
  /tmp/i1531_check "$CORPUS" > "$2" 2>&1
  return $?
}

# The case names a run reported, in the order the corpus lists them.
reds() { sed -n 's/^FAIL \([a-z0-9-]*\) .*/\1/p' "$1" | tr '\n' ' ' | sed 's/ *$//'; }

# ---- 1. the tree as it is ----------------------------------------------------------------
if [ "$MODE" != "--plants-only" ]; then
  echo "---- the corpus, on the arm this container has ----"
  build_and_run "$SRC" /tmp/tree.out
  RC=$?
  cat /tmp/tree.out
  note $([ "$RC" -eq 0 ] && echo 0 || echo 1) "every case in the corpus answers the way the corpus pins it"

  # The corpus must contain what #1531 says it contains. A corpus somebody quietly shortened
  # is the failure this whole slice is about, one level up.
  for want in control-valid s-zero s-order r-zero r-order other-key der-leading-zero-pad \
              der-overlong-length signature-truncated payload-truncated payload-byte-moved \
              signature-over-another-payload anchor-not-on-curve; do
    grep -q "^$want	" "$CORPUS/cases.tsv"
    note $? "  the corpus holds the case $want"
  done
  grep -q "	accept	" "$CORPUS/cases.tsv"
  note $? "  and one of them must be ACCEPTED, which is what stops 'return false' passing this"
fi

# ---- 2. the plants -----------------------------------------------------------------------
#
# Each line is: plant | the cases it MUST turn red, exactly | what it is.
# An empty middle column is a measurement and not an omission: see i1531_run.sh.
#
# payload-truncated is deliberately absent from x-comparison's list, and the reason is a
# property rather than an accident: with the arm made to say yes, that manifest gets past
# the signature and is then refused by PayloadNotJson, because ten bytes off the end of a
# JSON document is not a JSON document. Which is manifest.hpp's stated order working --
# the payload's fields are read only after its signature -- seen from underneath.
PLANTS=(
  "s-range||#1531's own mutation: the hand-written arm skips its range check on s"
  "off-curve||the hand-written arm stops asking whether the anchor is a point on P-256"
  "canonical-der|der-leading-zero-pad der-overlong-length|derSignature goes back to taking two spellings of one signature"
  "x-comparison|other-key payload-byte-moved signature-over-another-payload|the hand-written arm stops comparing x to r and says yes"
  "verification-deleted|s-zero s-order r-zero r-order other-key der-leading-zero-pad der-overlong-length signature-truncated payload-byte-moved signature-over-another-payload anchor-not-on-curve|#1305's mutation: manifest.hpp stops refusing an unverified manifest at all"
)

echo
echo "---- the plants: which of these refusals the corpus can actually see ----"
for row in "${PLANTS[@]}"; do
  PLANT="${row%%|*}"
  rest="${row#*|}"
  WANT="${rest%%|*}"
  WHAT="${rest#*|}"

  rm -rf /tmp/planted && cp -r /app/src /tmp/planted
  python3 /app/testers/i1531_plant.py "$PLANT" /tmp/planted > /tmp/plant.log 2>&1
  if [ $? -ne 0 ]; then
    cat /tmp/plant.log
    note 1 "the plant $PLANT could be applied to this tree"
    continue
  fi

  build_and_run /tmp/planted "/tmp/$PLANT.out"
  RC=$?
  GOT="$(reds "/tmp/$PLANT.out")"
  if [ "$RC" -eq 125 ]; then
    cat "/tmp/$PLANT.out"
    note 1 "the plant $PLANT compiles"
    continue
  fi

  echo
  echo "  $PLANT -- $WHAT"
  if [ -z "$WANT" ]; then
    echo "     DEFENCE IN DEPTH: this plant is measured to change NO verdict, because the"
    echo "     check underneath it refuses the same cases anyway. The corpus cannot see it."
  fi
  echo "     wanted red: ${WANT:-<none>}"
  echo "     got    red: ${GOT:-<none>}"
  [ "$GOT" = "$WANT" ]
  note $? "  $PLANT turns exactly the cases it is declared to turn, and no others"
done

echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

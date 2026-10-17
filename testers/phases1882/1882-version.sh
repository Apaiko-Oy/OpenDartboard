set -u
# turnaus#1882: whether the board really SENDS which build it runs, on the wire.
#
# Turnaus has stored and drawn a beat's `version` since #1304 (`App\Autoscoring\
# StatedVersion`), and no board ever sent one, so every board on the Scorer Devices page
# read "Ei ole kertonut, mitä ohjelmaversiota se ajaa" -- a live, inert feature, #1474's
# shape. So every assertion here reads the BODY that arrived at turnaus_stub.py, which
# models StatedVersion to the comparison (max 64 characters after the trim, a 422 that
# costs the condition too), and compares it with what THIS binary's --version prints.
#
# TWO PHASES, because the version must not ride on the census:
#   NOCAMS  a board whose cameras never open -- it beats with no `cameras` at all;
#   WHOLE   the shipped mocks -- it beats with a census.
# Both must state the version on every beat. The falsifier is the pure half's mutations
# (i1882_check.sh) and, measured once by hand, this phase against a body without it.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
NOWHERE=/run1882/no-such-camera-1.mp4,/run1882/no-such-camera-2.mp4,/run1882/no-such-camera-3.mp4
STUB_URL=http://127.0.0.1:8899
TRANSCRIPT=/run1882/transcript.jsonl

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- build it (make build, or run testers/run_all.sh, which builds first)"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  exit 1
fi
if ! grep -q 'def stated_version' /app/testers/turnaus_stub.py; then
  echo "FAIL testers/turnaus_stub.py does not model StatedVersion, so a 200 here says nothing"
  exit 1
fi

# What --version prints, read off the binary itself: the criterion is THAT string.
PRINTED=$($BIN --version | sed -n 's/^OpenDartboard runtime version: //p' | head -1)
echo "--- $BIN --version prints: '$PRINTED' ---"
if [ -z "$PRINTED" ]; then
  echo "FAIL the binary printed no version, so there is nothing to compare the wire with"
  exit 1
fi

STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1882/stub.out 2> /run1882/stub.err &
STUB=$!
sleep 1
if ! kill -0 $STUB 2>/dev/null; then
  echo "FAIL the stub did not start"; sed 's/^/    /' /run1882/stub.err; exit 1
fi

mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }

# Every beat in a slice, one JSON object per line: event, keys, version as sent and as kept.
beats() {
  python3 -c "
import json, sys
start, end = int(sys.argv[2]), int(sys.argv[3])
for n, line in enumerate(open(sys.argv[1])):
    if n < start or n >= end:
        continue
    try: r = json.loads(line)
    except ValueError: continue
    if r.get('event') not in ('beat', 'beat_refused'):
        continue
    print(json.dumps({'event': r.get('event'), 'keys': ','.join(r.get('body_keys') or []),
                      'raw': r.get('version_raw'), 'kept': r.get('version'),
                      'why': r.get('version_why')}))
" "$TRANSCRIPT" "$1" "$2"
}

$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1882/pair.out 2> /run1882/pair.err
if [ $? -ne 0 ]; then
  echo "FAIL the board did not pair"; tail -10 /run1882/pair.err | sed 's/^/    /'
  kill $STUB 2>/dev/null; exit 1
fi

phase() {
  local tag="$1" cams="$2" settle="$3" from to pid
  echo "=== $tag ==="
  rm -rf /run1882/cache
  from=$(mark)
  $BIN --cams "$cams" --width 1280 --height 720 \
    --turnaus $STUB_URL --allow-plaintext > /run1882/$tag.out 2> /run1882/$tag.err &
  pid=$!
  echo "$tag reached its verdict after $(await /run1882/$tag.out 'Scorer running with|BOARD FAULTED' 150)s"
  sleep "$settle"
  kill -TERM $pid 2>/dev/null; wait $pid 2>/dev/null
  to=$(mark)
  beats "$from" "$to" > /run1882/$tag.beats
  echo "    beats in this slice: $(wc -l < /run1882/$tag.beats)"
  sed 's/^/      /' /run1882/$tag.beats | sort | uniq -c | head -6
}

phase nocams "$NOWHERE" 20
phase whole  "$MOCKS"   30

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# One reader, so the rule is written once: for a phase and a key set, the counts of beats,
# refusals, beats whose sent and kept version is not exactly PRINTED, and beats of that shape.
count() {
  python3 -c "
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
printed, shape = sys.argv[2], sys.argv[3]
refused = sum(r['event'] != 'beat' for r in rows)
wrong = sum(r['raw'] != printed or r['kept'] != printed for r in rows)
stray = sorted({r['keys'] for r in rows} - {'condition,version', 'cameras,condition,version'})
print(len(rows), refused, wrong, sum(r['keys'] == shape for r in rows), ' '.join(stray) or '-')
" /run1882/$1.beats "$PRINTED" "$2"
}

echo
for spec in "nocams condition,version" "whole cameras,condition,version"; do
  set -- $spec
  read -r n refused wrong shaped stray < <(count "$1" "$2")
  echo "--- $1: $n beats, $refused refused, $wrong not stating '$PRINTED', $shaped shaped {$2}, stray shapes: $stray"
  if [ "$n" -lt 1 ]; then
    say "FAIL $1: no beat reached the stub, so nothing in this phase is measured" no; continue
  fi
  if [ "$refused" = "0" ]; then say "OK   $1: every beat was accepted" ok
  else say "FAIL $1: $refused beat(s) REFUSED -- the version cost this board its condition" no; fi
  if [ "$wrong" = "0" ]; then say "OK   $1: every beat stated exactly what --version prints, and Turnaus kept it" ok
  else say "FAIL $1: $wrong beat(s) did not state '$PRINTED'" no; fi
  if [ "$shaped" -ge 1 ]; then say "OK   $1: it beat the {$2} body" ok
  else say "FAIL $1: no beat of the {$2} shape, so this phase is not the board it is meant to be" no; fi
  if [ "$stray" = "-" ]; then say "OK   $1: no beat carried a key beyond condition, version and cameras" ok
  else say "FAIL $1: beats carried $stray" no; fi
done

echo "CHECK_RC=$FAILED"
exit $FAILED

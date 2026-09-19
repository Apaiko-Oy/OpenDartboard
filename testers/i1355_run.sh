#!/bin/bash
# #1355's harness: compile testers/i1355_bounds_check.cpp against the detector's own
# dart_processing.cpp and drive processDartState directly, once per camera count.
#
#   testers/i1355_run.sh [counts...]     default: 2 3 4
#
# One process per count, because processDartState's state is static and its `initialized`
# flag is set once for the life of a program. Three is the control -- the only count the
# shipped code could ever have been right for -- and two and four are the question.
# Both halves of the proof run here. The fourth camera's reconciliation is asserted as a
# STATE, because an out-of-bounds read is undefined and a test that only watches for a
# crash watches for nothing; and the whole thing is then run again under
# AddressSanitizer, which says the same thing in the language a bound is written in.
# #1341 has `1317-asan` hanging -- that builds the WHOLE detector under the sanitizer;
# this is one translation unit and a main, and it links in seconds.
#
# Measured on the unfixed tree, four cameras, ASAN:
#   heap-buffer-overflow, READ of size 4, dart_processing.cpp:617 (the candidate-state
#   ladder), 0 bytes to the right of a 12-byte region allocated by the brace-initialised
#   `previous_states` at dart_processing.cpp:82.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
COUNTS=("$@")
[ ${#COUNTS[@]} -eq 0 ] && COUNTS=(2 3 4)

# Reaped by name before it is started: an interrupted run leaves the container alive and
# --rm never fires, and the next run then dies on the name rather than on anything it
# measures. By the exact name, never by pattern.
docker rm -f "$(od_name i1355-bounds)" > /dev/null 2>&1

docker run --rm --name "$(od_name i1355-bounds)" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
    set -u
    g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -o /tmp/bounds_check \
      /app/testers/i1355_bounds_check.cpp /app/src/detector/geometry/detection/dart_processing.cpp \
      $(pkg-config --cflags --libs opencv4) || exit 2
    g++ -std=c++17 -O1 -g -fsanitize=address -I /app/src -I /app/src/utils -o /tmp/bounds_asan \
      /app/testers/i1355_bounds_check.cpp /app/src/detector/geometry/detection/dart_processing.cpp \
      $(pkg-config --cflags --libs opencv4) || exit 2
    rc=0
    for n in '"${COUNTS[*]}"'; do
      /tmp/bounds_check "$n" || rc=1
      out=$(ASAN_OPTIONS=detect_leaks=0 /tmp/bounds_asan "$n" 2>&1) || rc=1
      if echo "$out" | grep -q "AddressSanitizer"; then
        echo "$out" | grep -A4 "ERROR: AddressSanitizer"
        echo "FAIL $n cameras: AddressSanitizer found an out-of-bounds access"
        rc=1
      else
        echo "OK   $n cameras: no out-of-bounds access under AddressSanitizer"
      fi
    done
    exit $rc'

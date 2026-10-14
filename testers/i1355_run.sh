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

# #1688: the whole harness runs in $OD_ASAN_IMAGE (testers/asan.dockerfile, gcc 14.2),
# not $OD_IMAGE. gcc 10.2.1's libasan there sometimes never reaches main on this kernel
# (6.18, WSL2, vm.mmap_rnd_bits=32): it loops on `AddressSanitizer:DEADLYSIGNAL` for ever,
# about 2 MB of it a second. The cause, measured: that libasan maps its allocator MAP_FIXED
# at 0x600000000000-0x640000000000, and with 32 bits of mmap randomness a PIE program lands
# inside that window about one start in four, so the allocator maps over the program's
# own text and every instruction run there faults again. Measured: an empty main hung 9 of
# 40 there and 0 of 40 in the new image; this tester, through the old image, FAILed 3 of
# 5 runs on 4 hangs in 15 sanitizer starts, and through the new one PASSed 20 of 20 (60
# starts, no hang; load 1.3-6.6, 46-98 s a run; 2026-10-14). testers/asan.dockerfile has
# the rest. The
# container's sandbox is untouched (no seccomp change, no `setarch -R`).
#
# Every sanitizer run stays BOUNDED all the same (cf417e4), and a run that meets its bound
# is a FAIL with its reason in it: the bound is the backstop if a libasan ever loops again.
# Before the bound the hang wedged run_all.sh -- one run went 12 minutes and grew the
# capturing shell to 2 GB, because the output was captured into a variable. It now goes to
# a file. A hang is not a pass and is not retried: it is reported by name, and it costs
# this tester its verdict.
# OD_ASAN_BOUND_S is the bound, 30 s by default: eighty times a finished run, so a slow box
# cannot read as a hang, and 60 MB of DEADLYSIGNAL at worst in the container's /tmp.
ASAN_BOUND_S="${OD_ASAN_BOUND_S:-30}"
od_need_asan_image || exit 2

docker run --rm --name "$(od_name i1355-bounds)" --cpus=2 --network none -e HOME=/root \
  -e ASAN_BOUND_S="$ASAN_BOUND_S" \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_ASAN_IMAGE" bash -c '
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
      ASAN_OPTIONS=detect_leaks=0 timeout -k 5 "$ASAN_BOUND_S" /tmp/bounds_asan "$n" \
        > /tmp/asan_$n.out 2>&1
      arc=$?
      if [ $arc -eq 124 ] || [ $arc -eq 137 ]; then
        deadly=$(grep -c "AddressSanitizer:DEADLYSIGNAL" /tmp/asan_$n.out)
        if [ "$deadly" -gt 0 ]; then why="libasan DEADLYSIGNAL loop, #1688"
        else why="no DEADLYSIGNAL in its output, so not the #1688 loop"; fi
        echo "FAIL $n cameras: ASAN run hung: $why (no answer in ${ASAN_BOUND_S}s, rc=$arc, $deadly DEADLYSIGNAL lines; not a finding about the bounds, and not retried)"
        rc=1
      elif grep -q "ERROR: AddressSanitizer" /tmp/asan_$n.out; then
        grep -A4 "ERROR: AddressSanitizer" /tmp/asan_$n.out
        echo "FAIL $n cameras: AddressSanitizer found an out-of-bounds access"
        rc=1
      elif grep -q "AddressSanitizer" /tmp/asan_$n.out; then
        head -5 /tmp/asan_$n.out
        echo "FAIL $n cameras: AddressSanitizer stopped the run (rc=$arc) without a report"
        rc=1
      elif [ $arc -ne 0 ]; then
        tail -5 /tmp/asan_$n.out
        echo "FAIL $n cameras: the sanitizer build exited rc=$arc"
        rc=1
      else
        echo "OK   $n cameras: no out-of-bounds access under AddressSanitizer"
      fi
    done
    exit $rc'

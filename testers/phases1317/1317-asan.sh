set -u
# #1317 under AddressSanitizer, for the criterion #845 was held to as well.
#
# The bug this issue is about is a read past the end of a vector, so the first question is
# whether the fixed build is clean where the old one was not, and the second is whether
# anything else was standing behind it -- ASan stops at the first hard error, so a fix can
# uncover a finding that was always there.
#
# Two inputs, because one of them is the reason #845's ASan run did not find this: a
# calibration over the shipped mocks (the control, and what #845 measured) and one over an
# input whose wire stage comes up short.
#
# The sanitizer build is put in the run directory rather than in the tree: `build-asan`
# beside `build` is not in .gitignore, and a sanitizer build is run output. The two
# FETCHCONTENT_SOURCE_DIR pins reuse what `build/_deps` already cloned, so this needs no
# network.

BUILD=/run1317/build-asan
FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT -fsanitize=address -fno-omit-frame-pointer -g -O1"
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
# Leaks are off: OpenCV, httplib and the ncnn-era statics leak by design at exit and this
# issue is about a bounds read. `abort_on_error=1` and a `log_path` are not decoration:
# with abort off and no log path, a run that is ended by SIGTERM while the board sits in
# the fault vigil filled 161 MB of stderr with bare `AddressSanitizer:DEADLYSIGNAL` and
# flushed no stdout at all -- a report about the harness, not about the program. Reports
# go to their own files, so the run's own log stays readable either way.
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:halt_on_error=1:log_path=/run1317/asanrep

echo "--- configure and build the sanitizer binary ---"
cmake -S /app -B $BUILD -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="$FLAGS" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DAPP_VERSION=0.0.0-dev \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/app/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/app/build/_deps/httplib-src > /run1317/asan_cmake.log 2>&1 || {
  tail -20 /run1317/asan_cmake.log; exit 1; }
cmake --build $BUILD -- -j4 --no-print-directory > /run1317/asan_build.log 2>&1 || {
  tail -30 /run1317/asan_build.log; exit 1; }
echo "ASAN_BUILD_OK"

echo "--- cut the same partial-detection input 1317-partial.sh uses ---"
START="${START:-6}"
g++ -std=c++17 -O1 -o /run1317/partial /app/testers/i1317_partial_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
for i in 1 2 3; do
  /run1317/partial /app/mocks/rig-20260918/cam_$i.mp4 /run1317/partial_$i.avi "$START" 0 0 300 || exit 1
done
PARTIAL=/run1317/partial_1.avi,/run1317/partial_2.avi,/run1317/partial_3.avi

echo "--- ASan over the shipped mocks ---"
OD_MAX_CYCLES=20 $BUILD/opendartboard --cams $MOCKS \
  --width 1280 --height 720 > /run1317/asan_control.out 2> /run1317/asan_control.err
echo "ASAN_CONTROL_RC=$?"

echo "--- ASan over the partial-detection input ---"
$BUILD/opendartboard --cams $PARTIAL \
  --width 1280 --height 720 > /run1317/asan_partial.out 2> /run1317/asan_partial.err &
P=$!
sleep 40
kill -TERM $P 2>/dev/null
wait $P 2>/dev/null
echo "ASAN_PARTIAL_RC=$?"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

for name in control partial; do
  echo "=== AddressSanitizer over the $name input ==="
  grep -E 'ERROR: AddressSanitizer|SUMMARY: AddressSanitizer|runtime error' \
    /run1317/asan_$name.out /run1317/asan_$name.err /run1317/asanrep.* 2>/dev/null || true
  N=$(cat /run1317/asan_$name.out /run1317/asan_$name.err /run1317/asanrep.* 2>/dev/null | grep -cE 'ERROR: AddressSanitizer' || true)
  if [ "$N" = "0" ]; then say "OK   no AddressSanitizer finding over the $name input" ok
  else say "FAIL $N AddressSanitizer findings over the $name input" no; fi
done

echo "=== the wire counts the sanitizer run measured ==="
sed 's/\x1b\[[0-9;]*m//g' /run1317/asan_partial.out | grep -E 'did not calibrate: the wire stage' | sort -u || true
sed 's/\x1b\[[0-9;]*m//g' /run1317/asan_control.out | grep -E 'Initial calibration|CAMERAS:' || true

echo "--- and the SAME sanitizer over the base commit, on the same input ---"
# The other half of the question #845 was asked. ASan stops at the first hard error, so
# "clean now" only means something beside what the old build reported on the same footage.
# The base tree is unpacked from git rather than checked out over the worktree, so nothing
# here can mutate the branch under test, and it reuses build/_deps so it needs no network.
# The harness unpacks it on the host: /app is a git worktree whose .git is a file pointing
# at a repository that is not mounted here, so `git -C /app archive` cannot work inside the
# container.
BASE_SRC=/run1317/base-src
BASE_BUILD=/run1317/build-asan-base
if [ ! -f $BASE_SRC/CMakeLists.txt ]; then
  echo "FAIL no base source at $BASE_SRC -- the harness unpacks it"
  exit 1
fi
cmake -S $BASE_SRC -B $BASE_BUILD -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="$FLAGS" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DAPP_VERSION=0.0.0-dev \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/app/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/app/build/_deps/httplib-src > /run1317/asan_base_cmake.log 2>&1 || {
  tail -20 /run1317/asan_base_cmake.log; exit 1; }
cmake --build $BASE_BUILD -- -j4 --no-print-directory > /run1317/asan_base_build.log 2>&1 || {
  tail -30 /run1317/asan_base_build.log; exit 1; }
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:halt_on_error=1:log_path=/run1317/baserep \
$BASE_BUILD/opendartboard --cams $PARTIAL --width 1280 --height 720 \
  > /run1317/asan_base_partial.out 2> /run1317/asan_base_partial.err &
B=$!
sleep 40
kill -TERM $B 2>/dev/null
wait $B 2>/dev/null
echo "ASAN_BASE_PARTIAL_RC=$?"
echo "=== AddressSanitizer over the base commit, same input ==="
grep -E 'ERROR: AddressSanitizer|SUMMARY: AddressSanitizer' \
  /run1317/asan_base_partial.out /run1317/asan_base_partial.err /run1317/baserep.* 2>/dev/null || echo "(none)"
sed 's/\x1b\[[0-9;]*m//g' /run1317/asan_base_partial.out | grep -E 'Found [0-9]+ wire boundaries|Initial calibration' | sort -u || true

echo "CHECK_RC=$FAILED"
exit $FAILED

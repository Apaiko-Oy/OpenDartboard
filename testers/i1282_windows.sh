#!/bin/bash
# unrun-tester: needs a Windows toolchain, a Windows disk and powershell.exe, and reaches
# a Windows listener over the WSL gateway. run_all.sh is a docker gate and has none of it;
# this is run by hand on the box scripts\build-windows.bat is written for.
#
# #1282: the score socket and the mock-input path on REAL Windows, driven from WSL.
#
# WHY THIS EXISTS. testers/i1282_run.sh measures the end of a clip on Linux, which is
# where every other harness here runs. Both of this issue's criteria are about Windows:
# the first is a socket lookup that answered "not found" there and nowhere else, and the
# second was observed on a Windows RELEASE build, whose capture loop has no sleep in it
# because DEBUG_VIA_VIDEO_INPUT is a dev define. Neither can be asked of a Linux binary.
# This is #1303's technique on a different target: stage the tree on the Windows disk,
# build it with the toolchain build-windows.bat names, and ask the real .exe.
#
#   testers/i1282_windows.sh [worktree]
#
# What it needs: /mnt/c writable, cmd.exe and powershell.exe on the PATH (WSL interop),
# Visual Studio 2022 BuildTools where scripts\build-windows.bat says or OD_VS set, CMake,
# and the static OpenCV tree at C:\opencv-static.
#
# HOW THE SUBSCRIBER CHECK REACHES A WINDOWS BOARD. #1188's check runs HERE, on Linux,
# unchanged -- it is the CLIENT that reads TCP_INFO and its sockets are this machine's.
# What #1282 added to it is four options with their old defaults: --host, --token-file
# (a Windows board keeps its token in AppData, not beside the run), --detector-arg and a
# --cams that leaves a drive letter alone. The board is started through WSL interop, so
# OD_MAX_CYCLES has to be named in WSLENV to cross into the Windows process at all.
#
# EVERY detector run below is bounded, by `timeout` or by a pid PowerShell holds. #895's
# fault vigil means a board that cannot calibrate never exits and OD_MAX_CYCLES does not
# bound it -- and "the footage ended" and "the board is stuck" are the two states this
# issue is about telling apart, so a harness that confuses them proves nothing.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"

VS="${OD_VS:-C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools}"
CMAKE="${OD_CMAKE:-C:\\Program Files\\CMake\\bin\\cmake.exe}"
NINJA="${OD_NINJA:-$VS\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe}"
OPENCV="${OD_OPENCV_STATIC:-C:\\opencv-static\\x64\\vc17\\staticlib}"

STAGE_UNIX="${OD_WIN_STAGE_UNIX:-/mnt/c/od-run/i1282}"
STAGE_WIN="${OD_WIN_STAGE:-C:\\od-run\\i1282}"
# Where the board is, seen from here. WSL2 in its default NAT mode reaches the Windows
# host on this side's default gateway; a mirrored-networking box would say 127.0.0.1.
HOST="${OD_WIN_HOST:-$(ip route | awk '/default/{print $3; exit}')}"
TOKEN="${OD_WIN_TOKEN:-$(ls -d /mnt/c/Users/*/AppData/Roaming/OpenDartboard 2>/dev/null | head -1)/score_token}"

for tool in cmd.exe powershell.exe python3; do
  command -v "$tool" > /dev/null || { echo "i1282_windows: $tool is not on this machine" >&2; exit 2; }
done

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }
win() { (cd /mnt/c && timeout "${1}" cmd.exe /d /c "$2" 2>&1); }   # cmd refuses a UNC cwd

echo "---- staging the tree at $STAGE_WIN ----"
rm -rf "$STAGE_UNIX"
mkdir -p "$STAGE_UNIX/tree/mocks" "$STAGE_UNIX/run"
cp "$TREE/CMakeLists.txt" "$STAGE_UNIX/tree/"
cp -r "$TREE/src" "$STAGE_UNIX/tree/"
cp -r "$TREE/models" "$STAGE_UNIX/tree/" 2> /dev/null
cp "$TREE"/mocks/cam_1.mp4 "$TREE"/mocks/cam_2.mp4 "$TREE"/mocks/cam_3.mp4 "$STAGE_UNIX/tree/mocks/"

cat > "$STAGE_UNIX/build.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
cd /d $STAGE_WIN\\tree
"$CMAKE" -S . -B build-win-static -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" ^
  -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="$OPENCV" ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DAPP_VERSION=1282.0.0-test || exit /b 1
"$CMAKE" --build build-win-static --parallel 3 || exit /b 1
copy /y build-win-static\\opendartboard.exe $STAGE_WIN\\ >nul || exit /b 1
echo BUILT
BAT

# The RELEASE build, deliberately: DEBUG_VIA_VIDEO_INPUT is what puts a 16.7 ms sleep in
# every capture cycle, and its absence is half of what #1282's second observation is.
echo "---- building (release, static OpenCV: what build-windows.bat calls 'static') ----"
BUILD=$(win 3000 "$STAGE_WIN\\build.bat")
echo "$BUILD" | tail -3
echo "$BUILD" | grep -q BUILT; note $? "the detector builds on Windows through the toolchain build-windows.bat drives"
[ -f "$STAGE_UNIX/opendartboard.exe" ]; note $? "  and the .exe is there"
[ -f "$STAGE_UNIX/opendartboard.exe" ] || { echo "nothing further can be measured"; exit 1; }

CAMS="$STAGE_WIN\\tree\\mocks\\cam_1.mp4,$STAGE_WIN\\tree\\mocks\\cam_2.mp4,$STAGE_WIN\\tree\\mocks\\cam_3.mp4"

# ---- criterion 2: the mock footage ends ---------------------------------------------
cat > "$STAGE_UNIX/eof.ps1" <<'PS1'
param([string]$Exe, [string]$WorkDir, [string]$Label, [int]$DeadlineSeconds, [string]$Cams)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$out = Join-Path $WorkDir "$Label.out"; $err = Join-Path $WorkDir "$Label.err"
$res = Join-Path $WorkDir "$Label.result.txt"
Remove-Item $out,$err,$res -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList @("--cams", $Cams, "--width", "1280", "--height", "720") `
      -WorkingDirectory $WorkDir -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow -PassThru
# Reading .Handle is what makes .ExitCode readable afterwards: Start-Process -PassThru
# hands back a Process the caller has not opened, and without this the field comes back
# empty on a process that really did exit with a code.
$null = $p.Handle
"PID=$($p.Id)" | Out-File -Encoding ascii $res
# The deadline is held against the pid, and the verdict says which of the two happened:
# a board that cannot calibrate never exits, so "it stopped" and "we stopped it" are not
# the same measurement and the tester must not read one as the other.
if ($p.WaitForExit($DeadlineSeconds * 1000)) {
  Add-Content $res "ENDED_BY=itself"; Add-Content $res "EXIT_CODE=$($p.ExitCode)"
} else {
  try { $p.Kill(); $p.WaitForExit(20000) } catch {}
  Add-Content $res "ENDED_BY=deadline"
}
Add-Content $res ("STDOUT_BYTES=" + (Get-Item $out).Length)
Add-Content $res ("STDOUT_LINES=" + ((Get-Content $out | Measure-Object -Line).Lines))
PS1

echo
echo "---- the three mock clips are played to their end (release build, no cycle budget) ----"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$STAGE_WIN\\eof.ps1" \
  -Exe "$STAGE_WIN\\opendartboard.exe" -WorkDir "$STAGE_WIN\\run\\eof" -Label eof \
  -DeadlineSeconds 900 -Cams "$CAMS" > /dev/null 2>&1
RES="$STAGE_UNIX/run/eof/eof.result.txt"
if [ ! -f "$RES" ]; then
  note 1 "the end-of-footage run wrote a result at all"
else
  sed -i 's/\r$//' "$RES"; sed 's/^/     | /' "$RES"
  OUT="$STAGE_UNIX/run/eof/eof.out"
  BYTES=$(sed -n 's/^STDOUT_BYTES=//p' "$RES")
  grep -q "ENDED_BY=itself" "$RES"; note $? "the run ended on its own rather than on the deadline"
  grep -q "EXIT_CODE=0" "$RES"; note $? "  and ended cleanly"
  [ "${BYTES:-0}" -lt 2000000 ]; note $? "  and wrote under 2 MB of stdout ($BYTES bytes; the run this issue was filed about wrote about 200 MB)"
  [ "$(grep -c 'END OF FOOTAGE cam=' "$OUT")" = 3 ]; note $? "each of the three clips said its own end, once"
  grep -q 'END OF FOOTAGE: every file source has reached its end' "$OUT"
  note $? "and the scorer said the run was over because of it"
  sed 's/\x1b\[[0-9;]*m//g' "$OUT" > "$OUT.plain"
  [ "$(grep -c 'Initializing 3 cameras' "$OUT.plain")" = 1 ]; note $? "the sources were opened once: the clips were not replayed"
  grep -q 'BOARD SIGHT LOST' "$OUT" && note 1 "the end of a clip was not reported as the board going blind" \
    || note 0 "the end of a clip was not reported as the board going blind"
fi

# ---- criterion 1: #1188's subscriber check, against the Windows board ---------------
echo
echo "---- #1188's subscriber check, run from here against the board on $HOST ----"
echo "     token $TOKEN"
SUBS="$STAGE_UNIX/run/subs"
rm -rf "$SUBS"; mkdir -p "$SUBS"
# 3000 rather than 1100: a release build has no per-cycle sleep, so 1100 cycles is about
# fifty seconds and the dead subscriber needs forty of them AFTER the first message.
WSLENV=OD_MAX_CYCLES timeout 1200 python3 "$TREE/tools/score_socket/check_subscribers.py" \
  --binary "$STAGE_UNIX/opendartboard.exe" --host "$HOST" --detector-arg=--listen \
  --token-file "$TOKEN" --cams "$CAMS" --cycles 3000 --workdir "$SUBS" \
  > "$SUBS/check.out" 2> "$SUBS/check.err"
sed -n '/^run 2/,$p' "$SUBS/check.out" | grep -E "^  (ok|FAIL)  (C |the log|the logged|the reason|A was)" | sed 's/^/     | /'

# What is asserted here is #1282's criterion and not the whole check. The reference
# stream is a DEV build's -- DEBUG_SEEK_VIDEO decides which frame the board calibrates on
# -- so a release .exe scores different darts and every reference check fails by
# construction, on this platform and on Linux alike. What must hold is the drop.
grep -qE "^  ok    C's connection was closed by the board within 40 s" "$SUBS/check.out"
note $? "a stalled subscriber is closed within the bound the board states (ping 30 s + pong 10 s)"
grep -qE "^  ok    the log says C was dropped, and why" "$SUBS/check.out"
note $? "  and the board's own log says it dropped it"
grep -qE "^  ok    the reason is the missing pong" "$SUBS/check.out"
note $? "  and the reason is the missing pong, not the write timeout"
grep -qE "^  ok    A was not dropped" "$SUBS/check.out"
note $? "  and the subscriber that was reading was not touched"
# The half that was Windows' alone: before #1282 this line carried the parenthesis, on
# every subscriber, because findSocketOf could not answer on this platform at all.
if grep -q "its socket was not found" "$SUBS/run2/detector-one-dead.out" 2> /dev/null; then
  note 1 "every subscriber's socket was found (no 'its socket was not found' in the log)"
else
  note 0 "every subscriber's socket was found (no 'its socket was not found' in the log)"
fi
grep -qE "^  ok    C subscribed" "$SUBS/check.out"
note $? "  and the census looked at something: C really did subscribe"

echo
echo "measured here: a stalled subscriber's drop on Windows, and the end of the mock footage on Windows."
echo "not measured here: the CALIBRATING rung (it is pushed to Turnaus, not printed, and this"
echo "                   release build calibrates all three cameras inside one heartbeat anyway)."
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

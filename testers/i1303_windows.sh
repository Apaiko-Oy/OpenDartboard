#!/bin/bash
# unrun-tester: needs a Windows toolchain, a Windows disk and powershell.exe, and this file
# has said exactly that in its own prose since it was written -- #1306's marker calls it
# "#1303's pair". run_all.sh is a docker gate and has none of the three; this is run by hand
# on the box scripts\build-windows.bat is written for. It was reported unrun for the first
# time by #1430: until then a sentence in i1303_check.sh naming this file counted as a call.
# #1303: the launcher on real Windows, driven from WSL.
#
# WHY THIS EXISTS AND WHAT IT IS NOT. testers/i1303_check.sh measures the launcher's
# decisions and its behaviour on Linux, which is where every other harness in this
# repository runs -- but the launcher is a Windows program and three of this slice's
# criteria are about Windows conditions Linux has no equivalent of: a double-click, a
# console input buffer, and a start with no console at all. This asks those questions of a
# real .exe on the real platform.
#
# It is NOT part of testers/run_all.sh, and deliberately so: run_all.sh is a docker gate
# and this needs a Windows toolchain, a Windows disk and powershell.exe. It is run by hand
# on a box that has them -- the same box scripts/build-windows.bat is written for -- and
# it says plainly at the end which of the criteria it measured.
#
#   testers/i1303_windows.sh [worktree]
#
# What it needs: /mnt/c writable, cmd.exe and powershell.exe on the PATH (WSL interop),
# Visual Studio 2022 BuildTools at the path scripts\build-windows.bat names or OD_VS set,
# CMake, and the static OpenCV tree at C:\opencv-static. The launcher itself needs none of
# OpenCV -- it links KERNEL32 and nothing else -- but CMake's top-level configure finds
# OpenCV for the detector target before it can build any target at all.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"

VS="${OD_VS:-C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools}"
CMAKE="${OD_CMAKE:-C:\\Program Files\\CMake\\bin\\cmake.exe}"
NINJA="${OD_NINJA:-$VS\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe}"

STAGE_UNIX="${OD_WIN_STAGE_UNIX:-/mnt/c/od-run/i1303}"
STAGE_WIN="${OD_WIN_STAGE:-C:\\od-run\\i1303}"

for tool in cmd.exe powershell.exe; do
  command -v "$tool" > /dev/null || { echo "i1303_windows: $tool is not on this machine; this harness needs WSL interop" >&2; exit 2; }
done

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }
# cmd.exe refuses a UNC working directory, so every call is made from a Windows-visible one.
win() { (cd /mnt/c && timeout "${1}" cmd.exe /d /c "$2" 2>&1); }

echo "---- staging the tree at $STAGE_WIN ----"
rm -rf "$STAGE_UNIX"
mkdir -p "$STAGE_UNIX/tree" "$STAGE_UNIX/run"
cp "$TREE/CMakeLists.txt" "$STAGE_UNIX/tree/"
cp -r "$TREE/src" "$STAGE_UNIX/tree/"
cp "$TREE/testers/i1303_stub.cpp" "$TREE/testers/i1303_detached.cpp" "$STAGE_UNIX/"
cp "$TREE/testers/i1303_console.ps1" "$STAGE_UNIX/"

cat > "$STAGE_UNIX/build.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
cd /d $STAGE_WIN
cl /nologo /std:c++17 /EHsc /MT /O1 /Fe:stub.exe i1303_stub.cpp >nul || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O1 /Fe:detached.exe i1303_detached.cpp >nul || exit /b 1
cd /d $STAGE_WIN\\tree
"$CMAKE" -S . -B build-win-static -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" ^
  -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\\opencv-static\\x64\\vc17\\staticlib" ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DAPP_VERSION=1303.0.0-test >nul || exit /b 1
"$CMAKE" --build build-win-static --target opendartboard-launcher || exit /b 1
copy /y build-win-static\\opendartboard-launcher.exe $STAGE_WIN\\ >nul || exit /b 1
echo BUILT
BAT

echo "---- building (the launcher comes out of its own CMake target, as build-windows.bat makes it) ----"
BUILD=$(win 1800 "$STAGE_WIN\\build.bat")
echo "$BUILD" | tail -5
echo "$BUILD" | grep -q BUILT; note $? "the launcher builds on Windows through the CMake target build-windows.bat drives"
[ -f "$STAGE_UNIX/opendartboard-launcher.exe" ]; note $? "  and the .exe is there"
if [ ! -f "$STAGE_UNIX/opendartboard-launcher.exe" ]; then echo "nothing further can be measured"; exit 1; fi

# ---- one file, in-box DLLs only: #1299's property, kept for the second executable --------
cat > "$STAGE_UNIX/dep.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
dumpbin /nologo /dependents $STAGE_WIN\\opendartboard-launcher.exe
BAT
DEP=$(win 300 "$STAGE_WIN\\dep.bat")
echo "$DEP" | grep -E '^\s+\S+\.dll' | sed 's/^/     | /'
echo "$DEP" | grep -qiE '^\s+(opencv|vcruntime|msvcp|msvcr|concrt|vcomp|ucrtbase|api-ms-win-crt)'
[ $? -ne 0 ]; note $? "the launcher imports no redistributable: it is one file beside the detector"
echo "$DEP" | grep -qiE '^\s+\S+\.dll'; note $? "  and the census looked at something (an empty one proves nothing)"

# ---- the console cases: does the window stay, and what does it exit with? -----------------
echo
echo "---- a real console: the detector ends the three ways ----"
console_case() { # console_case <label> <await> <expected-exit> [-StubExit n | -StubMode m | -Detector p]
  local label="$1" await="$2" expect="$3"; shift 3
  rm -f "$STAGE_UNIX/run/$label.result.txt"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$STAGE_WIN\\i1303_console.ps1" \
    -Exe "$STAGE_WIN\\opendartboard-launcher.exe" -WorkDir "$STAGE_WIN\\run" -Label "$label" \
    -Await "$await" -TypeEnter "$@" > /dev/null 2>&1
  local result="$STAGE_UNIX/run/$label.result.txt"
  if [ ! -f "$result" ]; then note 1 "$label: the driver wrote no result at all"; return; fi
  # PowerShell writes CRLF, and a pattern anchored at end of line never matches one.
  sed -i 's/\r$//' "$result"
  sed 's/^/     | /' "$result"
  grep -q "AWAIT_SEEN=1" "$result"; note $? "$label: the ending reached the screen"
  grep -q "STILL_RUNNING_AFTER_REPORT=1" "$result"; note $? "$label: THE WINDOW STAYED -- the launcher was still there once it had said so"
  grep -q "EXIT_CODE=$expect\$" "$result"; note $? "$label: and the launcher exited $expect"
}

console_case clean   "ended cleanly"        0  -Detector "$STAGE_WIN\\stub.exe" -StubExit 0
console_case faulted "Exit code 3"          41 -Detector "$STAGE_WIN\\stub.exe" -StubExit 3
console_case crashed "ended with a fault"   41 -Detector "$STAGE_WIN\\stub.exe" -StubMode crash
console_case killed  "was stopped"          42 -Detector "$STAGE_WIN\\stub.exe" -StubMode stopped
console_case missing "did not start"        40 -Detector "$STAGE_WIN\\no-such-detector.exe"

if [ -f "$STAGE_UNIX/run/crashed.screen.txt" ]; then
  sed 's/^/     | /' "$STAGE_UNIX/run/crashed.screen.txt"
  grep -q "0xC0000005" "$STAGE_UNIX/run/crashed.screen.txt"
  note $? "an access violation is named as a number a reader can quote"
  grep -qi "Access violation" "$STAGE_UNIX/run/crashed.screen.txt"
  note $? "  and as words, in English"
  grep -q "rikkomus" "$STAGE_UNIX/run/crashed.screen.txt"
  note $? "  and in Finnish, with its umlauts intact through the console code page"
fi

# ---- input redirected: a scheduled task, a `< NUL` start ----------------------------------
echo
echo "---- input redirected from NUL ----"
cat > "$STAGE_UNIX/redirected.bat" <<BAT
@echo off
cd /d $STAGE_WIN
set "OD_DETECTOR=$STAGE_WIN\\stub.exe"
set "STUB_ARGV_TO=$STAGE_WIN\\run\\redirected.argv.txt"
set "STUB_EXIT=3"
del run\\redirected.argv.txt 2>nul
opendartboard-launcher.exe --cams 0,1,2 --allow-plaintext < NUL > run\\redirected.out.txt 2>&1
echo LAUNCHER_RC=%ERRORLEVEL%
BAT
R=$(win 120 "$STAGE_WIN\\redirected.bat")
echo "$R" | sed 's/^/     | /'
sed 's/^/     | /' "$STAGE_UNIX/run/redirected.out.txt" 2> /dev/null
echo "$R" | grep -q "LAUNCHER_RC=41"; note $? "input redirected: the launcher does not hang, and exits 41 for a faulted detector"
[ -s "$STAGE_UNIX/run/redirected.argv.txt" ]; note $? "  and it started the detector anyway"
grep -q "Enter" "$STAGE_UNIX/run/redirected.out.txt" && note 1 "  and asked nothing" || note 0 "  and asked nothing"
grep -q "Ohjelma" "$STAGE_UNIX/run/redirected.out.txt" && grep -q "program" "$STAGE_UNIX/run/redirected.out.txt"
note $? "  and still said what happened, in both languages"

# ---- no console at all: a service, a scheduled task, a shortcut with no window ------------
echo
echo "---- no console at all (DETACHED_PROCESS) ----"
cat > "$STAGE_UNIX/detached.bat" <<BAT
@echo off
cd /d $STAGE_WIN
set "OD_DETECTOR=$STAGE_WIN\\stub.exe"
set "STUB_ARGV_TO=$STAGE_WIN\\run\\detached.argv.txt"
set "STUB_EXIT=0"
del run\\detached.argv.txt 2>nul
detached.exe $STAGE_WIN\\opendartboard-launcher.exe --cams 0,1,2
BAT
D=$(win 120 "$STAGE_WIN\\detached.bat")
echo "$D" | sed 's/^/     | /'
echo "$D" | grep -q "DETACHED_EXIT_CODE=0"; note $? "no console at all: the launcher exits 0 and does not wait for ever"
[ -s "$STAGE_UNIX/run/detached.argv.txt" ]; note $? "  and it started the detector anyway"

# ---- the arguments an install carries ------------------------------------------------------
# Measured without assuming anything about what cmd did to the line: the same command line
# is given to the stub DIRECTLY and through the launcher, and the two argv files must agree
# from the first argument on. Whatever cmd's own splitting produced, the launcher must not
# have changed it.
echo
echo "---- the arguments, through CreateProcess's one string ----"
cat > "$STAGE_UNIX/argv.bat" <<'BATEOF'
@echo off
cd /d __STAGE__
set "ARGS=--cams 0,1,2 --allow-plaintext --credentials "C:\Program Files\OpenDartboard\credentials.json" "a b" "say \"hello\"" --label "Pub room" C:\od\"
set "STUB_ARGV_TO=__STAGE__\run\argv-direct.txt"
del run\argv-direct.txt 2>nul
stub.exe %ARGS%
set "OD_DETECTOR=__STAGE__\stub.exe"
set "STUB_ARGV_TO=__STAGE__\run\argv-through.txt"
del run\argv-through.txt 2>nul
opendartboard-launcher.exe %ARGS% < NUL > nul 2>&1
echo ARGV_RC=%ERRORLEVEL%
BATEOF
python3 -c 'import sys; p=sys.argv[1]; s=open(p).read(); open(p,"w").write(s.replace("__STAGE__", sys.argv[2]))' \
  "$STAGE_UNIX/argv.bat" "$STAGE_WIN"
A=$(win 120 "$STAGE_WIN\\argv.bat")
echo "$A" | sed 's/^/     | /'
if [ -s "$STAGE_UNIX/run/argv-direct.txt" ] && [ -s "$STAGE_UNIX/run/argv-through.txt" ]; then
  tail -n +2 "$STAGE_UNIX/run/argv-direct.txt" > "$STAGE_UNIX/run/direct.args"
  tail -n +2 "$STAGE_UNIX/run/argv-through.txt" > "$STAGE_UNIX/run/through.args"
  echo "     the detector was handed:"; sed 's/^/     | /' "$STAGE_UNIX/run/through.args"
  cmp -s "$STAGE_UNIX/run/direct.args" "$STAGE_UNIX/run/through.args"
  note $? "every argument reached the detector unchanged, byte for byte, through the launcher"
  [ "$(wc -l < "$STAGE_UNIX/run/through.args")" -ge 9 ]
  note $? "  and the census looked at a real number of them"
  diff "$STAGE_UNIX/run/direct.args" "$STAGE_UNIX/run/through.args" | sed 's/^/     ! /'
else
  note 1 "the argument comparison ran at all (one of the two argv files is missing)"
fi

echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

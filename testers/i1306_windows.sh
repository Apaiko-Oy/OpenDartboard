#!/bin/bash
# #1306: the update, the swap and the rollback on real Windows, driven from WSL.
#
# WHY THIS EXISTS AND WHAT IT IS NOT. testers/i1306_check.sh measures the whole slice in
# the Linux container -- real sockets, real signatures, real zips, real child processes --
# and that is where this repository's harnesses run. Four things it cannot measure there,
# and each of them is a reason the slice exists:
#
#   a running .exe cannot be OVERWRITTEN, which is why the swap is a rename and why it
#   lives in the launcher (the fifth criterion); the signature is verified by CNG rather
#   than by p256_verify.hpp, which is the half ADR-0077 §2 actually chose; the archive is
#   read by %SystemRoot%\System32\tar.exe, on a zip Compress-Archive really wrote, which
#   is what release.yml ships; and the detector is started by CreateProcessA.
#
# It is NOT part of testers/run_all.sh, and deliberately so: run_all.sh is a docker gate
# and this needs a Windows toolchain, a Windows disk and powershell.exe. It is run by hand
# on a box that has them -- the same box scripts\build-windows.bat is written for.
# unrun-tester: needs a Windows toolchain, a Windows disk and WSL interop; run_all.sh is a
# docker gate and cannot provide any of the three. The same is true of #1303's pair.
#
#   testers/i1306_windows.sh [worktree]
#
# What it needs: /mnt/c writable, cmd.exe and powershell.exe on the PATH (WSL interop),
# Visual Studio 2022 BuildTools at the path scripts\build-windows.bat names or OD_VS set,
# CMake, and the static OpenCV tree at C:\opencv-static -- the launcher needs none of
# OpenCV, but CMake's top-level configure finds it for the detector target before it can
# build any target at all.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"

VS="${OD_VS:-C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools}"
CMAKE="${OD_CMAKE:-C:\\Program Files\\CMake\\bin\\cmake.exe}"
NINJA="${OD_NINJA:-$VS\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe}"

STAGE_UNIX="${OD_WIN_STAGE_UNIX:-/mnt/c/od-run/i1306}"
STAGE_WIN="${OD_WIN_STAGE:-C:\\od-run\\i1306}"

A=v1.0.0
B=v1.1.0
C=v1.2.0
BAD=v1.3.0

for tool in cmd.exe powershell.exe; do
  command -v "$tool" > /dev/null || { echo "i1306_windows: $tool is not on this machine; this harness needs WSL interop" >&2; exit 2; }
done

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }
# cmd.exe refuses a UNC working directory, so every call is made from a Windows-visible one.
win() { (cd /mnt/c && timeout "${1}" cmd.exe /d /c "$2" 2>&1); }

echo "---- staging the tree at $STAGE_WIN ----"
rm -rf "$STAGE_UNIX"
mkdir -p "$STAGE_UNIX/tree" "$STAGE_UNIX/stubs" "$STAGE_UNIX/fixtures/rel" "$STAGE_UNIX/work"
cp "$TREE/CMakeLists.txt" "$STAGE_UNIX/tree/"
cp -r "$TREE/src" "$STAGE_UNIX/tree/"
cp "$TREE/LICENSE" "$STAGE_UNIX/"
cp "$TREE/testers/i1306_stub.cpp" "$TREE/testers/i1306_windows_check.cpp" "$STAGE_UNIX/"

# ---- the four stubs, each carrying its own version the way the artefact does ------------
{
  echo "@echo off"
  echo "call \"$VS\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul || exit /b 1"
  echo "cd /d $STAGE_WIN"
  for V in "$A" "$B" "$C" "$BAD"; do
    echo "cl /nologo /std:c++17 /EHsc /MT /O1 /DAPP_VERSION#\\\"$V\\\" /Fe:stubs\\stub-$V.exe /Fo:stubs\\$V. i1306_stub.cpp >nul || exit /b 1"
  done
  echo "echo STUBS_BUILT"
} > "$STAGE_UNIX/stubs.bat"
S=$(win 600 "$STAGE_WIN\\stubs.bat")
echo "$S" | tail -3
echo "$S" | grep -q STUBS_BUILT; note $? "the four versioned stubs build on Windows"
[ "$(ls "$STAGE_UNIX/stubs"/stub-*.exe 2> /dev/null | wc -l)" -eq 4 ]; note $? "  and there are four of them"

# ---- the release zips, written by the command release.yml uses --------------------------
# Compress-Archive, over a staging directory holding exactly what release.yml packages.
# So what tar.exe is asked to read below is the archive format this project really ships,
# not one python happened to write.
cat > "$STAGE_UNIX/zips.ps1" <<'PS1'
param([string]$Stage, [string]$Versions)
foreach ($v in $Versions.Split(',')) {
  $pack = Join-Path $Stage "pack\$v"
  if (Test-Path $pack) { Remove-Item -Recurse -Force $pack }
  New-Item -ItemType Directory -Force -Path $pack | Out-Null
  Copy-Item (Join-Path $Stage "stubs\stub-$v.exe") (Join-Path $pack 'opendartboard.exe')
  Copy-Item (Join-Path $Stage 'LICENSE') $pack
  @(
    "OpenDartboard (Apaiko-Oy fork) - fixture for testers/i1306_windows.sh",
    "",
    "version:    $v",
    "This is not a release. It is a stub that prints its own version."
  ) | Set-Content (Join-Path $pack 'BUILD-INFO.txt')
  $zip = Join-Path $Stage "fixtures\rel\opendartboard-$v.zip"
  Compress-Archive -Path "$pack\*" -DestinationPath $zip -Force
  Write-Host "ZIPPED $v $((Get-Item $zip).Length)"
}
PS1
Z=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$STAGE_WIN\\zips.ps1" -Stage "$STAGE_WIN" -Versions "$A,$B,$C,$BAD" 2>&1)
echo "$Z" | sed 's/\r$//' | sed 's/^/     | /'
[ "$(ls "$STAGE_UNIX/fixtures/rel"/*.zip 2> /dev/null | wc -l)" -eq 4 ]
note $? "four release zips, written by Compress-Archive exactly as release.yml writes one"

# ---- signed over those bytes, by OpenSSL, in WSL ----------------------------------------
python3 "$TREE/testers/i1306_fixtures.py" --use-zips "$STAGE_UNIX/fixtures" "$STAGE_UNIX/stubs" "$A" "$B" "$C" "$BAD" \
  | sed 's/^/     | /'
[ -s "$STAGE_UNIX/fixtures/anchor.hex" ]; note $? "the manifests are signed by OpenSSL over the bytes of those very zips"

# ---- the launcher itself: it still builds, and it still imports only in-box DLLs --------
cat > "$STAGE_UNIX/build.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
cd /d $STAGE_WIN\\tree
"$CMAKE" -S . -B build-win-static -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" ^
  -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\\opencv-static\\x64\\vc17\\staticlib" ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DAPP_VERSION=$A >nul || exit /b 1
"$CMAKE" --build build-win-static --target opendartboard-launcher || exit /b 1
copy /y build-win-static\\opendartboard-launcher.exe $STAGE_WIN\\ >nul || exit /b 1
echo BUILT
BAT
echo "---- building the launcher, which now carries winhttp, bcrypt and nlohmann ----"
BUILD=$(win 1800 "$STAGE_WIN\\build.bat")
echo "$BUILD" | tail -5
echo "$BUILD" | grep -q BUILT; note $? "the launcher builds on Windows with #1306's dependencies"
if [ ! -f "$STAGE_UNIX/opendartboard-launcher.exe" ]; then echo "nothing further can be measured"; exit 1; fi

cat > "$STAGE_UNIX/dep.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
dumpbin /nologo /dependents $STAGE_WIN\\opendartboard-launcher.exe
BAT
DEP=$(win 300 "$STAGE_WIN\\dep.bat")
echo "$DEP" | grep -E '^\s+\S+\.dll' | sed 's/^/     | /'
echo "$DEP" | grep -qiE '^\s+(opencv|vcruntime|msvcp|msvcr|concrt|vcomp|ucrtbase|api-ms-win-crt)'
[ $? -ne 0 ]; note $? "#1299's property survives #1306: the launcher imports no redistributable"
echo "$DEP" | grep -qi 'winhttp.dll'; note $? "  it names winhttp.dll, which is in-box"
echo "$DEP" | grep -qi 'bcrypt.dll'; note $? "  and bcrypt.dll, which is how ADR-0077 §2 verifies a signature"

# ---- and the four questions only this platform can answer --------------------------------
DEPS="$STAGE_WIN\\tree\\build-win-static\\_deps"
cat > "$STAGE_UNIX/check.bat" <<BAT
@echo off
call "$VS\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1
cd /d $STAGE_WIN
cl /nologo /std:c++17 /EHsc /MT /O1 /utf-8 /FIod_platform_first.hpp ^
  /I $STAGE_WIN\\tree\\src /I $STAGE_WIN\\tree\\src\\utils ^
  /I $DEPS\\nlohmann_json-src\\include ^
  /DAPP_VERSION#\\"$A\\" /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
  /Fe:wincheck.exe /Fo:wincheck. i1306_windows_check.cpp ^
  /link bcrypt.lib winhttp.lib || exit /b 1
echo CHECK_BUILT
wincheck.exe $STAGE_WIN\\fixtures $STAGE_WIN\\work $STAGE_WIN\\stubs $A $B $C $BAD
echo WINCHECK_RC=%ERRORLEVEL%
BAT
echo
echo "---- the swap, the unpacker, CNG, and the whole chain on Windows ----"
R=$(win 900 "$STAGE_WIN\\check.bat")
echo "$R" | sed 's/\r$//'
echo "$R" | grep -q "CHECK_BUILT"; note $? "the Windows check compiles against the launcher's own headers"
echo "$R" | grep -q "WINCHECK_RC=0"; note $? "and every one of its questions is answered the right way"

echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]

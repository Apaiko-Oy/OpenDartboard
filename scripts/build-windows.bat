@echo off
REM Windows build (issues #805, #807). CMake stays; the toolchain changes.
REM
REM   build-windows.bat                   release, shared OpenCV  (needs opencv_world4140.dll beside it)
REM   build-windows.bat dev               dev,     shared OpenCV
REM   build-windows.bat static            release, static OpenCV  (standalone: one .exe, no DLL, no redist)
REM   build-windows.bat dev static        dev,     static OpenCV
REM   build-windows.bat static dev        the same; the two words are order-free
REM
REM DEBUG_SEEK_VIDEO seeks a file source past its first three seconds and every
REM measurement in this chain was taken with it. DEBUG_VIA_VIDEO_INPUT puts a
REM 16.7 ms sleep in every capture cycle and opens the debug MJPEG listeners.
REM They are named here rather than inherited, so a run can say which it had.
REM
REM "static" is the standalone artefact. It needs an OpenCV built with
REM BUILD_SHARED_LIBS=OFF and BUILD_WITH_STATIC_CRT=ON — see
REM scripts/build-opencv-windows-static.bat, which produces exactly that tree —
REM and it links the CRT statically here too. The two halves must agree: a /MT
REM OpenCV against an /MD program is a duplicate-symbol link or two heaps at
REM runtime, and nothing warns you at configure time.

setlocal enabledelayedexpansion
REM #1299: these three paths are one developer box's, and a GitHub Windows runner is
REM not one -- it has Visual Studio Enterprise where this says BuildTools. The defaults
REM are unchanged, so a hand build is exactly what it always was; a caller that has
REM looked the toolchain up says where it really is in OD_VS, OD_CMAKE and OD_NINJA.
REM NINJA is derived from VS, so the VS override has to land before it.
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if defined OD_VS set "VS=%OD_VS%"
if defined OD_CMAKE set "CMAKE=%OD_CMAKE%"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if defined OD_NINJA set "NINJA=%OD_NINJA%"

set OD_DEFS=
set OD_DEV=
set OD_STATIC=
set OD_VERSION=

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="dev"    ( set "OD_DEV=1"    & shift & goto parse )
if /I "%~1"=="static" ( set "OD_STATIC=1" & shift & goto parse )
set OD_VERSION=%~1
shift
goto parse
:parsed

REM #1299: a version nobody passed compiled in as an empty string, so --version on a
REM hand-built .exe printed the label and nothing after it. The Makefile answers the
REM same omission with 0.0.0-dev; this is that answer, in the same words.
if not defined OD_VERSION set "OD_VERSION=0.0.0-dev"

if defined OD_DEV set OD_DEFS=/DDEBUG_SEEK_VIDEO /DDEBUG_VIA_VIDEO_INPUT

REM #1408: the update trust anchors (ADR-0077 §4). They are read from the environment
REM rather than typed here, and they are never a source file: release.yml puts the public
REM half of the signing key into OD_UPDATE_ANCHOR_CURRENT from an Actions variable, so
REM rotating a key is a variable and not a commit. Unset is legal and is what a hand build
REM has -- the artefact then answers NoAnchor to every manifest, which is what every build
REM before #1408 did. CMakeLists.txt refuses a malformed one outright rather than dropping
REM it silently, so a truncated paste stops here instead of shipping.
set "OD_ANCHORS="
if defined OD_UPDATE_ANCHOR_CURRENT set "OD_ANCHORS=%OD_ANCHORS% -DOD_UPDATE_ANCHOR_CURRENT=%OD_UPDATE_ANCHOR_CURRENT%"
if defined OD_UPDATE_ANCHOR_NEXT set "OD_ANCHORS=%OD_ANCHORS% -DOD_UPDATE_ANCHOR_NEXT=%OD_UPDATE_ANCHOR_NEXT%"

if defined OD_STATIC (
  REM The static tree's OpenCVConfig.cmake lives beside the .lib files.
  set "OPENCV_DIR=C:\opencv-static\x64\vc17\staticlib"
  set "CRT=-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
  if defined OD_DEV ( set "BUILD_DIR=build-win-static-dev" ) else ( set "BUILD_DIR=build-win-static" )
) else (
  set "OPENCV_DIR=C:\opencv-dl\opencv\build"
  set "CRT="
  if defined OD_DEV ( set "BUILD_DIR=build-win-dev" ) else ( set "BUILD_DIR=build-win" )
)

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

"%CMAKE%" -S . -B !BUILD_DIR! -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DOpenCV_DIR="!OPENCV_DIR!" ^
  !CRT! ^
  -DCMAKE_CXX_FLAGS="%OD_DEFS%" ^
  -DAPP_VERSION=%OD_VERSION% !OD_ANCHORS! || exit /b 1

"%CMAKE%" --build !BUILD_DIR! --parallel 3 || exit /b 1

REM #1303: two executables now. `cmake --build` builds every target, so nothing
REM above changed; this says both names so a hand build can see both appear.
echo Built !BUILD_DIR!\opendartboard.exe and !BUILD_DIR!\opendartboard-launcher.exe with OD_DEFS="%OD_DEFS%" OpenCV="!OPENCV_DIR!"
if defined OD_UPDATE_ANCHOR_CURRENT ( echo   an update trust anchor is compiled in ) else ( echo   NO update trust anchor is compiled in: this build refuses every manifest )
endlocal

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
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=C:\Program Files\CMake\bin\cmake.exe
set NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

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

if defined OD_DEV set OD_DEFS=/DDEBUG_SEEK_VIDEO /DDEBUG_VIA_VIDEO_INPUT

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
  -DAPP_VERSION=%OD_VERSION% || exit /b 1

"%CMAKE%" --build !BUILD_DIR! --parallel 3 || exit /b 1

echo Built !BUILD_DIR!\opendartboard.exe with OD_DEFS="%OD_DEFS%" OpenCV="!OPENCV_DIR!"
endlocal

@echo off
REM Windows build (issue #805). CMake stays; the toolchain changes.
REM
REM   build-windows.bat            release: OD_DEFS empty, no listener on 8081 or 8088
REM   build-windows.bat dev        the two debug defines the Makefile's build-dev sets
REM
REM DEBUG_SEEK_VIDEO seeks a file source past its first three seconds and every
REM measurement in this chain was taken with it. DEBUG_VIA_VIDEO_INPUT puts a
REM 16.7 ms sleep in every capture cycle and opens the debug MJPEG listeners.
REM They are named here rather than inherited, so a run can say which it had.

setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=C:\Program Files\CMake\bin\cmake.exe
set NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
set OPENCV_DIR=C:\opencv-dl\opencv\build

if /I "%1"=="dev" (
  set OD_DEFS=/DDEBUG_SEEK_VIDEO /DDEBUG_VIA_VIDEO_INPUT
  set BUILD_DIR=build-win-dev
) else (
  set OD_DEFS=
  set BUILD_DIR=build-win
)

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

"%CMAKE%" -S . -B %BUILD_DIR% -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DOpenCV_DIR="%OPENCV_DIR%" ^
  -DCMAKE_CXX_FLAGS="%OD_DEFS%" ^
  -DAPP_VERSION=%2 || exit /b 1

"%CMAKE%" --build %BUILD_DIR% || exit /b 1

echo Built %BUILD_DIR%\opendartboard.exe with OD_DEFS="%OD_DEFS%"
endlocal

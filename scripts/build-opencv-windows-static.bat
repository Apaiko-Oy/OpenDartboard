@echo off
REM Static, CRT-static OpenCV for the standalone Windows build (#805).
REM   - BUILD_SHARED_LIBS=OFF     no opencv_world4140.dll
REM   - BUILD_WITH_STATIC_CRT=ON  /MT, no vcruntime140/msvcp140 redistributable
REM   - WITH_FFMPEG=OFF           opencv_videoio_ffmpeg4140_64.dll is loaded at
REM                               RUNTIME, so leaving it on defeats the exercise
REM   - WITH_MSMF=ON              one backend for cameras and for the mock files
REM   - WITH_DSHOW=OFF            the study's rejected alternative (capture 2.1)
REM BUILD_LIST is what the program's includes need: opencv.hpp + imgproc, plus
REM solvePnP (calib3d), whose own module deps are features2d and flann.
REM #1299: the toolchain paths are overridable for the same reason as in
REM build-windows.bat -- CI's Visual Studio is not at a developer box's path. SRC, BLD
REM and INST are deliberately NOT overridable: the release workflow puts the OpenCV
REM sources where this script already looks for them, so CI walks the maintainer's path
REM rather than a second one that could drift away from it.
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if defined OD_VS set "VS=%OD_VS%"
if defined OD_CMAKE set "CMAKE=%OD_CMAKE%"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if defined OD_NINJA set "NINJA=%OD_NINJA%"
set SRC=C:\opencv-dl\opencv\sources
set BLD=C:\od\opencv-static-build
set INST=C:\opencv-static

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

"%CMAKE%" -S "%SRC%" -B "%BLD%" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX="%INST%" ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_WITH_STATIC_CRT=ON ^
  -DBUILD_LIST=core,imgproc,imgcodecs,videoio,flann,features2d,calib3d ^
  -DWITH_FFMPEG=OFF ^
  -DWITH_MSMF=ON ^
  -DWITH_DSHOW=OFF ^
  -DWITH_OBSENSOR=OFF ^
  -DWITH_OPENCL=OFF ^
  -DWITH_OPENCL_D3D11_NV=OFF ^
  -DWITH_DIRECTX=OFF ^
  -DWITH_DIRECTML=OFF ^
  -DWITH_VA=OFF ^
  -DWITH_1394=OFF ^
  -DWITH_GSTREAMER=OFF ^
  -DWITH_ARAVIS=OFF ^
  -DWITH_ADE=OFF ^
  -DWITH_PROTOBUF=OFF ^
  -DWITH_QUIRC=OFF ^
  -DWITH_EIGEN=OFF ^
  -DWITH_ITT=OFF ^
  -DWITH_VTK=OFF ^
  -DWITH_JPEG=ON -DBUILD_JPEG=ON ^
  -DWITH_PNG=OFF -DWITH_TIFF=OFF -DWITH_WEBP=OFF -DWITH_JASPER=OFF ^
  -DWITH_OPENEXR=OFF -DWITH_OPENJPEG=OFF -DWITH_AVIF=OFF -DWITH_GDAL=OFF -DWITH_GDCM=OFF ^
  -DWITH_IMGCODEC_HDR=OFF -DWITH_IMGCODEC_SUNRASTER=OFF -DWITH_IMGCODEC_PXM=OFF -DWITH_IMGCODEC_PFM=OFF ^
  -DBUILD_ZLIB=ON ^
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF ^
  -DBUILD_opencv_apps=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF ^
  -DBUILD_JAVA=OFF -DBUILD_opencv_js=OFF -DBUILD_PACKAGE=OFF -DINSTALL_TESTS=OFF ^
  -DOPENCV_ENABLE_NONFREE=OFF ^
  -DBUILD_opencv_world=OFF || exit /b 1

"%CMAKE%" --build "%BLD%" --parallel 3 || exit /b 1
"%CMAKE%" --install "%BLD%" || exit /b 1
echo OPENCV_STATIC_BUILD_DONE
endlocal

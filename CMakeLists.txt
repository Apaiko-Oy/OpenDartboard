cmake_minimum_required(VERSION 3.17)
project(opendartboard LANGUAGES CXX)

# std::filesystem is used for directory creation; nothing set a standard before, so
# g++ 10 on bullseye compiled this project as gnu++14.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add FetchContent module for dependency management
include(FetchContent)

# Fetch nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

# Fetch cpp-httplib (MIT licensed)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.3
)



# Make dependencies available
FetchContent_MakeAvailable(nlohmann_json httplib)

# Tell CMake where the static ncnn & OpenCV we just built/installed live.
# /usr/local is a Linux answer; on Windows OpenCV is found through OpenCV_DIR or
# CMAKE_PREFIX_PATH given on the command line.
if(NOT WIN32)
    set(CMAKE_PREFIX_PATH "/usr/local" ${CMAKE_PREFIX_PATH})
endif()

# Find threads package for threading support
find_package(Threads REQUIRED)

# Use pkg-config to find OpenCV if standard find_package fails
find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    # pkg-config is the Linux fallback only. On Windows find_package is the whole
    # story and a failure here should say so rather than look for a tool that is
    # not part of the toolchain.
    if(WIN32)
        message(FATAL_ERROR "OpenCV not found. Pass -DOpenCV_DIR=<path containing OpenCVConfig.cmake>")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENCV REQUIRED opencv4)
    set(OpenCV_INCLUDE_DIRS ${OPENCV_INCLUDE_DIRS})
    set(OpenCV_LIBS ${OPENCV_LIBRARIES})
endif()

add_executable(opendartboard
    src/main.cpp
    src/scorer/scorer.cpp
    # Communication components
    src/communication/score_queue.cpp
    src/communication/websocket_service.cpp
    # #822: the outbound client -- pairing, and the push of what the scorer publishes.
    src/communication/turnaus_client.cpp
    # #1189: the QR code the setup view prints; vendored, MIT, src/third_party/qrcodegen/LICENSE
    src/third_party/qrcodegen/qrcodegen.cpp
    # src/detector/ai/yolo_detector.cpp // Uncomment when AI detector is ready
    src/detector/geometry/geometry_detector.cpp
    # calibration files
    src/detector/geometry/calibration/geometry_calibration.cpp
    src/detector/geometry/calibration/color_processing.cpp
    src/detector/geometry/calibration/contour_processing.cpp
    src/detector/geometry/calibration/roi_processing.cpp
    src/detector/geometry/calibration/ellipse_processing.cpp
    src/detector/geometry/calibration/bull_processing.cpp
    src/detector/geometry/calibration/mask_processing.cpp
    src/detector/geometry/calibration/wire_processing.cpp
    src/detector/geometry/calibration/perspective_processing.cpp
    src/detector/geometry/calibration/orientation_processing.cpp
    # detection files
    # src/detector/geometry/detection/geometry_detection.cpp
    src/detector/geometry/detection/motion_processing.cpp
    src/detector/geometry/detection/dart_processing.cpp
    src/detector/geometry/detection/score_processing.cpp
)

# Add include directories for easier includes
target_include_directories(opendartboard PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${NCNN_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${httplib_SOURCE_DIR}  # Add httplib include path (not libwebsockets)
)

# /usr/local/include and /usr/include/opencv4 were unconditional. They are dead on
# Windows, and find_package/pkg-config already answer for them on Linux.
if(NOT WIN32)
    target_include_directories(opendartboard PRIVATE /usr/local/include /usr/include/opencv4)
endif()

target_link_libraries(opendartboard
    PRIVATE
        ${OpenCV_LIBS}
        ${NCNN_LIBRARIES}
        nlohmann_json::nlohmann_json
        ${CMAKE_DL_LIBS}
        Threads::Threads  # Add pthread linking
)

# Define version for the project
target_compile_definitions(opendartboard
  PRIVATE
    APP_VERSION="${APP_VERSION}"
)

# ---------------------------------------------------------------- Windows
# Media Foundation is what CAP_MSMF and --autocams talk to, and ws2_32 is the
# debug MJPEG streamer's sockets. ${CMAKE_DL_LIBS} above is already the portable
# spelling and resolves to nothing here.
if(WIN32)
    # winhttp is #822's outbound transport. winhttp.dll is an in-box Windows DLL and it
    # brings Schannel with it, so TLS costs the standalone artefact nothing: no OpenSSL,
    # no extra file beside the .exe, no VC++ redistributable, and no /MT-versus-/MD
    # question about a third-party library, because there is no third-party library.
    target_link_libraries(opendartboard PRIVATE mfplat mf mfreadwrite mfuuid ole32 ws2_32 winhttp)
    target_compile_definitions(opendartboard PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

if(MSVC)
    # The sources are UTF-8 and MSVC assumes the active code page without this.
    # /FI puts the Windows SDK ahead of OpenCV in every translation unit; the
    # header says why that is load-bearing rather than tidy.
    # /EHsc is stated here rather than left to CMAKE_CXX_FLAGS, because the build
    # passes OD_DEFS through that variable and overwriting it drops CMake's own
    # default flags — of which /EHsc is the one whose absence turns every use of
    # std::ostream in a template into warning C4530 and then a failed build.
    target_compile_options(opendartboard PRIVATE /utf-8 /bigobj /EHsc /FIod_platform_first.hpp)
endif()

target_compile_features(opendartboard PRIVATE cxx_std_17)
# ---------------------------------------------------------- the launcher (#1303)
# ADR-0077 §1: a board's program is two files. This is the small one -- what a shortcut
# points at, what a tester double-clicks, and what stays on screen to say why the
# detector stopped. It links nothing at all: no OpenCV, no httplib, no json, nothing but
# what the CRT already brings. So it builds in seconds, it cannot be broken by a change
# to the detector's dependencies, and it keeps the one-file property #1299 asserts --
# there is no second DLL to put beside it.
#
# `cmake --build` builds every target, so scripts\build-windows.bat produces both
# executables with no change to how it is called.
#
# WINDOWS ONLY, because the launcher is Windows only (ADR-0077, "Scope"): a Linux board
# updates through apt and is started by whatever starts it, and adding a binary here that
# nothing installs and the .deb does not carry would be a thing to explain for ever. The
# decisions inside it are still compiled and RUN on Linux -- testers/i1303_check.sh does
# that with g++ directly, the way #1258's and #1305's logic checks do -- so no criterion
# depends on a platform this repository's harnesses cannot reach.
if(WIN32)
    add_executable(opendartboard-launcher src/launcher/main.cpp)

    target_include_directories(opendartboard-launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )

    target_compile_definitions(opendartboard-launcher PRIVATE
        APP_VERSION="${APP_VERSION}"
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    target_compile_features(opendartboard-launcher PRIVATE cxx_std_17)

    if(MSVC)
        # The same three as the detector, for the same three reasons: the sources are
        # UTF-8 and carry a and o with umlauts in every line this program says; /EHsc
        # because CMAKE_CXX_FLAGS is overwritten by the build script; and the Windows SDK
        # force-included ahead of everything, which is what od_platform_first.hpp is for.
        target_compile_options(opendartboard-launcher PRIVATE /utf-8 /EHsc /FIod_platform_first.hpp)
    endif()
endif()

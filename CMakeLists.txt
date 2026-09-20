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

# ---------------------------------------------- the update trust anchors (ADR-0077 §4, #1408)
# The public half of the key a release is signed with, compiled in. Two of them, current and
# next, so a rotation is three ordinary releases; src/update/update_keys.hpp holds the whole
# argument and the recipe for turning a public.pem into these 128 characters.
#
# EMPTY IS A LEGAL VALUE AND IT MEANS "nothing here can vouch for anything". A build with no
# anchor refuses every manifest with NoAnchor, which is the safe direction to fail in and is
# what every build before #1408 did. So it is not required here: a developer's build, a
# tester's build and `make build` on somebody's laptop all go on working with nothing passed.
# What .github/workflows/release.yml does instead is refuse to RELEASE without one, by name,
# before it signs anything -- because an artefact that cannot verify the manifest shipped
# beside it is a release nobody can ever update from.
#
# A MALFORMED ONE IS A FATAL ERROR RATHER THAN A SILENT DROP, and that is the half worth
# having here. update_keys::anchors() leaves out anything anchorFromHex refuses, so 127
# characters, a point with the leading 04 still on it, or a pasted newline all compile
# cleanly and produce a binary that answers NoAnchor to a manifest that is perfectly good.
# On a developer's box that is a puzzle; on a release it is a shipped artefact that can never
# be updated. The shape is known here, so it is refused here.
#
# testers/i1305_inside.sh passes its fixture anchor through CMAKE_CXX_FLAGS instead and is
# untouched by this: that is the same preprocessor define arriving by another road, and this
# adds a road rather than closing one.
set(OD_UPDATE_ANCHOR_CURRENT "" CACHE STRING "128 hex characters: the current signing key's public point, X then Y")
set(OD_UPDATE_ANCHOR_NEXT "" CACHE STRING "128 hex characters: the next signing key's public point, for a rotation")

function(od_update_anchors target)
    foreach(which CURRENT NEXT)
        set(anchor "${OD_UPDATE_ANCHOR_${which}}")
        if(NOT anchor STREQUAL "")
            # CMake's regex has no bounded repetition -- `{128}` is matched literally, so
            # `^[0-9a-fA-F]{128}$` refuses every correct anchor and the build can never be
            # given one. Measured on cmake 3.18.4: the 128-hex anchor fails that pattern and
            # passes `^[0-9a-fA-F]+$`. The length is therefore its own check.
            #
            # Found by dispatching the workflow rather than by reading it: the bash guard in
            # `signing-inputs` accepted the same string this refused, and two engines
            # disagreeing about one variable is what named the cause.
            string(LENGTH "${anchor}" anchor_length)
            if(NOT anchor MATCHES "^[0-9a-fA-F]+$" OR NOT anchor_length EQUAL 128)
                message(FATAL_ERROR
                    "OD_UPDATE_ANCHOR_${which} is not 128 hexadecimal characters, so update_keys::anchors() "
                    "would drop it and this build would answer NoAnchor to every manifest. It is the "
                    "uncompressed public point with the leading 04 removed -- see src/update/update_keys.hpp.")
            endif()
            target_compile_definitions(${target} PRIVATE OD_UPDATE_ANCHOR_${which}="${anchor}")
        endif()
    endforeach()
endfunction()

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

# #1408. The detector reads a manifest too -- `--check-update` is its whole subject -- so it
# carries the same anchors the launcher does. ADR-0077's scope is Windows and a Linux board
# updates through apt, but a Linux board that cannot even ASK is a board whose operator has
# no way to learn a fix exists, and the anchor is public: it costs the .deb nothing.
od_update_anchors(opendartboard)

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
# ---------------------------------------------------------- the launcher (#1303, #1306)
# ADR-0077 §1: a board's program is two files. This is the small one -- what a shortcut
# points at, what a tester double-clicks, what stays on screen to say why the detector
# stopped, and, since #1306, what applies an update and can go back to the version that
# worked.
#
# WHAT IT LINKS, AND WHAT THAT COST. #1303 wrote here that it links nothing at all: no
# OpenCV, no httplib, no json. #1306 had to reach the network and verify a signature, so
# it now carries three things and no OpenCV:
#
#   nlohmann_json   header-only, already fetched for the detector. Nothing is linked.
#   winhttp.lib     the transport the detector already uses on Windows. winhttp.dll is an
#                   in-box Windows DLL, so TLS still costs the artefact nothing.
#   bcrypt.lib      ECDSA P-256 through CNG (ADR-0077 §2), which is why this fork vendors
#                   no cryptography. bcrypt.dll is in-box too.
#
# The property #1299 asserts is therefore untouched: `dumpbin /dependents` on the launcher
# names only DLLs that ship with Windows, there is still no second file to put beside it,
# and release.yml's census already reads the launcher as well as the detector. What is
# genuinely gone is "it cannot be broken by a change to the detector's dependencies" --
# it now shares nlohmann with it, which is the price of reading the same manifest the
# detector's --check-update reads, from the same header, with the same refusals.
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

    # #1408: and this is the one that decides whether a board can ever be updated, because
    # the launcher is the half that installs (ADR-0077 §1).
    od_update_anchors(opendartboard-launcher)

    # #1306. nlohmann is the detector's own dependency and is linked as an interface
    # target, so nothing is compiled twice; winhttp and bcrypt are in-box Windows DLLs.
    target_link_libraries(opendartboard-launcher PRIVATE
        nlohmann_json::nlohmann_json
        winhttp
        bcrypt
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

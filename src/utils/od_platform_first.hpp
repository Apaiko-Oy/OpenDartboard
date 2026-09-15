#pragma once
// Force-included into every translation unit on MSVC (see CMakeLists.txt), and
// included by nothing on Linux. It exists because three collisions between the
// Windows SDK and this program only happen when <windows.h> arrives SECOND:
//
//   * ACCESS_MASK — winnt.h typedefs it, OpenCV declares cv::ACCESS_MASK, and
//     every file here says `using namespace cv;`. If OpenCV is parsed first the
//     name is ambiguous inside winnt.h itself and the SDK header fails to
//     compile. 100+ errors, none of them in this program's code.
//   * ERROR — wingdi.h defines it as 0, and logging.hpp has LogLevel::ERROR.
//   * min/max — the usual one, handled by NOMINMAX.
//
// Only ERROR is undefined. `near`/`far`/`small` look like the same kind of
// pollution and are not: FAR expands to `far`, so undefining it breaks the SDK's
// own FD_SET, which is how cpp-httplib fails to compile 60 lines deep in Winsock.
//
// Putting the SDK first and undefining the three macros afterwards costs
// nothing and keeps every other header in the program unaware of Windows.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Media Foundation, here rather than in autocam.hpp, for the same reason and a
// fourth collision: rpcndr.h typedefs `byte` in the global namespace, every file
// in this program says `using namespace std;`, and C++17 has std::byte. Parsed
// after that using-directive the SDK's own COM headers are ambiguous with
// themselves. Parsed before it they are not.
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#undef ERROR

#endif

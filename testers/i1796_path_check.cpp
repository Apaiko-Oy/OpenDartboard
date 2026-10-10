// #1796: which manifest path a build asks for, pure.
//
// Turnaus serves one manifest per channel per platform, told apart by a suffix on the
// path (docs/api.md, "The update manifest per platform"), and the suffix is compiled in
// as OD_UPDATE_PLATFORM (src/update/update_check.hpp says why it is not read at runtime).
// Three facts, each a different thing to get wrong:
//
//   1. the bare path is what it always was, so every Windows board and every build that
//      defines nothing asks exactly #1302's address -- testers/i1305_update_check.cpp
//      asserts the same string, and this file asserts it with the suffix made explicit;
//   2. a suffix lands between the channel and `.json`, for both channels;
//   3. and THIS build's pathFor(channel) is the one its definition says: suffixed when
//      OD_UPDATE_PLATFORM is defined, bare when it is not. testers/i1796_inside.sh compiles
//      this file both ways, so both halves of that are measured by one source.
//
// The mutation (testers/i1796_check.sh --mutate) drops the suffix from platformSuffix()
// on a copy of src/: fact 3's suffixed half goes red here, naming the path the build
// would really have asked, and the end-to-end install in i1796_inside.sh goes red with
// it because the served manifest is never found.
//
//   unit_check.sh 1796 runs this alone, undefined; i1796_inside.sh runs it twice.

#include "update/update_check.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void check(bool passed, const std::string &what, const std::string &got = "")
{
    std::cout << (passed ? "ok   " : "FAIL ") << what;
    if (!passed && !got.empty())
    {
        std::cout << " (got " << got << ")";
    }
    std::cout << std::endl;
    if (!passed)
    {
        failures++;
    }
}

int main()
{
    const std::string bare = "/updates/opendartboard/stable.json";
    const std::string arm64 = "/updates/opendartboard/stable-linux-arm64.json";

    // 1. The Windows path, unchanged: no suffix is #1302's address.
    check(update_check::pathFor("stable", "") == bare, "no suffix: the path is #1302's, unchanged",
          update_check::pathFor("stable", ""));
    check(update_check::pathFor("beta", "") == "/updates/opendartboard/beta.json",
          "no suffix, beta: /updates/opendartboard/beta.json", update_check::pathFor("beta", ""));

    // 2. A platform's suffix sits between the channel and .json.
    check(update_check::pathFor("stable", "-linux-arm64") == arm64, "-linux-arm64: " + arm64,
          update_check::pathFor("stable", "-linux-arm64"));
    check(update_check::pathFor("beta", "-linux-arm64") == "/updates/opendartboard/beta-linux-arm64.json",
          "-linux-arm64, beta: /updates/opendartboard/beta-linux-arm64.json",
          update_check::pathFor("beta", "-linux-arm64"));

    // 3. This build.
#ifdef OD_UPDATE_PLATFORM
    check(std::string(OD_UPDATE_PLATFORM) == "linux-arm64", "this build is compiled for linux-arm64",
          OD_UPDATE_PLATFORM);
    check(update_check::platformSuffix() == "-linux-arm64", "OD_UPDATE_PLATFORM defined: the suffix is -linux-arm64",
          update_check::platformSuffix());
    check(update_check::pathFor("stable") == arm64, "OD_UPDATE_PLATFORM defined: pathFor(stable) carries the platform",
          update_check::pathFor("stable"));
    check(update_check::pathFor("stable") != bare, "  and it is NOT the Windows path, which would install a Windows zip",
          update_check::pathFor("stable"));
#else
    check(update_check::platformSuffix().empty(), "OD_UPDATE_PLATFORM undefined: the suffix is empty",
          update_check::platformSuffix());
    check(update_check::pathFor("stable") == bare, "OD_UPDATE_PLATFORM undefined: pathFor(stable) is the bare path",
          update_check::pathFor("stable"));
#endif

    std::cout << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

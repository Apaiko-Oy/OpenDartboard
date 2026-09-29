// unrun-tester: compiled and run by testers/i1660_check.sh, which needs a live systemd and is kept unrun for testers/i1383_units.sh's reason.
// #1660: where the detector would keep its credential, asked of src/utils/od_paths.hpp
// alone -- no OpenCV, no nlohmann, no network -- so the answer can be read under any
// environment a harness can build: `env -i`, a unit's, a mutated header's.
//
// It prints three lines and decides nothing; testers/i1660_check.sh decides.
//
//   dir=<configDir()>
//   credentials=<configFile("credentials.json")>   the default main.cpp gives --credentials
//   cwd=<the working directory>                    what a relative path would have meant
//
// An empty value is printed as an empty value, not as a word, so `dir=` is the no-home
// answer and a harness compares bytes.
#include "od_paths.hpp"

#include <cstdio>
#include <unistd.h>

int main()
{
    char cwd[4096] = {0};
    if (::getcwd(cwd, sizeof cwd) == nullptr)
    {
        cwd[0] = '\0';
    }
    std::printf("dir=%s\n", od_paths::configDir().c_str());
    std::printf("credentials=%s\n", od_paths::configFile("credentials.json").c_str());
    std::printf("cwd=%s\n", cwd);
    return 0;
}

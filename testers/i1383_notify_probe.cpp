// #1383: src/utils/od_notify.hpp, compiled on its own and pointed at a real systemd.
//
// It is the detector's supervisor half with everything else taken away -- no OpenCV, no
// cameras, no scorer -- so testers/i1383_units.sh can put THIS repository's sendto() in
// front of a live manager and read back what the manager kept. A stand-in that sent the
// same bytes with systemd-notify would measure systemd and not us.
//
//   i1383_notify_probe <word> <detail> <seconds-to-stay-up>
//
// unrun-tester: compiled and run by testers/i1383_units.sh, which needs a live systemd
// session manager and is kept out of run_all.sh for that reason and carries its own
// marker. A marker is per file here, deliberately (#1371), so this one says it too.
#include "utils/od_notify.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <word> <detail> <seconds>\n", argv[0]);
        return 2;
    }
    std::printf("supervised=%d\n", od_notify::supervised() ? 1 : 0);
    const bool sent = od_notify::status(argv[1], argv[2]);
    std::printf("sent=%d\n", sent ? 1 : 0);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(std::atoi(argv[3])));
    return 0;
}

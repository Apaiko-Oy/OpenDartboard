# The image the AddressSanitizer testers run in (#1688): 1355-bounds and 1317-asan, and
# nothing else. Every other tester stays on od-amd64:bullseye; this file does not touch it.
#
# Build it from the checkout root, sending only the two files it reads as the context
# (the tree is ~640 MB and none of the rest is wanted):
#
#   tar -c apt-packages.txt testers/asan.dockerfile \
#     | docker build -f testers/asan.dockerfile -t od-amd64:trixie-asan -
#
# WHY NOT BULLSEYE. gcc 10.2.1's libasan.so.6 puts its primary allocator at a FIXED
# address, 0x600000000000-0x640000000000, and maps it MAP_FIXED over whatever is there.
# On this kernel vm.mmap_rnd_bits is 32 (read inside the container), so a PIE executable
# loads at 0x555555554000 plus up to 2^32 pages -- up to ~0x655555554000 -- and about a
# quarter of the time it lands inside that 4 TB window. The allocator then maps over the
# program's own text, the first instruction run there faults, and ASan's SEGV handler
# returns to the same instruction: `AddressSanitizer:DEADLYSIGNAL` for ever. Measured: an
# empty main hung 9 of 40 in od-amd64:bullseye, and a hung process's /proc/<pid>/maps shows
# the allocator at 0x600000000000 and NO mapping of the executable at all.
#
# gcc 12.2's libasan.so.8 (bookworm) keeps the fixed base and hung 13 of 40. gcc 14.2's
# libasan.so.8 (trixie) chooses the allocator's base at start-up instead, so a program the
# kernel put at 0x606016bb5000 -- inside the old window -- ran clean; an empty main
# finished 40 of 40. That is the fix, and it needs no seccomp or `setarch -R` change: the
# container's sandbox is untouched (the maintainer's decision of 2026-10-13).
#
# Pinned like the gate image: the base by digest, the archive by snapshot date, so a
# rebuild next year installs the same compiler that was measured here.
FROM debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5

ENV DEBIAN_FRONTEND=noninteractive

RUN printf 'deb http://snapshot.debian.org/archive/debian/20261001T000000Z trixie main\ndeb http://snapshot.debian.org/archive/debian-security/20261001T000000Z trixie-security main\n' > /etc/apt/sources.list \
 && rm -f /etc/apt/sources.list.d/* \
 && echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99snapshot

COPY apt-packages.txt /tmp/
RUN apt-get update -y && xargs apt-get install -y < /tmp/apt-packages.txt

WORKDIR /app

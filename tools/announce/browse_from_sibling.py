#!/usr/bin/env python3
"""What a second device on the same network sees (issue #1189).

Browses _opendartboard._tcp.local. with python3-zeroconf - a resolver of its own, not
Avahi's - for a few seconds and prints every board it finds: name, address, port, txt.
Run it from a sibling container on the board's bridge network, or from a laptop on the
club's wifi. Prints "no board answered" when nothing does.

    python3 tools/announce/browse_from_sibling.py --seconds 8
"""

import argparse
import sys
import time

from zeroconf import ServiceBrowser, Zeroconf  # python3-zeroconf


class Seen:
    def __init__(self):
        self.boards = {}

    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        if info is None:
            return
        addresses = [".".join(str(b) for b in a) for a in info.addresses]
        txt = {k.decode(): (v.decode() if v else "") for k, v in info.properties.items()}
        self.boards[name] = (addresses, info.port, txt)

    def remove_service(self, zc, type_, name):
        self.boards.pop(name, None)

    def update_service(self, zc, type_, name):
        self.add_service(zc, type_, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=8.0)
    args = parser.parse_args()
    zc = Zeroconf()
    seen = Seen()
    ServiceBrowser(zc, "_opendartboard._tcp.local.", seen)
    time.sleep(args.seconds)
    zc.close()
    if not seen.boards:
        print("no board answered")
        return 1
    for name, (addresses, port, txt) in sorted(seen.boards.items()):
        label = name.split("._opendartboard")[0]
        print(f"board '{label}'  {','.join(addresses)}:{port}  {txt}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

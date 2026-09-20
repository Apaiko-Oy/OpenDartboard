#!/usr/bin/env python3
"""#1383: a stand-in for the thing systemd is, from the detector's side of the wire.

The detector's supervisor half is forty lines of sendto() to $NOTIFY_SOCKET, and what a
container has none of is an init. So this binds the socket systemd would have bound,
writes every datagram it receives to a transcript, one per line, and is killed when the
phase that started it has finished with it.

It measures the WIRE and deliberately not the manager: that a real systemd keeps a
STATUS= and prints it as the `Status:` line is measured in testers/i1383_units.sh, on a
real one, because nothing in a container can answer it. The two halves are the two
things that can go wrong -- a datagram nobody sends, and a datagram nobody reads -- and
each is measured where it can be.

    python3 i1383_notify_listener.py <socket-path> <transcript-path>

It prints READY on stdout once the socket is bound, so the phase that starts it can wait
for that line rather than sleeping and hoping. A detector that sends before the bind
sends into nothing, and a sleep is how that becomes an intermittent green.
"""
import os
import socket
import sys

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    path, transcript = sys.argv[1], sys.argv[2]
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    # systemd's own socket is 0666-ish; nothing here runs as another user, but a mode
    # that would refuse the sender is a failure that reads as silence.
    os.chmod(path, 0o666)
    with open(transcript, "a", buffering=1) as out:
        print("READY", flush=True)
        while True:
            datagram, _ = sock.recvfrom(65536)
            for line in datagram.decode("utf-8", "replace").splitlines():
                out.write(line + "\n")

if __name__ == "__main__":
    sys.exit(main())

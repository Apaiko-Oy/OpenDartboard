#!/usr/bin/env python3
"""Announcement and setup-view check for OpenDartboard (issue #1189).

One command. It runs the detector twice against stated inputs - without `--listen` and
with it - with `--label` and `--announce-dir` pointed at a directory Avahi reads, and asks
the host's own responder what it is publishing; then it runs `--setup` and reads the QR
code back the way a phone camera would.

Without --listen it asserts:
  * no service file is written to the announce directory
  * a resolver (avahi-browse) finds no _opendartboard._tcp instance with the label
  * the log says the board is not announced
With --listen it asserts:
  * the service file is written, names the label, the port and path=/scores, and
    carries no token
  * the resolver finds the instance: name == label, port == 13520, txt has path=/scores
  * the token appears in nothing the resolver returns
  * on stop, the file is removed and the resolver finds nothing again
And --setup:
  * the output carries the token in no line of plain text
  * the half-block QR, drawn to a PNG, decodes (zbarimg) to exactly
    ws://<address>:13520/scores?token=<token> - the URL docs/api.md describes - where
    <address> is this host's non-loopback address and <token> is the file's

    mkdir -p /run/dbus && dbus-daemon --system --fork && avahi-daemon -D     # the responder
    python3 tools/announce/check_announce.py \\
        --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4

Needs avahi-daemon running on this host (avahi-utils for avahi-browse), zbar-tools and
python3-pil; the detector's Run and the token reader come from the sibling harnesses.
The announce directory defaults to /etc/avahi/services, so run it as root or point
--announce-dir at a directory Avahi has been told to read.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "board_position"))
sys.path.insert(0, os.path.join(HERE, "..", "score_socket"))
from check_board_position import read_token  # noqa: E402
from check_score_socket import Run, second_address  # noqa: E402

SERVICE_TYPE = "_opendartboard._tcp"


class AnnounceRun(Run):
    """The socket harness's Run, with the announce flags on the detector's argv. Run
    builds its argv inside __init__, so the flags are added by wrapping Popen for the
    one call rather than by editing the sibling harness."""

    def __init__(self, extra, *args, **kwargs):
        real = subprocess.Popen

        def with_extra(argv, **popen_kwargs):
            return real(list(argv) + list(extra), **popen_kwargs)

        subprocess.Popen = with_extra
        try:
            super().__init__(*args, **kwargs)
        finally:
            subprocess.Popen = real
UPPER, LOWER, BOTH = "▀", "▄", "█"


def browse(timeout):
    """What the host's responder resolves for the service type: a list of dicts with
    name, port and txt, from avahi-browse's parsable output; the raw text beside it."""
    try:
        out = subprocess.run(
            ["avahi-browse", "-rtp", SERVICE_TYPE], capture_output=True, text=True, timeout=timeout
        ).stdout
    except (FileNotFoundError, subprocess.TimeoutExpired) as error:
        return None, repr(error)
    found = []
    for line in out.splitlines():
        parts = line.split(";")
        if len(parts) >= 10 and parts[0] == "=" and parts[2] == "IPv4":
            found.append({"name": parts[3].replace("\\032", " "), "host": parts[6], "address": parts[7],
                          "port": parts[8], "txt": parts[9]})
    return found, out


def browse_until(label, present, timeout):
    """Browse until the label is (or is not) resolved, within `timeout` seconds."""
    deadline = time.time() + timeout
    found, raw = [], ""
    while time.time() < deadline:
        found, raw = browse(timeout=10)
        if found is None:
            return None, raw
        if any(e["name"] == label for e in found) == present:
            break
        time.sleep(1)
    return found, raw


def draw_png(block_lines, path, scale=8):
    """The half-block QR as a phone would see it on a screen: one square per module."""
    from PIL import Image, ImageDraw  # python3-pil

    width = max(len(line) for line in block_lines)
    height = 2 * len(block_lines)
    image = Image.new("1", (width * scale, height * scale), 1)
    draw = ImageDraw.Draw(image)
    for row, line in enumerate(block_lines):
        for col, ch in enumerate(line):
            upper = ch in (UPPER, BOTH)
            lower = ch in (LOWER, BOTH)
            if upper:
                draw.rectangle([col * scale, 2 * row * scale, (col + 1) * scale - 1, (2 * row + 1) * scale - 1], fill=0)
            if lower:
                draw.rectangle([col * scale, (2 * row + 1) * scale, (col + 1) * scale - 1, (2 * row + 2) * scale - 1], fill=0)
    image.save(path)


def decode_png(path):
    """zbarimg's reading of the image, as a camera app would give it."""
    try:
        out = subprocess.run(["zbarimg", "-q", "--raw", path], capture_output=True, text=True, timeout=30)
    except FileNotFoundError:
        return None
    return out.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default="build/opendartboard")
    parser.add_argument("--cams", required=True)
    parser.add_argument("--cycles", type=int, default=120, help="the socket opens before the first dart; no dart is needed")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--port", type=int, default=13520)
    parser.add_argument("--label", default="Kello 1 harness", help="the label the board is announced as")
    parser.add_argument("--announce-dir", default="/etc/avahi/services")
    parser.add_argument("--workdir", default=None, help="fresh directory for the runs; a temp dir by default")
    parser.add_argument("--connect-timeout", type=float, default=180.0)
    parser.add_argument("--browse-timeout", type=float, default=20.0, help="how long the responder gets to publish or withdraw")
    args = parser.parse_args()

    workdir = args.workdir or tempfile.mkdtemp(prefix="announce-")
    binary = os.path.abspath(args.binary)
    cams = ",".join(os.path.abspath(c) for c in args.cams.split(","))
    token_path = os.path.join(workdir, "score_token")
    service_file = os.path.join(args.announce_dir, "opendartboard.service")
    label = args.label
    address = second_address()

    print("announce check v1")
    print(f"binary   {binary}")
    print(f"workdir  {workdir}   port {args.port}   announce dir {args.announce_dir}   label '{label}'   address {address}")

    results = []

    def record(what, expected, outcome, detail=""):
        ok = outcome == expected
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {what:60} expected {expected:9} got {outcome:9} {detail}")

    for tool in ("avahi-browse", "zbarimg"):
        record(f"{tool} is on this host", "yes", "yes" if shutil.which(tool) else "no")
    if os.path.exists(service_file):
        os.remove(service_file)
        print(f"  note  removed a stale {service_file} before starting")

    extra = ["--label", label, "--announce-dir", args.announce_dir]

    # ---- run 1: no --listen
    print("run 1: no --listen")
    run = AnnounceRun(extra, binary, cams, args.width, args.height, workdir, args.cycles, listen=False)
    token = read_token(token_path, time.time() + args.connect_timeout)
    if token is None:
        record("the token file is written before the socket opens", "file", "missing", token_path)
        run.stop()
        print("FAIL: no token")
        return 1
    if not run.wait_listening(args.port, token, args.connect_timeout):
        record("the socket opens on loopback", "open", "closed")
        run.stop()
        print("FAIL: the socket never opened")
        return 1
    record("no service file is written", "absent", "present" if os.path.exists(service_file) else "absent", service_file)
    found, raw = browse_until(label, present=False, timeout=args.browse_timeout)
    if found is None:
        record("the resolver answers", "yes", "no", raw)
    else:
        record("the resolver finds no board with the label", "absent",
               "present" if any(e["name"] == label for e in found) else "absent",
               f"{len(found)} {SERVICE_TYPE} instance(s) resolved")
    run.stop()
    log = run.log_text()
    record("the log says the board is not announced", "yes", "yes" if "not announced: loopback only" in log else "no")

    # ---- run 2: --listen
    print("run 2: --listen")
    run = AnnounceRun(extra, binary, cams, args.width, args.height, workdir, args.cycles, listen=True)
    if not run.wait_listening(args.port, token, args.connect_timeout):
        record("the socket opens on the network", "open", "closed")
        run.stop()
        print("FAIL: the socket never opened")
        return 1
    if os.path.exists(service_file):
        record("the service file is written", "present", "present", service_file)
        body = open(service_file).read()
        record("the service file names the port", "yes", "yes" if f"<port>{args.port}</port>" in body else "no")
        record("the service file names the label", "yes", "yes" if f"<name>{label}</name>" in body else "no")
        record("the service file names the path", "yes", "yes" if "path=/scores" in body else "no")
        record("the service file carries no token", "absent", "present" if token in body else "absent")
    else:
        record("the service file is written", "present", "absent", service_file)
    found, raw = browse_until(label, present=True, timeout=args.browse_timeout)
    if found is None:
        record("the resolver answers", "yes", "no", raw)
    else:
        mine = [e for e in found if e["name"] == label]
        record("the resolver finds the board by its label", "present", "present" if mine else "absent",
               f"{len(found)} {SERVICE_TYPE} instance(s) resolved")
        if mine:
            entry = mine[0]
            record("the resolved port is the socket's", str(args.port), entry["port"], f"host {entry['host']} address {entry['address']}")
            record("the resolved txt names the path", "yes", "yes" if "path=/scores" in entry["txt"] else "no", entry["txt"])
            record("the resolved txt names the label", "yes", "yes" if f"label={label}" in entry["txt"] else "no")
        record("the token is in nothing the resolver returns", "absent", "present" if token in raw else "absent")
    run.stop()
    log = run.log_text()
    record("the log says the board is announced", "yes", "yes" if f"announced as '{label}'" in log else "no")
    record("the service file is removed on stop", "absent", "present" if os.path.exists(service_file) else "absent")
    found, raw = browse_until(label, present=False, timeout=args.browse_timeout)
    if found is not None:
        record("the resolver finds nothing after the stop", "absent",
               "present" if any(e["name"] == label for e in found) else "absent")
    record("the token is in no log line", "absent", "present" if token in log else "absent")

    # ---- --setup
    print("--setup")
    setup = subprocess.run([binary, "--setup", "--label", label, "--token-file", token_path],
                           cwd=workdir, capture_output=True, text=True, timeout=30)
    out = setup.stdout
    record("--setup exits 0", "0", str(setup.returncode), (setup.stderr or "").strip()[:80])
    record("the token is in no line of plain text", "absent", "present" if token in out else "absent")
    stated = re.search(r"ws://([0-9.]+):(\d+)/scores", out)
    record("the view states the socket address", "yes", "yes" if stated else "no", stated.group(0) if stated else "")
    lines = out.splitlines()
    blocks = [l for l in lines if l and set(l) <= {UPPER, LOWER, BOTH, " "} and any(c != " " for c in l)]
    record("the view draws a QR", "yes", "yes" if len(blocks) > 10 else "no", f"{len(blocks)} block lines")
    png = os.path.join(workdir, "setup_qr.png")
    if blocks:
        draw_png(blocks, png)
        decoded = decode_png(png)
        expected = f"ws://{address}:{args.port}/scores?token={token}"
        record("the QR decodes to the documented URL", "match", "match" if decoded == expected else "differs",
               f"decoded {decoded!r}" if decoded != expected else f"ws://{address}:{args.port}/scores?token=<token>")
        if stated:
            record("the QR's address is the one the view states", "match", "match" if stated.group(1) == address else "differs",
                   f"stated {stated.group(1)} decoded {address}")
        record("a decoded QR without the token would not match", "yes",
               "yes" if decoded and "token=" in decoded and decoded.split("token=")[1] == token else "no")

    failed = results.count(False)
    print(f"checks {len(results)}   failed {failed}")
    print("PASS" if failed == 0 else f"FAIL: {failed} check(s) did not hold")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

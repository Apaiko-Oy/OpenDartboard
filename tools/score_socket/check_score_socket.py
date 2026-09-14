#!/usr/bin/env python3
"""Score-socket check for OpenDartboard (issue #1187).

One command. It runs the detector twice against stated inputs - once without
`--listen`, once with it - and probes the WebSocket upgrade from two addresses: loopback,
and a second address, which is this host's own non-loopback address (detected, or given
with --second-address). A socket bound to loopback refuses a connection to the host's
other address even from the same machine, so the bind address is what is measured, and
running this inside a container measures the container's bridge address.

Without --listen it asserts:
  * loopback with the token      -> 101, subscribed
  * loopback without the token   -> 401
  * loopback with a wrong token  -> 401
  * second address with the token -> the connection is refused (nothing listens there)
With --listen it asserts:
  * second address with the token -> 101, and a score message arrives on it
  * second address without / with a wrong token -> 401
  * loopback without the token   -> 401 (a local tool and a phone use one path)
  * loopback with the token      -> 101
And on both runs: the detector's log names the address it bound; every refusal is
logged; the token appears nowhere in the log or on stdout; the token file is mode 0600.

    python3 tools/score_socket/check_score_socket.py \\
        --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4

Python 3.8+, standard library only. The WebSocket client is the raw one from
tools/board_position, so the bytes read are the bytes on the wire.
"""

import argparse
import os
import re
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "board_position"))
from check_board_position import RawWebSocket, read_token  # noqa: E402


def second_address():
    """The address this host would use to reach the world: not loopback."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("10.255.255.255", 1))
        return probe.getsockname()[0]
    finally:
        probe.close()


def upgrade(host, port, token):
    """One upgrade attempt. Returns (outcome, detail): outcome is '101', a refused
    status code such as '401', 'refused' when nothing listens, or 'timeout'."""
    try:
        ws = RawWebSocket(host, port, token=token)
    except ConnectionRefusedError:
        return "refused", "connection refused"
    except socket.timeout:
        return "timeout", "no answer"
    except ConnectionError as error:
        match = re.search(r" (\d{3}) ", str(error))
        return (match.group(1) if match else "error"), str(error)
    except OSError as error:
        return "error", repr(error)
    ws.sock.close()
    return "101", "subscribed"


def wait_for_message(host, port, token, budget):
    """Subscribe and wait up to `budget` seconds for the first text frame."""
    ws = RawWebSocket(host, port, token=token)
    deadline = time.time() + budget
    ws.sock.settimeout(1.0)
    try:
        frames = ws.frames()
        while time.time() < deadline:
            try:
                text = next(frames)
                return "101", f"subscribed, first message {text[:72]}"
            except StopIteration:
                return "101", "subscribed, socket closed before a message"
            except socket.timeout:
                continue
    finally:
        ws.sock.close()
    return "101", f"subscribed, no message within {budget:.0f}s"


class Run:
    """One detector process in one working directory."""

    def __init__(self, binary, cams, width, height, workdir, cycles, listen):
        self.workdir = workdir
        self.listen = listen
        os.makedirs(workdir, exist_ok=True)
        env = dict(os.environ, OD_MAX_CYCLES=str(cycles))
        argv = [binary, "--debug", "--cams", cams, "--width", str(width), "--height", str(height)]
        if listen:
            argv.append("--listen")
        self.stdout_path = os.path.join(workdir, "detector-listen.out" if listen else "detector-loopback.out")
        self.stdout = open(self.stdout_path, "wb")
        self.proc = subprocess.Popen(argv, cwd=workdir, env=env, stdout=self.stdout, stderr=subprocess.STDOUT)

    def wait_listening(self, port, token, timeout):
        """Until loopback answers the upgrade - with anything but a closed port."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False
            outcome, _ = upgrade("127.0.0.1", port, token)
            if outcome not in ("refused", "error"):
                return True
            time.sleep(0.25)
        return False

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.stdout.close()

    def log_text(self):
        """This run's own stdout. Not debug_frames/opendartboard.log: under --debug every
        line is written to both, and the file is appended across the two runs, so
        counting in it doubles and accumulates."""
        with open(self.stdout_path, errors="replace") as handle:
            return handle.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default="build/opendartboard")
    parser.add_argument("--cams", required=True)
    parser.add_argument("--cycles", type=int, default=400, help="enough for the first dart; the run is stopped after the probes")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--port", type=int, default=13520)
    parser.add_argument("--second-address", default=None, help="a non-loopback address of this host; detected by default")
    parser.add_argument("--workdir", default=None, help="fresh directory for both runs; a temp dir by default")
    parser.add_argument("--connect-timeout", type=float, default=180.0)
    parser.add_argument("--message-timeout", type=float, default=120.0, help="how long to wait for the first message on the network subscriber")
    args = parser.parse_args()

    workdir = args.workdir or tempfile.mkdtemp(prefix="score-socket-")
    binary = os.path.abspath(args.binary)
    cams = ",".join(os.path.abspath(c) for c in args.cams.split(","))
    second = args.second_address or second_address()
    port = args.port
    token_path = os.path.join(workdir, "score_token")

    print("score-socket check v1")
    print(f"binary   {binary}")
    print(f"cams     {cams}")
    print(f"workdir  {workdir}   port {port}   second address {second}")

    results = []

    def record(label, expected, outcome, detail):
        ok = outcome == expected
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {label:58} expected {expected:8} got {outcome:8} {detail}")

    if second.startswith("127."):
        record("a second, non-loopback address is available", "yes", "no", second)

    # ---- run 1: no --listen
    print("run 1: no --listen")
    run = Run(binary, cams, args.width, args.height, workdir, args.cycles, listen=False)
    token = read_token(token_path, time.time() + args.connect_timeout)
    if token is None:
        record("the token file is written before the socket opens", "file", "missing", token_path)
        run.stop()
        print("FAIL: no token, nothing to present")
        return 1
    mode = stat.S_IMODE(os.stat(token_path).st_mode)
    record("the token file is mode 0600", "0600", f"{mode:04o}", token_path)
    wrong = ("0" if token[0] != "0" else "1") + token[1:]
    if not run.wait_listening(port, token, args.connect_timeout):
        record("the socket opens on loopback", "open", "closed", "never answered an upgrade")
        run.stop()
        print("FAIL: the socket never opened")
        return 1
    record("loopback, with the token", "101", *upgrade("127.0.0.1", port, token))
    record("loopback, no token", "401", *upgrade("127.0.0.1", port, None))
    record("loopback, wrong token", "401", *upgrade("127.0.0.1", port, wrong))
    record(f"second address {second}, with the token", "refused", *upgrade(second, port, token))
    run.stop()
    log = run.log_text()
    bound = re.findall(r"listening on ws://([^:]+):", log)
    record("the log names the bound address", "127.0.0.1", bound[-1] if bound else "none", "")
    refusals = len(re.findall(r"refused with 401", log))
    record("every refusal is logged", "2", str(refusals), "lines saying 'refused with 401'")
    record("the token is in no log line", "absent", "present" if token in log else "absent", "")

    # ---- run 2: --listen, same working directory, so the same token
    print("run 2: --listen")
    run = Run(binary, cams, args.width, args.height, workdir, args.cycles, listen=True)
    if not run.wait_listening(port, token, args.connect_timeout):
        record("the socket opens", "open", "closed", "never answered an upgrade")
        run.stop()
        print("FAIL: the socket never opened")
        return 1
    record(f"second address {second}, no token", "401", *upgrade(second, port, None))
    record(f"second address {second}, wrong token", "401", *upgrade(second, port, wrong))
    record("loopback, no token", "401", *upgrade("127.0.0.1", port, None))
    record("loopback, with the token", "101", *upgrade("127.0.0.1", port, token))
    try:
        outcome, detail = wait_for_message(second, port, token, args.message_timeout)
    except (OSError, ConnectionError) as error:
        outcome, detail = "error", repr(error)
    record(f"second address {second}, with the token, receives a dart", "101", outcome, detail)
    if outcome == "101" and "first message" not in detail:
        record("a message reached the network subscriber", "message", "none", detail)
    run.stop()
    log = run.log_text()
    bound = re.findall(r"listening on ws://([^:]+):", log)
    record("the log names the bound address", "0.0.0.0", bound[-1] if bound else "none", "")
    refusals = len(re.findall(r"refused with 401", log))
    record("every refusal is logged", "3", str(refusals), "lines saying 'refused with 401'")
    record("the token is in no log line", "absent", "present" if token in log else "absent", "")

    failed = results.count(False)
    print(f"checks {len(results)}   failed {failed}")
    verdict = "PASS" if failed == 0 else f"FAIL: {failed} check(s) did not hold"
    print(verdict)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

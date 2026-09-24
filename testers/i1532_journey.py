#!/usr/bin/env python3
"""#1532: a Windows board is watched updating from one version to the next, and refusing
three ways not to.

unrun-tester: needs a Windows runner with the launcher built by cl.exe, WinHTTP, CNG,
%SystemRoot%\\System32\\tar.exe and a loopback TLS certificate trusted by the machine --
none of which a docker gate has. It is run by .github/workflows/update-journey.yml.

    testers/i1532_journey.py <work> <launcher.exe> <stubs> <zips> <manifests> <port> <cert> <key> \\
                             <A> <B> <C> <BAD>

WHAT IS REAL AND WHAT IS STOOD IN FOR. Every piece of the path is the one that ships: the
launcher is src/launcher/main.cpp compiled by cl.exe, so the manifest is fetched by
WinHTTP, verified by CNG's BCryptVerifySignature against an anchor compiled in, the zip is
fetched over TLS, digested, unpacked by the in-box tar.exe, swapped in by rename, and the
detector is started by CreateProcessA. Nothing here calls a launcher function; the only
thing driven is the .exe, from outside, the way a shortcut drives it.

Two things stand in for the real ones, and neither is on the path under test:

  the detector is testers/i1306_stub.cpp compiled per version. The update happens before
  the detector starts, which is the issue's own reason it needs no camera; and the stub
  prints the same first line the real detector's --version does, carries its version the
  way the artefact does (-DAPP_VERSION), and can be told to fail to start.

  the server is this script, on 127.0.0.1, over TLS with a certificate the job made and
  trusted a minute ago. It serves /updates/opendartboard/stable.json -- #1302's path -- and
  the zips, and it WRITES DOWN every request, which is how "a quick restart does not
  check" is read rather than inferred.

EVERY SCENARIO HAS ITS OWN INSTALL. The launcher remembers when the detector last stopped
(ADR-0077 §7), so a second start in the same directory within fifteen minutes is a quick
restart by design; sharing one layout between scenarios would make every one after the
first measure §7 instead of what it names. The one scenario that shares is the quick
restart itself, which shares on purpose.
"""

import http.server
import os
import shutil
import ssl
import subprocess
import sys
import threading

failures = []


def note(passed, what):
    print(("ok   " if passed else "FAIL ") + what, flush=True)
    if not passed:
        failures.append(what)


class Loopback:
    """One TLS server, one offered manifest at a time, and a log of what was asked."""

    def __init__(self, port, cert, key, zips):
        self.offered = None
        self.asked = []
        self.zips = zips
        loopback = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                loopback.asked.append(self.path)
                body = None
                if self.path == "/updates/opendartboard/stable.json" and loopback.offered is not None:
                    body = open(loopback.offered, "rb").read()
                    kind = "application/json"
                elif self.path.startswith("/rel/"):
                    name = os.path.basename(self.path)
                    candidate = os.path.join(loopback.zips, name)
                    if os.path.isfile(candidate):
                        body = open(candidate, "rb").read()
                        kind = "application/zip"
                if body is None:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Type", kind)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        self.server.socket = context.wrap_socket(self.server.socket, server_side=True)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def offer(self, manifest):
        self.offered = manifest
        self.asked = []


def main():
    if len(sys.argv) != 13:
        raise SystemExit(__doc__)
    work, launcher, stubs, zips, manifests, port, cert, key, A, B, C, BAD = sys.argv[1:]
    port = int(port)

    # The second lock on the door the workflow's guard already shut: a tag is the only
    # thing that publishes, and a run on one is refused here before any throwaway anchor
    # has been put anywhere near a board.
    if os.environ.get("GITHUB_REF_TYPE") == "tag":
        print("REFUSED: this check builds a launcher trusting a throwaway key and must never run on a tag")
        return 2

    server = Loopback(port, cert, key, zips)
    base = "https://127.0.0.1:%d" % port

    def fresh(name):
        """An installed layout at version A: the launcher, and detector A beside it."""
        where = os.path.join(work, name)
        if os.path.exists(where):
            shutil.rmtree(where)
        os.makedirs(where)
        shutil.copy(launcher, os.path.join(where, "opendartboard-launcher.exe"))
        shutil.copy(os.path.join(stubs, "stub-" + A + ".exe"), os.path.join(where, "opendartboard.exe"))
        return where

    def start(where, *extra, bad=None):
        """One start through the launcher, as a shortcut makes it. Returns (rc, output, ran)."""
        ran_to = os.path.join(where, "ran.txt")
        if os.path.exists(ran_to):
            os.remove(ran_to)
        env = dict(os.environ)
        for inherited in ("OD_DETECTOR", "OD_TURNAUS_URL", "OD_UPDATE_NOW", "OD_STUB_BAD", "OD_STUB_KILLED"):
            env.pop(inherited, None)
        env["OD_STUB_RAN_TO"] = ran_to
        if bad:
            env["OD_STUB_BAD"] = bad
        argv = [os.path.join(where, "opendartboard-launcher.exe"), "--turnaus", base,
                "--credentials", os.path.join(where, "credentials.json")] + list(extra)
        done = subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              env=env, timeout=300, cwd=where)
        output = done.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
        ran = open(ran_to).read().split() if os.path.exists(ran_to) else []
        for line in output.splitlines():
            print("     | " + line)
        print("     | (exit %d; the detector ran as %s; the server was asked %s)" % (done.returncode, ran,
                                                                                    server.asked))
        return done.returncode, output, ran

    def installed(where):
        """What the installed program says it is, asked of the program rather than the state file."""
        done = subprocess.run([os.path.join(where, "opendartboard.exe"), "--version"], stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60, cwd=where,
                              env={k: v for k, v in os.environ.items() if not k.startswith("OD_")})
        line = done.stdout.decode("utf-8", errors="replace").strip().splitlines()
        prefix = "OpenDartboard runtime version: "
        return line[0][len(prefix):] if line and line[0].startswith(prefix) else "(unreadable: %r)" % line

    manifest = lambda name: os.path.join(manifests, name + ".json")  # noqa: E731
    stays = "The board starts version " + A + ", the one it already had."

    # ---- 1. A to B, the journey itself -------------------------------------------------------
    print("\n---- 1. an installed %s is offered a signed %s, and becomes it ----" % (A, B))
    board = fresh("update")
    note(installed(board) == A, "update: before anything, the installed program reports " + A)
    server.offer(manifest("good-" + B))
    rc, said, ran = start(board)
    note("/updates/opendartboard/stable.json" in server.asked, "update: the launcher asked #1302's address")
    note("/rel/opendartboard-" + B + ".zip" in server.asked, "update: and fetched the zip the manifest named")
    note("Version " + B + " was installed." in said, "update: the launcher said it updated to " + B)
    note(installed(board) == B, "update: afterwards the installed program reports " + B + " (it says "
         + installed(board) + ")")
    note(ran == [B], "update: and " + B + " is what started, once")
    note(rc == 0, "update: the launcher exited 0")

    # ---- 2. the quick restart, on the same board, seconds later ----------------------------
    print("\n---- 2. the same board restarts seconds later and is offered %s: ADR-0077 §7 ----" % C)
    server.offer(manifest("good-" + C))
    rc, said, ran = start(board)
    note(server.asked == [], "quick restart: the launcher did not check -- the server was asked nothing (it was "
         "asked %s)" % server.asked)
    note(installed(board) == B, "quick restart: " + B + " is still installed")
    note(ran == [B], "quick restart: and " + B + " is what started")
    # The control. Without it, "nothing was asked" is also what a broken server, a manifest
    # nobody could install, or a launcher that never checks would all produce.
    server.offer(manifest("good-" + C))
    rc, said, ran = start(board, "--update-now")
    note("/updates/opendartboard/stable.json" in server.asked and installed(board) == C,
         "quick restart, control: the same offer, forced with --update-now, IS taken -- so the silence above "
         "was §7 and not an offer nobody could install")

    # ---- 3. the zip is not the one the manifest names: its digest -------------------------
    print("\n---- 3. a manifest for %s whose zip does not match its stated sha256 ----" % B)
    board = fresh("digest")
    server.offer(manifest("digest-" + B))
    rc, said, ran = start(board)
    note("/rel/opendartboard-" + B + ".zip" in server.asked, "digest: the zip was fetched, so the refusal is "
         "about its bytes")
    note("CHECKSUM DOES NOT MATCH" in said, "digest: the launcher said why: the checksum does not match")
    note(stays in said, "digest: and that it starts the version it had")
    note(installed(board) == A, "digest: " + A + " is still installed (it says " + installed(board) + ")")
    note(ran == [A], "digest: and " + A + " is what started")

    # ---- 4. ... and its size ------------------------------------------------------------------
    print("\n---- 4. a manifest for %s whose zip does not match its stated size ----" % B)
    board = fresh("size")
    server.offer(manifest("size-" + B))
    rc, said, ran = start(board)
    note("/rel/opendartboard-" + B + ".zip" in server.asked, "size: the zip was fetched, so the refusal is about "
         "its length")
    note("The download was cut short" in said, "size: the launcher said why: the length is not the stated one")
    note(stays in said, "size: and that it starts the version it had")
    note(installed(board) == A, "size: " + A + " is still installed (it says " + installed(board) + ")")
    note(ran == [A], "size: and " + A + " is what started")

    # ---- 5. signed, by somebody else -----------------------------------------------------------
    print("\n---- 5. a manifest for %s signed by a key that is not the anchor ----" % B)
    board = fresh("stranger")
    server.offer(manifest("stranger-" + B))
    rc, said, ran = start(board)
    note("SIGNATURE VERIFICATION FAILED" in said, "stranger: the launcher said why: the signature does not verify")
    note(not any(one.startswith("/rel/") for one in server.asked), "stranger: and fetched nothing the manifest "
         "named")
    note(installed(board) == A, "stranger: " + A + " is still installed (it says " + installed(board) + ")")
    note(ran == [A], "stranger: and " + A + " is what started")

    # ---- 6. a B that will not start ------------------------------------------------------------
    print("\n---- 6. a signed %s whose detector fails to start, and the launcher going back ----" % BAD)
    board = fresh("rollback")
    server.offer(manifest("good-" + BAD))
    rc, said, ran = start(board, bad=BAD)
    note("Version " + BAD + " was installed." in said, "rollback: " + BAD + " really was installed first")
    note("Version " + BAD + " did not start" in said, "rollback: the launcher said " + BAD + " did not start")
    note("Going back to version " + A + ", which worked." in said, "rollback: the launcher SAID it went back to "
         + A + " -- read from its own words, not inferred")
    note(ran == [BAD, BAD, A], "rollback: the detector ran as %s, two failed starts then %s (it ran as %s)"
         % ([BAD, BAD, A], A, ran))
    note(installed(board) == A, "rollback: " + A + " is installed again (it says " + installed(board) + ")")

    print()
    if failures:
        print("%d FAILED:" % len(failures))
        for one in failures:
            print("  FAIL " + one)
        return 1
    print("JOURNEY_OK: every scenario answered the way #1532 asks")
    return 0


if __name__ == "__main__":
    sys.exit(main())

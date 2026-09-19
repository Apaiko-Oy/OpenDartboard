#!/usr/bin/env python3
"""#1306: real release zips, and the signed manifests that name them.

#1305's fixtures were manifests alone, because #1305 downloads nothing. This slice
INSTALLS, so its fixtures have to be artefacts: a zip of the shape release.yml's
Compress-Archive writes -- deflated entries, opendartboard beside LICENSE and
BUILD-INFO.txt -- around a stub that says which version it is. Nothing here is argued:
the digest in every manifest is the SHA-256 of the file on disk, and every signature is
made by OpenSSL rather than by anything in this tree.

    testers/i1306_fixtures.py <outdir> <stubdir> <version> [<version> ...]

<stubdir> holds one compiled stub per version, named `stub-<version>`. For each it
writes `rel/opendartboard-<version>.zip` and a manifest `stable-<version>.json` signed
for the stable channel by a key minted on this run.

And the four a board must refuse, every one of them well-formed enough to be installed
by something that does not check:

    baddigest-<v>.json    the real size, one hex digit of the digest changed
    toonew-<v>.json       minimumLauncher above anything this launcher can be
    tampered-<v>.json     stable-<v>.json with one payload byte changed and the
                          signature left exactly as it was
    stranger-<v>.json     the same payload signed by a DIFFERENT key

The keys are regenerated every run because ECDSA signing is randomised, so a fixture's
bytes are not stable and pinning them would be pinning nothing. What is stable is the
relationship between the files, which is what the harness asserts.
"""

import base64
import hashlib
import json
import os
import subprocess
import sys
import zipfile

# The host is a name that can never resolve. The harness rewrites the scheme and the
# host and keeps the path, so the bytes come from its own server on 127.0.0.1 -- and a
# fixture that somehow reached a real network would reach nothing at all.
BASE = "https://releases.example.invalid/rel/"
MINIMUM_LAUNCHER = "v1.0.0"


def run(*argv, stdin=None):
    return subprocess.run(argv, input=stdin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout


def keypair(out, name):
    """A throwaway P-256 key. Never a trust anchor, never committed, minted per run."""
    private = os.path.join(out, name + ".pem")
    run("openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", private)
    text = run("openssl", "ec", "-in", private, "-pubout", "-text", "-noout").decode()
    point = ""
    reading = False
    for line in text.splitlines():
        if line.strip().startswith("pub:"):
            reading = True
            continue
        if reading:
            if ":" not in line or "ASN1" in line or "NIST" in line:
                break
            point += line.strip().replace(":", "")
    if not point.startswith("04") or len(point) != 130:
        raise SystemExit("openssl did not print an uncompressed public point: " + point[:24])
    return private, point[2:]


def sign(out, private, payload_bytes):
    message = os.path.join(out, "message.bin")
    with open(message, "wb") as handle:
        handle.write(payload_bytes)
    signature = run("openssl", "dgst", "-sha256", "-sign", private, message)
    os.remove(message)
    return signature


def envelope(out, private, payload_bytes):
    return (
        json.dumps(
            {
                "payload": base64.b64encode(payload_bytes).decode(),
                "signature": base64.b64encode(sign(out, private, payload_bytes)).decode(),
            },
            indent=2,
        )
        + "\n"
    )


def make_executable_in_zip(path, detector_binary, version):
    """Rewrite the archive with the detector's entry carrying mode 0755."""
    info = zipfile.ZipInfo("opendartboard")
    info.external_attr = (0o100755 << 16)
    info.compress_type = zipfile.ZIP_DEFLATED
    with open(detector_binary, "rb") as handle:
        body = handle.read()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zip_file:
        zip_file.writestr(info, body)
        zip_file.writestr("LICENSE", "GPL-3.0, as the real package carries it.\n")
        zip_file.writestr(
            "BUILD-INFO.txt",
            "OpenDartboard fixture for testers/i1306_check.sh\nversion:    " + version + "\n",
        )


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    out, stubs, versions = sys.argv[1], sys.argv[2], sys.argv[3:]
    releases = os.path.join(out, "rel")
    os.makedirs(releases, exist_ok=True)

    ours, point = keypair(out, "signing")
    stranger, _ = keypair(out, "stranger")
    with open(os.path.join(out, "anchor.hex"), "w") as handle:
        handle.write(point + "\n")

    for version in versions:
        stub = os.path.join(stubs, "stub-" + version)
        if not os.path.exists(stub):
            raise SystemExit("no stub for " + version + " at " + stub)
        name = "opendartboard-" + version + ".zip"
        archive = os.path.join(releases, name)
        make_executable_in_zip(archive, stub, version)
        raw = open(archive, "rb").read()
        payload = {
            "channel": "stable",
            "version": version,
            "url": BASE + name,
            "sha256": hashlib.sha256(raw).hexdigest(),
            "size": len(raw),
            "minimumLauncher": MINIMUM_LAUNCHER,
        }
        good = json.dumps(payload, separators=(",", ":")).encode()
        with open(os.path.join(out, "stable-" + version + ".json"), "w") as handle:
            handle.write(envelope(out, ours, good))

        # One hex digit of the digest, and nothing else. Still 64 lowercase characters,
        # so it passes every shape check a manifest has and fails only on the bytes.
        wrong = dict(payload)
        first = payload["sha256"][0]
        wrong["sha256"] = ("0" if first != "0" else "1") + payload["sha256"][1:]
        with open(os.path.join(out, "baddigest-" + version + ".json"), "w") as handle:
            handle.write(envelope(out, ours, json.dumps(wrong, separators=(",", ":")).encode()))

        too_new = dict(payload, minimumLauncher="v9999.0.0")
        with open(os.path.join(out, "toonew-" + version + ".json"), "w") as handle:
            handle.write(envelope(out, ours, json.dumps(too_new, separators=(",", ":")).encode()))

        # The signature from the good manifest over a payload that is one byte different.
        tampered = good.replace(b'"size":' + str(payload["size"]).encode(),
                                b'"size":' + str(payload["size"] + 1).encode())
        if tampered == good:
            raise SystemExit("the tamper changed nothing")
        signed = json.loads(open(os.path.join(out, "stable-" + version + ".json")).read())
        with open(os.path.join(out, "tampered-" + version + ".json"), "w") as handle:
            handle.write(
                json.dumps(
                    {"payload": base64.b64encode(tampered).decode(), "signature": signed["signature"]}, indent=2
                )
                + "\n"
            )

        with open(os.path.join(out, "stranger-" + version + ".json"), "w") as handle:
            handle.write(envelope(out, stranger, good))

        print("FIXTURE " + version + " " + str(payload["size"]) + " bytes, sha256 " + payload["sha256"][:16] + "...")

    for throwaway in ("signing.pem", "stranger.pem"):
        os.remove(os.path.join(out, throwaway))
    print("FIXTURES_WRITTEN " + out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

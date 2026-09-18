#!/usr/bin/env python3
"""#1305: write the manifest fixtures i1305_update_check.cpp reads.

Run it in the od-amd64:bullseye container -- testers/i1305_check.sh does, before the
check -- so that every signature here is produced by OpenSSL and not by anything in this
tree. That is the point: a verifier tested only against signatures its own code made is a
verifier tested against its own bugs.

Deterministic where it can be. The keys are regenerated on every run because ECDSA
signing is randomised anyway, so a fixture's bytes are not stable and pinning them would
be pinning nothing. What IS stable is the relationship between the files, which is what
the check asserts:

  anchor.hex     the signing key's public point, X||Y, 128 hex characters
  good.json      a manifest signed for the stable channel by that key
  tampered.json  good.json with ONE BYTE of the payload changed and the signature left
                 exactly as it was -- v9.9.9 becomes v9.9.8, so it is still a perfectly
                 well-formed manifest naming a real-looking version. A build with the
                 verification deleted reads it happily, which is the whole design of
                 #1305's criterion.
  stranger.json  the same payload signed by a DIFFERENT key: a valid signature that this
                 build has no business accepting
  beta.json      signed for the beta channel, to be asked for at the stable address
  matching.json  signed for stable, naming OD_FIXTURE_MATCHING_VERSION, so that "there is
                 no update" can be shown by the real program
  garbage.json   not an envelope at all
"""

import base64
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures1305")

PAYLOAD = {
    "channel": "stable",
    "version": "v9.9.9",
    "url": "https://github.com/Apaiko-Oy/OpenDartboard/releases/download/v9.9.9/opendartboard-v9.9.9.zip",
    "sha256": "5c8d1b4f3a9e0c27d6b8f41a72e5309ca4bd6e17f0928c35ab41d7e69f0c2b83",
    "size": 39845120,
    "minimumLauncher": "v1.0.0",
}


def run(*argv, stdin=None):
    return subprocess.run(argv, input=stdin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout


def keypair(name):
    """A throwaway P-256 key. Never a trust anchor, never committed, minted per run."""
    private = os.path.join(OUT, name + ".pem")
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


def sign(private, payload_bytes):
    message = os.path.join(OUT, "message.bin")
    with open(message, "wb") as handle:
        handle.write(payload_bytes)
    signature = run("openssl", "dgst", "-sha256", "-sign", private, message)
    os.remove(message)
    return signature


def envelope(private, payload_bytes):
    return json.dumps(
        {
            "payload": base64.b64encode(payload_bytes).decode(),
            "signature": base64.b64encode(sign(private, payload_bytes)).decode(),
        },
        indent=2,
    ) + "\n"


def write(name, contents):
    with open(os.path.join(OUT, name), "w") as handle:
        handle.write(contents)


def main():
    os.makedirs(OUT, exist_ok=True)
    ours, point = keypair("signing")
    stranger, _ = keypair("stranger")

    payload = json.dumps(PAYLOAD, separators=(",", ":")).encode()
    write("anchor.hex", point + "\n")
    write("good.json", envelope(ours, payload))
    write("stranger.json", envelope(stranger, payload))

    # One byte. The signature is the one from good.json, untouched.
    tampered_payload = payload.replace(b'"version":"v9.9.9"', b'"version":"v9.9.8"')
    if tampered_payload == payload or len(tampered_payload) != len(payload):
        raise SystemExit("the tamper changed nothing, or changed more than one byte")
    good = json.loads(open(os.path.join(OUT, "good.json")).read())
    write(
        "tampered.json",
        json.dumps({"payload": base64.b64encode(tampered_payload).decode(), "signature": good["signature"]}, indent=2)
        + "\n",
    )

    # A manifest naming whatever version the binary under test reports, so that the
    # "there is no update" answer can be shown by the real program rather than argued.
    matching = dict(PAYLOAD, version=os.environ.get("OD_FIXTURE_MATCHING_VERSION", "v0.0.0-match"))
    write("matching.json", envelope(ours, json.dumps(matching, separators=(",", ":")).encode()))

    beta = dict(PAYLOAD, channel="beta")
    write("beta.json", envelope(ours, json.dumps(beta, separators=(",", ":")).encode()))
    write("garbage.json", '{"not": "an envelope"}\n')

    for throwaway in ("signing.pem", "stranger.pem"):
        os.remove(os.path.join(OUT, throwaway))

    print("FIXTURES_WRITTEN " + OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())

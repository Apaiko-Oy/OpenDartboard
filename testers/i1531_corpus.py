#!/usr/bin/env python3
"""#1531: mint the negative corpus both signature arms are held to.

unrun-tester: this MINTS the corpus; it is not a check and it asserts nothing. Its output
is committed under testers/corpus1531/ and that is what the gate reads, on both arms, so
running this on every gate would replace the bytes the two arms are being held to -- which
is precisely the thing #1531 exists to stop. Run it by hand when a case is added.

#1337's decision, taken 2026-09-23: keep the hand-written P-256 arm, and hold both arms
to ONE corpus. This script is what mints it. Its output is COMMITTED, under
testers/corpus1531/, and that is the whole point rather than an economy -- read
testers/corpus1531/README.md for why one file of bytes beats two mintings.

    testers/i1531_corpus.py [outdir]

It needs openssl and nothing else. Every signature below is produced by OpenSSL rather
than by anything in this tree, which is #1305's rule and the reason it exists: a verifier
measured only against signatures its own code made is a verifier measured against its own
bugs. What this script writes by hand is only the DER ENVELOPE around r and s, because the
cases are about encodings OpenSSL would never produce.

THE KEY IS A THROWAWAY AND IS DESTROYED BEFORE THIS SCRIPT EXITS. It is never the release
key, it signs nothing anybody will ever install, and its public half is committed as the
corpus's own anchor -- which is exactly what lets the Windows job run this corpus on an
ordinary pull request with no secret of any kind.

RE-MINTING IS A DELIBERATE ACT. ECDSA signing is randomised, so a re-mint rewrites every
byte under testers/corpus1531/ and the diff is unreadable. Re-mint when a case is ADDED or
when a case's shape changes -- not to refresh it, because there is nothing in it that goes
stale: the curve does not move and neither does DER.
"""

import base64
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The order of the curve. Two cases are about this number exactly.
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

PAYLOAD = {
    "channel": "stable",
    "version": "v9.9.9",
    "url": "https://github.com/Apaiko-Oy/OpenDartboard/releases/download/v9.9.9/opendartboard-v9.9.9.zip",
    "sha256": "5c8d1b4f3a9e0c27d6b8f41a72e5309ca4bd6e17f0928c35ab41d7e69f0c2b83",
    "size": 39845120,
    "minimumLauncher": "v1.0.0",
}

OTHER_PAYLOAD = dict(PAYLOAD, version="v9.9.7")


def run(*argv, stdin=None):
    return subprocess.run(argv, input=stdin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout


def keypair(work, name):
    """A throwaway P-256 key and its public point as 128 hex characters."""
    private = os.path.join(work, name + ".pem")
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


def sign(work, private, payload_bytes):
    message = os.path.join(work, "message.bin")
    with open(message, "wb") as handle:
        handle.write(payload_bytes)
    signature = run("openssl", "dgst", "-sha256", "-sign", private, message)
    os.remove(message)
    return signature


# ---------------------------------------------------------------- DER, by hand
#
# Only the envelope. r and s themselves always come out of an OpenSSL signature.


def der_integer(value: bytes) -> bytes:
    """Canonical DER INTEGER: no leading zero unless the high bit needs one."""
    body = value.lstrip(b"\x00") or b"\x00"
    if body[0] & 0x80:
        body = b"\x00" + body
    return b"\x02" + bytes([len(body)]) + body


def der_sequence(body: bytes) -> bytes:
    if len(body) >= 0x80:
        raise SystemExit("a P-256 signature is never this long; the short form is the canonical one here")
    return b"\x30" + bytes([len(body)]) + body


def read_der(signature: bytes):
    """r and s out of an OpenSSL signature, as 32 bytes apiece."""
    if signature[0] != 0x30:
        raise SystemExit("openssl did not write a SEQUENCE")
    i = 2
    out = []
    for _ in range(2):
        if signature[i] != 0x02:
            raise SystemExit("openssl did not write an INTEGER where one belongs")
        length = signature[i + 1]
        value = signature[i + 2 : i + 2 + length]
        out.append(int.from_bytes(value, "big").to_bytes(32, "big"))
        i += 2 + length
    return out[0], out[1]


def envelope(payload_bytes: bytes, signature: bytes) -> str:
    return (
        json.dumps(
            {
                "payload": base64.b64encode(payload_bytes).decode(),
                "signature": base64.b64encode(signature).decode(),
            },
            indent=2,
        )
        + "\n"
    )


def on_curve(point_hex: str) -> bool:
    p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
    b = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
    x = int(point_hex[:64], 16)
    y = int(point_hex[64:], 16)
    return (y * y - (x * x * x - 3 * x + b)) % p == 0


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "corpus1531")
    work = os.path.join(out, ".mint")
    os.makedirs(work, exist_ok=True)

    ours, anchor = keypair(work, "signing")
    stranger, _ = keypair(work, "stranger")

    payload = json.dumps(PAYLOAD, separators=(",", ":")).encode()
    other_payload = json.dumps(OTHER_PAYLOAD, separators=(",", ":")).encode()

    # Sign until r's high bit is CLEAR, so that the leading-zero case below is the subtle
    # non-canonical encoding rather than the obvious 34-byte one. Which it is matters: a
    # 34-byte integer is refused by a length bound that has always been there, and would
    # prove nothing about canonical form.
    for _ in range(40):
        signature = sign(work, ours, payload)
        r, s = read_der(signature)
        if not (r[0] & 0x80):
            break
    else:
        raise SystemExit("forty signatures and every r had its high bit set, which is not chance")

    cases = []

    def case(name, expect, body, why, anchor_override="-"):
        with open(os.path.join(out, name + ".json"), "w") as handle:
            handle.write(body)
        cases.append((name, expect, anchor_override, why))

    # ---- the control. Without it the corpus proves only that the verifier says no to
    # everything, which `return false` also does.
    case(
        "control-valid",
        "accept",
        envelope(payload, signature),
        "THE CONTROL: OpenSSL signed these payload bytes with the corpus key",
    )

    # ---- r and s out of range. These reach the arithmetic: the DER is canonical and the
    # integers parse, so what refuses them is the verifier's own range check, or what lies
    # behind it.
    case("s-zero", "refuse", envelope(payload, der_sequence(der_integer(r) + der_integer(b"\x00"))), "s = 0")
    case(
        "s-order",
        "refuse",
        envelope(payload, der_sequence(der_integer(r) + der_integer(N.to_bytes(32, "big")))),
        "s = n, the order of the curve",
    )
    case("r-zero", "refuse", envelope(payload, der_sequence(der_integer(b"\x00") + der_integer(s))), "r = 0")
    case(
        "r-order",
        "refuse",
        envelope(payload, der_sequence(der_integer(N.to_bytes(32, "big")) + der_integer(s))),
        "r = n, the order of the curve",
    )

    # ---- a signature that is perfectly valid, under a key this build does not trust.
    case(
        "other-key",
        "refuse",
        envelope(payload, sign(work, stranger, payload)),
        "a VALID signature over these very bytes, made by a different key",
    )

    # ---- non-canonical DER. Neither of these is a forgery; both are a second spelling of
    # one signature, and a reader that takes two spellings is a reader whose bytes are not
    # the thing it verified.
    padded = b"\x02" + bytes([33]) + b"\x00" + r
    case(
        "der-leading-zero-pad",
        "refuse",
        envelope(payload, der_sequence(padded + der_integer(s))),
        "non-canonical DER: r padded with a leading zero its high bit does not call for",
    )
    canonical_body = der_integer(r) + der_integer(s)
    overlong = b"\x30" + b"\x81" + bytes([len(canonical_body)]) + canonical_body
    case(
        "der-overlong-length",
        "refuse",
        envelope(payload, overlong),
        "non-canonical DER: the SEQUENCE length written in the long form it does not need",
    )

    # ---- truncation, on each side of the envelope.
    case(
        "signature-truncated",
        "refuse",
        envelope(payload, signature[:-8]),
        "the last eight bytes of a real signature are gone",
    )
    case(
        "payload-truncated",
        "refuse",
        envelope(payload[:-10], signature),
        "the last ten bytes of the signed payload are gone, signature untouched",
    )

    # ---- the payload moved, and the signature moved. Two different mistakes that look
    # the same from a distance.
    moved = payload.replace(b'"version":"v9.9.9"', b'"version":"v9.9.8"')
    if moved == payload or len(moved) != len(payload):
        raise SystemExit("the tamper changed nothing, or changed more than one byte")
    case("payload-byte-moved", "refuse", envelope(moved, signature), "ONE byte of the payload, signature untouched")
    case(
        "signature-over-another-payload",
        "refuse",
        envelope(payload, sign(work, ours, other_payload)),
        "the corpus key's own signature -- over a DIFFERENT payload",
    )

    # ---- and the anchor itself. This one is about what a build trusts rather than about
    # what arrived, so it carries its own anchor column.
    bad_point = anchor[:64] + ("%064x" % (int(anchor[64:], 16) ^ 1))
    if on_curve(bad_point) or not on_curve(anchor):
        raise SystemExit("the off-curve point is on the curve, or the real anchor is not")
    case(
        "anchor-not-on-curve",
        "refuse",
        envelope(payload, signature),
        "a valid manifest, read by a build whose anchor is not a point on P-256",
        anchor_override=bad_point,
    )

    with open(os.path.join(out, "anchor.hex"), "w") as handle:
        handle.write(anchor + "\n")
    with open(os.path.join(out, "cases.tsv"), "w") as handle:
        handle.write("# name\texpect\tanchor\twhy    -- see README.md; minted by testers/i1531_corpus.py\n")
        for name, expect, anchor_override, why in cases:
            handle.write("%s\t%s\t%s\t%s\n" % (name, expect, anchor_override, why))

    for throwaway in os.listdir(work):
        os.remove(os.path.join(work, throwaway))
    os.rmdir(work)

    print("CORPUS_WRITTEN %s (%d cases, %d of them refusals)" % (out, len(cases), len(cases) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())

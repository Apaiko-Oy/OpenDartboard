#!/usr/bin/env python3
"""#1532: the manifests a Windows board is offered on its way from one version to the next.

unrun-tester: this MINTS; it asserts nothing. It is run by .github/workflows/update-journey.yml
on a windows-2022 runner, over zips that job has just written, and by nothing else -- the
manifests it writes name the SHA-256 of those very zips, which exist only on that runner.

    testers/i1532_mint.py <outdir> <zipdir> <base-url> <B> <C> <BAD> <minimumLauncher>

WHY THIS IS MINTED IN THE JOB AND NOT AN `accept` ROW IN testers/corpus1531 -- which is what
that directory's README asks the next reader to do, and which was measured and could not
be done. A manifest signs the artefact's sha256 and size. The artefact here is a zip around
an .exe that cl.exe compiled on the runner a minute ago, and cl.exe's output is not
byte-stable across the runner image's toolset updates, so a committed manifest would name a
zip nobody can build again -- or the zip, with an .exe in it, would have to be committed
too. And #1531's key cannot sign anything new: its private half was destroyed by design.
The reason #1531 committed its bytes was that TWO arms on two machines must be asked about
the same bytes. Here there is ONE arm -- the launcher's CNG, on one runner -- so a per-run
mint loses nothing that commitment would have bought.

What IS reused is #1531's minting, not rewritten: the throwaway key pair and the OpenSSL
signature come from testers/i1531_corpus.py's own `keypair` and `sign`, imported below, and
the envelope is the one both arms read. That is the half "Blocked by #1531" was about.

THE KEYS ARE THROWAWAYS, destroyed before this exits. The anchor written to anchor.hex is
compiled into a TEST launcher that the job builds for itself and never uploads.

What it writes, every one signed by the throwaway key unless it says otherwise:

    good-<B>.json       the real size, the real digest: the update that must happen
    digest-<B>.json     the real size, one hex digit of the digest changed
    size-<B>.json       the real digest, the size one byte larger
    stranger-<B>.json   good-<B>'s very payload, signed by a DIFFERENT key
    good-<BAD>.json     a real manifest for a release whose detector will not start
    good-<C>.json       a real manifest offered to a quick restart, which must not look
"""

import base64
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import i1531_corpus  # noqa: E402  -- #1531's minting, reused rather than written twice


def envelope(payload_bytes, signature):
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


def main():
    if len(sys.argv) != 8:
        raise SystemExit(__doc__)
    out, zips, base, b, c, bad, minimum = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, ".mint")
    os.makedirs(work, exist_ok=True)

    ours, anchor = i1531_corpus.keypair(work, "signing")
    stranger, stranger_anchor = i1531_corpus.keypair(work, "stranger")
    if stranger_anchor == anchor:
        raise SystemExit("two throwaway keys came out the same, which is not chance")

    def payload_for(version):
        name = "opendartboard-" + version + ".zip"
        raw = open(os.path.join(zips, name), "rb").read()
        return {
            "channel": "stable",
            "version": version,
            "url": base.rstrip("/") + "/rel/" + name,
            "sha256": hashlib.sha256(raw).hexdigest(),
            "size": len(raw),
            "minimumLauncher": minimum,
        }

    def write(name, payload, key):
        body = json.dumps(payload, separators=(",", ":")).encode()
        with open(os.path.join(out, name + ".json"), "w") as handle:
            handle.write(envelope(body, i1531_corpus.sign(work, key, body)))
        print("MINTED %-18s version %s size %d sha256 %s..." % (name, payload["version"], payload["size"],
                                                                payload["sha256"][:12]))

    good = payload_for(b)
    write("good-" + b, good, ours)

    first = good["sha256"][0]
    write("digest-" + b, dict(good, sha256=("0" if first != "0" else "1") + good["sha256"][1:]), ours)
    write("size-" + b, dict(good, size=good["size"] + 1), ours)
    write("stranger-" + b, good, stranger)
    write("good-" + bad, payload_for(bad), ours)
    write("good-" + c, payload_for(c), ours)

    with open(os.path.join(out, "anchor.hex"), "w") as handle:
        handle.write(anchor + "\n")

    for throwaway in os.listdir(work):
        os.remove(os.path.join(work, throwaway))
    os.rmdir(work)
    print("MINT_WRITTEN " + out + " (both private keys destroyed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""#1796: real Linux release archives, and the signed manifests that name them.

#1306's fixtures are zips, the shape the Windows job's Compress-Archive writes. A Pi
installs the tar.gz release.yml's deb job writes since #1796 -- the detector, the
launcher, LICENSE and BUILD-INFO.txt at the top, the detector's entry carrying mode 0755
-- so this writes that shape around a stub that says which version it is, and signs a
manifest for it under the name Turnaus serves a platform's manifest at. The signing, the
keys and the envelope are i1306_fixtures.py's own functions, imported; nothing about a
manifest is different between the platforms except the artefact it names.

    testers/i1796_fixtures.py <outdir> <stubdir> <manifest-name> <version> [<version> ...]

<stubdir> holds one compiled stub per version, named `stub-<version>`. For each it writes
`rel/opendartboard-<version>-linux-arm64.tar.gz` and `<manifest-name>-<version>.json`
signed for the stable channel by a key minted on this run, whose anchor is `anchor.hex`.
`<manifest-name>` is the leaf the harness serves it under -- `stable-linux-arm64` -- so a
reader of the harness's access log and a reader of this directory see the same word.
"""

import hashlib
import io
import json
import os
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i1306_fixtures import envelope, keypair  # noqa: E402

BASE = "https://releases.example.invalid/rel/"
MINIMUM_LAUNCHER = "v1.0.0"


def write_archive(path, detector_binary, version):
    """The deb job's shape: the detector 0755 at the top, the three files beside it."""
    with open(detector_binary, "rb") as handle:
        body = handle.read()
    texts = {
        "opendartboard-launcher": b"#!/bin/sh\nexit 0\n",
        "LICENSE": b"GPL-3.0, as the real package carries it.\n",
        "BUILD-INFO.txt": ("OpenDartboard fixture for testers/i1796_check.sh\nversion:    " + version + "\n").encode(),
    }
    with tarfile.open(path, "w:gz") as archive:
        info = tarfile.TarInfo("opendartboard")
        info.size = len(body)
        info.mode = 0o755
        archive.addfile(info, io.BytesIO(body))
        for name, text in texts.items():
            info = tarfile.TarInfo(name)
            info.size = len(text)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(text))


def main():
    argv = sys.argv[1:]
    if len(argv) < 4:
        raise SystemExit(__doc__)
    out, stubs, manifest_name, versions = argv[0], argv[1], argv[2], argv[3:]
    releases = os.path.join(out, "rel")
    os.makedirs(releases, exist_ok=True)

    ours, point = keypair(out, "signing")
    with open(os.path.join(out, "anchor.hex"), "w") as handle:
        handle.write(point + "\n")

    for version in versions:
        name = "opendartboard-" + version + "-linux-arm64.tar.gz"
        archive = os.path.join(releases, name)
        stub = os.path.join(stubs, "stub-" + version)
        if not os.path.exists(stub):
            raise SystemExit("no stub for " + version + " at " + stub)
        write_archive(archive, stub, version)
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
        with open(os.path.join(out, manifest_name + "-" + version + ".json"), "w") as handle:
            handle.write(envelope(out, ours, good))
        print("FIXTURE " + version + " " + str(payload["size"]) + " bytes, sha256 " + payload["sha256"][:16] + "...")

    os.remove(os.path.join(out, "signing.pem"))
    print("FIXTURES_WRITTEN " + out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""#1531: plant one named defect in a COPY of src/, for testers/i1531_inside.sh.

    i1531_plant.py <plant> <source root>

Every plant asserts the text it is about is where it expects it, and fails loudly if it is
not -- a plant that silently applied nothing would turn a corpus green and read as the
corpus being insensitive, which is the exact conclusion this file exists to let somebody
draw honestly.

Nothing here touches the worktree: the caller copies src/ to /tmp first.
"""

import os
import sys

P256 = "update/p256_verify.hpp"
MANIFEST = "update/manifest.hpp"


def swap(root, relative, was, now, what):
    path = os.path.join(root, relative)
    with open(path) as handle:
        text = handle.read()
    if text.count(was) != 1:
        raise SystemExit(
            "the plant expected exactly one of this in %s and found %d:\n%s" % (relative, text.count(was), was)
        )
    with open(path, "w") as handle:
        handle.write(text.replace(was, now))
    print("PLANTED: " + what)


PLANTS = {}


def plant(name):
    def register(function):
        PLANTS[name] = function
        return function

    return register


@plant("s-range")
def s_range(root):
    """#1531's own mutation: the range check on s, and only on s."""
    swap(
        root,
        P256,
        "if (isZero(rr) || isZero(ss) || compare(rr, n) >= 0 || compare(ss, n) >= 0)",
        "if (isZero(rr) || compare(rr, n) >= 0)",
        "the hand-written arm no longer range-checks s",
    )


@plant("off-curve")
def off_curve(root):
    swap(
        root,
        P256,
        "        if (!onCurve(qx, qy))\n        {\n            return false;\n        }\n",
        "",
        "the hand-written arm no longer asks whether the anchor is on the curve",
    )


@plant("canonical-der")
def canonical_der(root):
    """Both halves of #1531's own repair, back out. This is the plant the corpus sees."""
    swap(
        root,
        MANIFEST,
        "            if (der.size() < 8 || der.size() > 72 || uint8_t(der[0]) != 0x30)\n"
        "            {\n"
        "                return false;\n"
        "            }\n"
        "            // The short form, stating exactly what is really there.\n"
        "            if (uint8_t(der[1]) >= 0x80 || size_t(uint8_t(der[1])) + 2 != der.size())\n"
        "            {\n"
        "                return false;\n"
        "            }\n"
        "            size_t i = 2;\n",
        "            if (der.size() < 8 || uint8_t(der[0]) != 0x30)\n"
        "            {\n"
        "                return false;\n"
        "            }\n"
        "            uint8_t sequence_length = uint8_t(der[1]);\n"
        "            size_t i = (sequence_length < 0x80) ? 2 : 2 + size_t(sequence_length & 0x7f);\n",
        "the SEQUENCE length may be written in the long form again",
    )
    swap(
        root,
        MANIFEST,
        "                if (bytes[0] & 0x80)\n"
        "                {\n"
        "                    return false; // a negative INTEGER, and r and s are unsigned\n"
        "                }\n"
        "                if (length > 1 && bytes[0] == 0x00 && !(bytes[1] & 0x80))\n"
        "                {\n"
        "                    return false; // a leading zero the high bit does not call for\n"
        "                }\n",
        "",
        "and an integer may carry a leading zero its high bit does not call for",
    )


@plant("x-comparison")
def x_comparison(root):
    swap(
        root,
        P256,
        "        return equal(x, rr);",
        "        (void)x;\n        return true;",
        "the hand-written arm stops comparing the recovered x to r",
    )


@plant("verification-deleted")
def verification_deleted(root):
    """#1305's mutation, asked of #1531's corpus instead of #1305's two fixtures."""
    swap(
        root,
        MANIFEST,
        "        if (!verified)\n        {\n            return refuse(Refusal::NotVerified);\n        }\n\n",
        "",
        "manifest.hpp no longer refuses a manifest nothing verified",
    )


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in PLANTS:
        raise SystemExit("usage: i1531_plant.py <%s> <source root>" % "|".join(sorted(PLANTS)))
    PLANTS[sys.argv[1]](sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())

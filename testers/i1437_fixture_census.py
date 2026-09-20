#!/usr/bin/env python3
"""#1437: a fixture is measured whole, or the clip that was left out is named.

WHAT THIS IS ABOUT. `mocks/rig-20260918/` holds three clips. #1416 measured ring identity
across both fixtures on the integration branch and published `rig doubles 27/27, --,
35/35`: the middle camera produced nothing, the census printed a gap where its number
should have been, and NOTHING WENT RED. Every fixture-wide figure taken on that tree is
one camera short of what its author believes they measured, and there is no way to tell
from the figure which ones.

That is this repository's recurring shape -- an assertion that passes by not being asked,
a sweep over a database holding nothing -- reached through a fixture rather than through a
test. The cure is the same one #1371 and #1389 used: a census, asserted EMPTY outright,
with a marker beside the line as the only hatch and no list of files allowed to fail.

THE RULE, and it is narrower than "name every clip". A file that names ONE clip of a
fixture is using one clip on purpose: `i1318_make_source.cpp` builds footage from
`mocks/cam_1.mp4`, `i1339_scaled_footage.cpp` scales it, and demanding its two siblings
would be demanding noise. What cannot be right is a file that measures SEVERAL clips of
one fixture and leaves one out -- that is a fixture-wide measurement with a hole in it,
and it is exactly the shape #1416's census had. So:

    a file naming MORE THAN ONE clip of a fixture must name them all.

Measured on this tree the day it was written: 0 files name a strict subset of two, so the
census is empty and can be asserted so. The counts it is empty against are
mocks/cam_{1,2,3} at 75/59/55 references and rig-20260918/cam_{1,2,3} at 12/12/11 -- the
asymmetry being the single-clip generators above, which this rule deliberately does not
read.

WHAT IT DELIBERATELY DOES NOT READ, said as numbers rather than as silence:

  * A file naming exactly one clip of a fixture. 20 such references on this tree, every
    one of them a generator or a single-camera phase. Demanding three would be demanding
    two clips nobody reads.
  * A loop. `mocks/cam_$i.mp4` and `mocks/rig-20260918/cam_$i.mp4` -- 8 and 2 sites --
    name every clip by construction, so a file containing one is treated as naming all of
    that fixture and is never reported.
  * Whether the clips a file names were actually READ. A path in a comment counts the
    same as a path on a command line. This census is about what a file SAYS; the harness
    beside it is about what the detector ANSWERS, and neither can do the other's half.

    testers/i1437_fixture_census.py [tree-root]

Exit 0 when the census is empty, 1 when it is not.
"""
import os
import re
import sys

MARKER = "fixture-subset-exempt:"
CLIP = re.compile(r"mocks/(?:([A-Za-z0-9_.-]+)/)?cam_([0-9]+)\.mp4")
LOOP = re.compile(r"mocks/(?:([A-Za-z0-9_.-]+)/)?cam_[$\{]")
READ = (".sh", ".py", ".cpp", ".hpp", ".h", ".md", ".mjs", ".ps1", "Makefile")


def fixtures(root):
    """Every directory under mocks/ that holds cam_<n>.mp4, and the indices it holds."""
    found = {}
    base = os.path.join(root, "mocks")
    for dirpath, _dirnames, filenames in os.walk(base):
        held = set()
        for name in filenames:
            m = re.fullmatch(r"cam_([0-9]+)\.mp4", name)
            if m:
                held.add(int(m.group(1)))
        if held:
            rel = os.path.relpath(dirpath, base)
            found["" if rel == "." else rel] = held
    return found


def files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "mocks", "models", "dist")]
        for name in filenames:
            if name.endswith(READ) or name == "Makefile":
                yield os.path.join(dirpath, name)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    held = fixtures(root)
    if not held:
        print("FAIL no fixture found under mocks/ -- this census would pass by having "
              "nothing to read, which is the failure it exists to catch")
        return 1

    print("fixtures:")
    for fix, clips in sorted(held.items()):
        print(f"  mocks/{fix + '/' if fix else ''} holds {len(clips)} clips: "
              + ", ".join(f"cam_{i}" for i in sorted(clips)))
    print()

    faults = []
    for path in sorted(files(root)):
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if MARKER in text:
            continue
        looped = {m.group(1) or "" for m in LOOP.finditer(text)}
        named = {}
        for m in CLIP.finditer(text):
            fix = m.group(1) or ""
            named.setdefault(fix, {})[int(m.group(2))] = text.count("\n", 0, m.start()) + 1
        for fix, seen in named.items():
            if fix in looped or fix not in held:
                continue
            if len(seen) <= 1:
                continue
            missing = held[fix] - set(seen)
            if missing:
                where = min(seen.values())
                faults.append(
                    (os.path.relpath(path, root), where, fix, sorted(seen), sorted(missing)))

    if not faults:
        print("the census is empty: no file measures part of a fixture and calls it the fixture")
        return 0

    print(f"{len(faults)} file(s) name several clips of a fixture and leave one out:")
    for path, line, fix, seen, missing in faults:
        name = f"mocks/{fix + '/' if fix else ''}"
        print(f"  {path}:{line}: names {name} "
              + ", ".join(f"cam_{i}" for i in seen)
              + " but not " + ", ".join(f"cam_{i}" for i in missing))
    print()
    print("A fixture-wide measurement that leaves a clip out is a measurement one camera")
    print("short of what its reader will believe it is. Name the clip, or write")
    print(f"`{MARKER} <why>` in the file and say why this one is not fixture-wide.")
    return 1


if __name__ == "__main__":
    sys.exit(main())

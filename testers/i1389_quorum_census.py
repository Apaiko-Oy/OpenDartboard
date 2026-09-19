#!/usr/bin/env python3
"""#1389: the census that refuses a FOURTH copy of the camera quorum.

ADR-0081 §2 is about three copies of one number, each set by a different slice and none
of them aware of the others. Removing the three and leaving nothing behind would buy one
afternoon: the fourth arrives the next time somebody writes `if (cameras < 2)` in a stage
that has never counted cameras before, and nothing in this repository would say a word.
A constant three places happen to read today is not a rule. This is the rule.

It is #1371's shape, one level down: a census of every site in `src/` that compares a
count of cameras against a number, asserted EMPTY outright, with a marker beside the line
as the only hatch and no list of files allowed to fail.

WHAT IT READS. Every `.cpp`, `.hpp` and `.h` under `src/`, with comments and string
literals blanked out first -- these files are more prose than code and every refusal
sentence in them carries digits, so a scanner that read them would report a hundred
sentences and nothing else. A comparison counts when one side is an integer literal and
the other names a camera count: an identifier holding `camera`, `seeing`, `voting`,
`voters`, `answering` or `quorum`. An assignment of an integer literal to such a name
counts too, because that is the shape all three of the original quorums had.

WHAT IT DELIBERATELY DOES NOT READ, said as numbers rather than as silence, because a
check whose scope is narrower than its claim is the defect this repository keeps buying:

  * A comparison against 0. `cameras_answering > 0`, `camera_index < 0`, `seeing == 0` --
    emptiness and index guards, not quorums. 6 sites on a clean tree.
  * A comparison between two identifiers. `goes_clean >= quorum`, `seeing >=
    cameras_that_must_see`: those are the sites that READ the quorum, and they are what
    this census wants to see rather than what it wants to refuse. They are checked the
    other way round, below.
  * A fourth site that counts cameras into a variable this vocabulary does not name --
    `if (n < 2)`. Nothing here can see that, and saying so is the honest form. What
    narrows it is that the three real sites all named their count, and so does every
    count in this tree today.

THE OTHER HALF. A census of literals cannot tell you the three that are left agree, so
the three named sites are also read forward: each must resolve its threshold through
`camera_quorum::`, and `kCameras` must be written exactly once in the whole tree.

    python3 testers/i1389_quorum_census.py [tree-root]

Exit 0 when the census is empty and the three sites read the one number; 1 otherwise.
"""
import os
import re
import sys

VOCAB = re.compile(r"camera|seeing|voting|voters|answering|quorum", re.I)
CMP = re.compile(
    r"(?:([A-Za-z_][A-Za-z_0-9:.]*(?:->[A-Za-z_0-9]+)?)\s*(<=|>=|==|!=|<|>)\s*([0-9]+)\b)"
    r"|(?:\b([0-9]+)\s*(<=|>=|==|!=|<|>)\s*([A-Za-z_][A-Za-z_0-9:.]*(?:->[A-Za-z_0-9]+)?))"
)
ASSIGN = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*=\s*([0-9]+)\s*[;,)]")
MARKER = "camera-quorum-exempt:"

# The three sites ADR-0081 §2 names, and the file each one lives in. A site that stops
# resolving through camera_quorum:: is the drift this issue exists to prevent, and it is
# a different failure from a fourth site appearing -- so it is reported separately.
READERS = {
    "calibration admission": (
        "src/detector/geometry/geometry_detector.cpp",
        re.compile(r"cameras_that_must_see\s*=\s*camera_quorum::cameras\(\)"),
    ),
    "the dart event's board census": (
        "src/detector/geometry/detection/motion_processing.hpp",
        re.compile(r"int\s+quorum\s*=\s*camera_quorum::cameras\(\)"),
    ),
    "the state vote's floor": (
        "src/detector/geometry/detection/dart_processing.hpp",
        re.compile(r"min_cameras_to_move_the_board\s*=\s*camera_quorum::cameras\(\)"),
    ),
}
HOME = "src/detector/geometry/camera_quorum.hpp"
DEFINITION = re.compile(r"\bkCameras\s*=\s*[0-9]+")


def blanked(text):
    """The file with comments and string/char literals replaced by spaces, lines kept."""
    out = []
    i = 0
    n = len(text)
    state = 0  # 0 code, 1 line comment, 2 block comment, 3 string, 4 char
    while i < n:
        c = text[i]
        if state == 0:
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                state = 1
                out.append("  ")
                i += 1
            elif c == "/" and i + 1 < n and text[i + 1] == "*":
                state = 2
                out.append("  ")
                i += 1
            elif c == '"':
                state = 3
                out.append(" ")
            elif c == "'":
                state = 4
                out.append(" ")
            else:
                out.append(c)
        elif state == 1:
            out.append("\n" if c == "\n" else " ")
            if c == "\n":
                state = 0
        elif state == 2:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                state = 0
                out.append("  ")
                i += 1
            else:
                out.append("\n" if c == "\n" else " ")
        elif state == 3:
            if c == "\\":
                out.append("  ")
                i += 1
            elif c == '"':
                state = 0
                out.append(" ")
            else:
                out.append("\n" if c == "\n" else " ")
        elif state == 4:
            if c == "\\":
                out.append("  ")
                i += 1
            elif c == "'":
                state = 0
                out.append(" ")
            else:
                out.append(" ")
        i += 1
    return "".join(out)


def exempted(raw_lines, line_no):
    """The marker, on the statement's own line or on any comment line directly above it."""
    if MARKER in raw_lines[line_no - 1]:
        return True
    i = line_no - 2
    while i >= 0:
        stripped = raw_lines[i].strip()
        if not stripped.startswith("//") and not stripped.startswith("*"):
            return False
        if MARKER in raw_lines[i]:
            return True
        i -= 1
    return False


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    root = os.path.abspath(root)
    src = os.path.join(root, "src")
    if not os.path.isdir(src):
        print("i1389 census: no src/ under %s" % root)
        return 2

    census = []
    exempt = 0
    against_zero = 0
    definitions = []

    for dirpath, _dirs, files in os.walk(src):
        for name in sorted(files):
            if not name.endswith((".cpp", ".hpp", ".h")):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)
            raw = open(path, encoding="utf-8", errors="replace").read()
            raw_lines = raw.split("\n")
            code_lines = blanked(raw).split("\n")
            for line_no, line in enumerate(code_lines, 1):
                found = []
                for m in CMP.finditer(line):
                    ident = m.group(1) or m.group(6)
                    literal = m.group(3) or m.group(4)
                    if not ident or not VOCAB.search(ident):
                        continue
                    if literal == "0":
                        against_zero += 1
                        continue
                    found.append(m.group(0).strip())
                for m in ASSIGN.finditer(line):
                    if not VOCAB.search(m.group(1)):
                        continue
                    if m.group(2) == "0":
                        against_zero += 1
                        continue
                    if DEFINITION.search(m.group(0)):
                        definitions.append((rel, line_no, m.group(0).strip()))
                        continue
                    found.append(m.group(0).strip().rstrip(";,)"))
                for what in found:
                    if exempted(raw_lines, line_no):
                        exempt += 1
                    else:
                        census.append((rel, line_no, what))

    failed = 0
    print("#1389: every site in src/ that measures a camera count against a number")
    print("")
    if census:
        failed = 1
        print("FAIL %d site(s) compare a count of cameras against a literal and do not read" % len(census))
        print("     camera_quorum::cameras(). Read it, or say beside the line why that number")
        print("     is not the quorum, with a `%s <why>` comment." % MARKER)
        for rel, line_no, what in census:
            print("       %s:%d   %s" % (rel, line_no, what))
    else:
        print("OK   the census is empty: no site in src/ holds a camera count against a")
        print("     literal of its own")
    print("     (%d exempted by name; %d comparisons against 0 are not quorums and are not read)"
          % (exempt, against_zero))
    print("")

    if len(definitions) == 1 and definitions[0][0].replace("\\", "/") == HOME:
        print("OK   the number is written once, at %s:%d -- %s"
              % (definitions[0][0], definitions[0][1], definitions[0][2]))
    else:
        failed = 1
        print("FAIL the quorum must be written exactly once, in %s. Found %d:"
              % (HOME, len(definitions)))
        for rel, line_no, what in definitions:
            print("       %s:%d   %s" % (rel, line_no, what))
    print("")

    for label, (rel, pattern) in sorted(READERS.items()):
        path = os.path.join(root, rel)
        text = open(path, encoding="utf-8", errors="replace").read() if os.path.exists(path) else ""
        if pattern.search(text):
            print("OK   %s reads it (%s)" % (label, rel))
        else:
            failed = 1
            print("FAIL %s no longer resolves its threshold through camera_quorum:: (%s)"
                  % (label, rel))

    print("")
    print("QUORUM_CENSUS_%s" % ("FAILED" if failed else "OK"))
    return failed


if __name__ == "__main__":
    sys.exit(main())

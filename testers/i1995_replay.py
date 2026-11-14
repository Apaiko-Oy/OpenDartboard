"""turnaus#1995: a kept window replays through the current binary.

mocks/rig-20260918 is pushed at the Turnaus stub once with OD_KEEP_WINDOWS set (#1366's Stub
and Board, as i1991_rig.py), and the directory it wrote is then read again by the same
binary -- `opendartboard --replay-windows <directory>` -- with no camera and no stub. What a
replay must reproduce is the body the stub counted for the same reference, and five things
of it are held, not one: the sector, the path, the cameras used, and the board radius and
angle to the four places a body carries. A sector alone would pass a replay that found its
tip three pixels away.

  replay     every kept dart reads as it was pushed (bar at most the last push, which may
             still be with the writer when the run ends -- i1991_rig.py's allowance), every
             line says what a replay cannot reproduce, and the replay wrote nothing
  layout     a copy of the directory whose calibration STATES another record size: every
             dart is refused `calibration-refused`, and the refusal names the stated size
             and this build's -- both read from the run, neither written here
  picture    a copy with one dart's settled picture removed: that dart is refused
             `picture-missing` by name and every other dart reads as before

Then two mutations of the detector, each made on a copy of the tree inside the container
(/app there is the copy; the checkout is mounted read-only at /tree), rebuilt, and replayed
over the SAME kept directory. Each prediction is printed before its run.

  M1  closeKeptWindow no longer seats the working background.
      PREDICTION: the replay is red; the dart named is the FIRST dart pushed on a board that
      already held one (its fresh figure is then the cumulative one, holding both darts), in
      radius and angle at least; every dart whose account names no working background reads
      exactly as pushed.
  M2  window_replay.hpp no longer holds the calibration's stated record size to the struct.
      PREDICTION: `layout` is red -- the altered file is read instead of refused, no dart is
      `calibration-refused`, and `replay` on the unaltered directory stays green.

I1995_REUSE=1 reads the kept directory and the pushes a previous run left under /out instead
of running the rig again (a developer's shortcut; the gate never sets it).
OD_WINDOW_UNIT=cycles is pinned on the board's own environment (#1826).
"""
import glob
import json
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1366_position_check as rig  # noqa: E402

check = rig.check
OUT = os.environ.get("I1995_OUT", "/out")
FIELDS = ("sector", "path", "cameras", "radius", "angle")
ALWAYS = ("reversion-clock", "before-close", "unpublished-windows")
SEAT = "            working_backgrounds[i] = k.fresh;\n"
SIZE_RULE = "if (header.record_bytes != (uint32_t)sizeof(DartboardCalibration) && !before_the_refit)"


def as_pushed(body):
    return {"sector": body["sector"], "path": body.get("path", "-"),
            "cameras": ",".join(str(c) for c in body.get("cameras_used", [])) or "-",
            "radius": "%.4f" % body["board_radius"] if body.get("board_radius") is not None else "none",
            "angle": "%.4f" % body["board_angle"] if body.get("board_angle") is not None else "none"}


def replay(binary, directory, switches, label):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": rig.WORK}
    env.update(switches)
    done = subprocess.run([binary, "--replay-windows", directory], cwd=rig.WORK, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=900)
    with open(os.path.join(OUT, label + ".out"), "w") as fh:
        fh.write(done.stdout)
        fh.write(done.stderr)
    lines, notes = {}, []
    for line in done.stdout.splitlines():
        if line.startswith("I1991 REPLAY ref="):
            f = dict(x.split("=", 1) for x in line.split()[2:] if "=" in x)
            lines[f["ref"]] = f
        elif line.startswith("I1991 REPLAY-"):
            notes.append(line)
    return done.returncode, lines, notes


def differences(pushed, lines, refs):
    out = []
    for ref in refs:
        got = lines.get(ref)
        want = as_pushed(pushed[ref])
        if got is None:
            out.append((ref, "no REPLAY line"))
            continue
        moved = ["%s %s->%s" % (k, want[k], got.get(k)) for k in FIELDS if got.get(k) != want[k]]
        if moved:
            out.append((ref, "; ".join(moved)))
    return out


def tree_digest(directory):
    return sorted((os.path.relpath(p, directory), os.path.getsize(p), os.path.getmtime(p))
                  for p in glob.glob(os.path.join(directory, "**", "*"), recursive=True) if os.path.isfile(p))


def keep_the_rig():
    """One run of the rig with the switch set: the kept directory and what the stub counted, by reference."""
    kept = os.path.join(OUT, "windows")
    saved = os.path.join(OUT, "pushed.json")
    if os.environ.get("I1995_REUSE") == "1" and os.path.isdir(kept) and os.path.exists(saved):
        print("keep: REUSING %s (I1995_REUSE=1) -- the rig is not run" % kept, flush=True)
        with open(saved) as fh:
            return kept, json.load(fh)
    port = 18995
    stub = rig.Stub("keep", port)
    board = rig.Board("keep", port)
    directory = os.path.join(board.dir, "windows")
    board.env["OD_WINDOW_UNIT"] = "cycles"
    board.env["OD_KEEP_WINDOWS"] = directory
    check(board.pair("--pair", rig.CLUB) == 0, "keep: the board pairs to its club")
    rc = board.run(rig.RIG_CYCLES, rig.RIG)
    check(rc == 0, "keep: the run ends normally (rc=%s)" % rc)
    counted = [e["body"] for e in stub.events("counted")]
    stub.stop()
    shutil.rmtree(kept, ignore_errors=True)
    shutil.copytree(directory, kept)
    for name in ("run.out", "run.err"):
        shutil.copy(os.path.join(board.dir, name), os.path.join(OUT, "keep-" + name))
    with open(saved, "w") as fh:
        json.dump(counted, fh)
    return kept, counted


def mutant(label, path, old, new):
    """The copy of the tree at /app, one line changed, rebuilt. The binary, or None."""
    target = os.path.join("/app", path)
    shutil.copy(os.path.join(rig.APP, path), target)  # from the checkout: the last mutation is undone
    with open(target) as fh:
        text = fh.read()
    if not check(text.count(old) == 1, "%s: the line it rewrites is in %s exactly once" % (label, path)):
        return None
    with open(target, "w") as fh:
        fh.write(text.replace(old, new))
    built = subprocess.run("cmake --build /app/build --target opendartboard -- -j2 --no-print-directory",
                           shell=True, capture_output=True, text=True, timeout=1500)
    with open(os.path.join(OUT, label + "-build.log"), "w") as fh:
        fh.write(built.stdout + built.stderr)
    if not check(built.returncode == 0, "%s: the mutant builds" % label):
        return None
    out = os.path.join(rig.WORK, "opendartboard-" + label)
    shutil.copy("/app/build/opendartboard", out)
    shutil.copy(os.path.join(rig.APP, path), target)
    return out


def altered_layout(kept, where):
    """A copy whose calibration states a record 8 bytes longer than it says now. Returns (was, stated)."""
    shutil.rmtree(where, ignore_errors=True)
    shutil.copytree(kept, where)
    was = None
    for path in glob.glob(os.path.join(where, "calibration", "*.dat")):
        with open(path, "r+b") as fh:
            head = bytearray(fh.read(16))
            was = struct.unpack_from("<I", head, 8)[0]
            struct.pack_into("<I", head, 8, was + 8)
            fh.seek(0)
            fh.write(head)
    return was, (was + 8 if was is not None else None)


def layout_case(binary, kept, switches, label):
    """(every dart refused for its calibration and the sizes named, text)"""
    where = os.path.join(rig.WORK, label)
    was, stated = altered_layout(kept, where)
    rc, lines, notes = replay(binary, where, switches, label)
    mine = [n for n in notes if n.startswith("I1991 REPLAY-OF")]
    build_bytes = mine[0].split("calibration_record_bytes=")[1].split()[0] if mine else "?"
    named = [n for n in notes if n.startswith("I1991 REPLAY-REFUSED calibration=")
             and ("states records of %s bytes" % stated) in n and ("DartboardCalibration is %s bytes" % build_bytes) in n]
    refused = [r for r, f in lines.items() if f.get("reason") == "calibration-refused" and f.get("path") == "refused"]
    ok = rc == 1 and bool(named) and len(lines) > 0 and len(refused) == len(lines)
    return ok, ("rc=%s, %d of %d darts calibration-refused, the file said %s and was made to state %s, this build's is %s, "
                "named: %s" % (rc, len(refused), len(lines), was, stated, build_bytes, "yes" if named else "NO"))


def main():
    os.makedirs(OUT, exist_ok=True)
    kept, counted = keep_the_rig()
    pushed = {b["reference"]: b for b in counted if b.get("reference")}
    order = [b["reference"] for b in counted if b.get("reference")]
    accounts = {}
    for path in glob.glob(os.path.join(kept, "darts", "*.json")):
        with open(path) as fh:
            a = json.load(fh)
        accounts[a["reference"]] = a
    refs = [r for r in order if r in accounts]
    check(len(refs) >= 10 and len(order) - len(refs) <= 1,
          "keep: %d darts pushed and %d of them kept (at least ten, bar at most the last)" % (len(order), len(refs)))
    switches = {k: v for k, v in (accounts[refs[0]]["switches"] if refs else {}).items()
                if k not in ("OD_KEEP_WINDOWS", "OD_KEEP_WINDOWS_MB", "OD_MAX_CYCLES") and v != "(withheld)"}
    print("replay: under the board's own switches %s" % json.dumps(switches, sort_keys=True), flush=True)

    # ---- replay ------------------------------------------------------------------------------
    before = tree_digest(kept)
    rc, lines, notes = replay(rig.BIN, kept, switches, "replay")
    for n in notes:
        print("   " + n[:200], flush=True)
    check(rc == 0, "replay: the replay ends 0 (rc=%s)" % rc)
    check(sorted(lines) == sorted(accounts), "replay: one REPLAY line for every account (%d of %d)" % (len(lines), len(accounts)))
    diffs = differences(pushed, lines, refs)
    for ref, what in diffs:
        print("   DIFF %s %s" % (ref, what), flush=True)
    check(not diffs, "replay: %d of %d kept darts read as they were pushed -- sector, path, cameras, radius, angle"
          % (len(refs) - len(diffs), len(refs)))
    print("   pushed   %s" % " ".join(pushed[r]["sector"] for r in refs))
    print("   replayed %s" % " ".join(lines.get(r, {}).get("sector", "?") for r in refs), flush=True)
    silent = [r for r, f in lines.items() if not all(t in f.get("cannot", "").split(",") for t in ALWAYS)]
    check(not silent, "replay: every line says what a replay cannot reproduce (%s)" % (silent[:2] or "all %d" % len(lines)))
    check(any(n.startswith("I1991 REPLAY-CANNOT") for n in notes), "replay: and the three are said in words once, at the top")
    quiet = [r for r, f in lines.items() if "switches:" in f.get("cannot", "")]
    check(not quiet, "replay: no line reports a switch the board ran under and the replay did not (%s)" % (quiet[:2] or "none"))
    check(tree_digest(kept) == before, "replay: the replay wrote nothing under the kept directory")

    # ---- layout ------------------------------------------------------------------------------
    ok, text = layout_case(rig.BIN, kept, switches, "layout")
    check(ok, "layout: a calibration stating another record size is refused by name, with both sizes (%s)" % text)

    # ---- picture -----------------------------------------------------------------------------
    victim = refs[len(refs) // 2]
    where = os.path.join(rig.WORK, "picture")
    shutil.rmtree(where, ignore_errors=True)
    shutil.copytree(kept, where)
    gone = accounts[victim]["cameras"][0]["settled"]
    os.remove(os.path.join(where, "pics", gone + ".png"))
    rc, plines, pnotes = replay(rig.BIN, where, switches, "picture")
    # the same picture is the next dart's working background, so that dart is refused too
    lost = [r for r in refs if any(gone in (c["settled"], c["clean"], c["fresh"]) for c in accounts[r]["cameras"])]
    told = [n for n in pnotes if n.startswith("I1991 REPLAY-REFUSED ref=") and gone in n]
    check(rc == 1 and all(plines.get(r, {}).get("reason") == "picture-missing" for r in lost) and len(told) == len(lost),
          "picture: the %d dart(s) naming the removed picture %s are refused picture-missing, each by name (rc=%s)"
          % (len(lost), gone, rc))
    rest = differences(pushed, plines, [r for r in refs if r not in lost])
    check(not rest, "picture: every other dart reads as it was pushed (%s)" % (rest[:2] or "all"))

    # ---- mutations ---------------------------------------------------------------------------
    if os.environ.get("I1995_MUTATIONS", "1") != "1":
        print("mutations: SKIPPED (I1995_MUTATIONS=%s)" % os.environ.get("I1995_MUTATIONS"), flush=True)
    elif not os.path.isdir("/app/build"):
        check(False, "mutations: /app holds a copy of the tree and its build (i1995_run.sh makes it)")
    else:
        second = [r for r in refs if accounts[r]["board_state_before"] > 0]
        bare = [r for r in refs if not any(c["fresh"] for c in accounts[r]["cameras"])]
        named = second[0] if second else "-"
        print("==== M1: closeKeptWindow does not seat the working background ====")
        print("PREDICTION: red; %s (the first dart pushed on a board already holding one) moves in radius and angle "
              "at least; the %d darts whose account names no working background read as pushed" % (named, len(bare)), flush=True)
        m1 = mutant("M1", "src/detector/geometry/detection/dart_processing.cpp", SEAT, "")
        if m1:
            rc, mlines, _ = replay(m1, kept, switches, "M1")
            mdiffs = dict(differences(pushed, mlines, refs))
            for ref, what in mdiffs.items():
                print("   M1 DIFF %s %s" % (ref, what), flush=True)
            hit = mdiffs.get(named, "")
            check(named in mdiffs and "radius" in hit and "angle" in hit,
                  "M1: the replay is red and names %s in radius and angle (%s)" % (named, hit or "it read as pushed"))
            check(not [r for r in bare if r in mdiffs],
                  "M1: no dart without a working background moved (%d of them; %d darts moved in all)" % (len(bare), len(mdiffs)))
        print("==== M2: the calibration's stated record size is not held to the struct ====")
        print("PREDICTION: `layout` red -- the altered file is read, no dart is calibration-refused; `replay` on the "
              "unaltered directory stays green", flush=True)
        m2 = mutant("M2", "src/detector/geometry/detection/window_replay.hpp", SIZE_RULE, "if (false)")
        if m2:
            ok, text = layout_case(m2, kept, switches, "M2-layout")
            check(not ok, "M2: the layout case is red under the mutant (%s)" % text)
            rc, mlines, _ = replay(m2, kept, switches, "M2")
            mdiffs = differences(pushed, mlines, refs)
            check(rc == 0 and not mdiffs, "M2: the unaltered directory still replays as pushed (rc=%s, %d differ)" % (rc, len(mdiffs)))

    print("\nchecks %d   failed %d" % (rig.checks, len(rig.failures)))
    for f in rig.failures:
        print("  - " + f)
    return 1 if rig.failures else 0


if __name__ == "__main__":
    os.makedirs(rig.WORK, exist_ok=True)
    sys.exit(main())

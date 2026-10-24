#!/usr/bin/env python3
"""turnaus#1936's census: is a dart near the treble ring solved ~2-3 mm towards the bull,
and does the sealed look's held-out treble residual predict the T->S losses?

Two sources, both read-only:

  live   a session's detector log plus its turnaus truth line (corrected = truth; an
         uncorrected dart is presumed right and counted separately, because outside the
         marked span it is not reliable truth). Per dart: the solved radius (the fused
         solve, `board_radius` x 170 mm), the camera the ring was read through, that
         camera's SEALED treble residuals and the scoring edge #1553 judged the treble
         at (99 mm + the bounded bloom correction), the published ring and the truth ring.
  replay a testers/i1555_run.sh run directory: the I1512ENTRY lines (solved r, per-camera
         calls `byCam`) joined to the fixture truth by the I1536 CAMERA-DART window.

Usage:
  i1936_census.py live  <log> <truth-line> [label]
  i1936_census.py replay <i1555 run dir>
  i1936_census.py all   <sessions dir> [i1555 run dir]
"""
import json
import os
import re
import sys

R_DOUBLE_OUT = 170.0
T_IN, T_OUT = 99.0, 107.0
BAND_LO, BAND_HI = 88.0, 118.0  # "near the treble ring": +-11 mm around the band

MODEL = re.compile(r"Camera (\d) board model \[[^\]]*\]: (.*)")
RESID = re.compile(r"the (bullseye|25 ring|treble ring's inner edge|treble ring's outer edge|doubles ring's inner edge|doubles ring) "
                   r"\[(held-out|fitted)\] ([+-]?\d+\.\d) mm \(rms [\d.]+ px over (\d+) rays\)")
FACTOR = re.compile(r"factor (\d\.\d+)")
LOOK = re.compile(r"camera (\d) calibrated on look (\d+) of")
SEAL = re.compile(r"camera (\d) seals look (\d+) of")
READ_THROUGH = re.compile(r"Geometric score: (\S+) from .* read through camera (\d+)")
COUNTED = re.compile(r"TURNAUS: COUNTED (\{.*\})")


def ring_of(score):
    if score in (None, "-", "None", ""):
        return None
    if score.startswith("T"):
        return "T"
    if score.startswith("D"):
        return "D"
    if score.startswith("S"):
        return "S"
    return score  # BULL / OUTER / MISS


def bloom_edge(resid, which):
    """#1553's trebleBloomAdjustMm restated: held-out, >=50 rays, widening only, clamped 8 mm.
    (The #1485 in-band test is not in the log line; a printed residual on an ACCEPTED fit
    passed it, which is what this assumes.)"""
    r = resid.get(which)
    if r is None or r[1] != "held-out" or r[3] < 50:
        return 0.0
    widen = -1.0 if which == "inner" else 1.0
    bloom = r[2] * widen
    if not bloom > 0:
        return 0.0
    return widen * min(bloom, T_OUT - T_IN)


def parse_model(text):
    resid = {}
    for name, kind, mm, rays in RESID.findall(text):
        key = {"bullseye": "bull", "25 ring": "25", "treble ring's inner edge": "inner",
               "treble ring's outer edge": "outer", "doubles ring's inner edge": "dinner",
               "doubles ring": "douter"}[name]
        resid[key] = (name, kind, float(mm), int(rays))
    f = FACTOR.search(text)
    return {"resid": resid, "factor": float(f.group(1)) if f else None}


def live(log_path, truth_path, label):
    truth = {}
    with open(truth_path) as fh:
        header = None
        for line in fh:
            if line.startswith("# reference"):
                header = line[2:].split()
                continue
            if line.startswith("#") or not line.strip():
                continue
            v = line.split()
            row = dict(zip(header, v))
            truth[row["reference"]] = row

    # Calibration in effect: per camera, the look that was sealed (#1456), else the last fit.
    pending = {}   # cam -> last model seen (awaiting its "calibrated on look N")
    by_look = {}   # (cam, look) -> model
    sealed = {}    # cam -> model in effect
    darts = []
    last_read = None
    with open(log_path, errors="replace") as fh:
        for line in fh:
            if "Session Started" in line:
                pending, by_look, sealed = {}, {}, {}
                continue
            m = MODEL.search(line)
            if m:
                cam = int(m.group(1))
                pending[cam] = parse_model(m.group(2))
                sealed.setdefault(cam, pending[cam])  # a single-look start seals its only fit
                sealed[cam] = sealed[cam] if (cam, "sealed") in by_look else pending[cam]
                continue
            m = LOOK.search(line)
            if m and int(m.group(1)) in pending:
                by_look[(int(m.group(1)), int(m.group(2)))] = pending[int(m.group(1))]
                continue
            m = SEAL.search(line)
            if m:
                cam, look = int(m.group(1)), int(m.group(2))
                if (cam, look) in by_look:
                    sealed[cam] = by_look[(cam, look)]
                    by_look[(cam, "sealed")] = True
                continue
            m = READ_THROUGH.search(line)
            if m:
                last_read = (m.group(1), int(m.group(2)))
                continue
            m = COUNTED.search(line)
            if m:
                try:
                    j = json.loads(m.group(1))
                except ValueError:
                    continue
                ref = j.get("reference")
                cal = {c: sealed.get(c) for c in (1, 2, 3)}
                read = last_read if (j.get("path") == "geometry" and last_read
                                     and last_read[0] == j.get("sector")) else None
                darts.append({"ref": ref, "j": j, "read": read, "cal": cal})
                last_read = None

    rows = []
    for d in darts:
        t = truth.get(d["ref"])
        j = d["j"]
        if t is None or j.get("board_radius") is None:
            continue
        r_mm = float(j["board_radius"]) * R_DOUBLE_OUT
        corrected = t.get("corrected", "-") not in ("-", "None")
        truth_score = t["corrected"] if corrected else t["published"]
        cam = d["read"][1] if d["read"] else None
        edges = {}
        for c in (1, 2, 3):
            cal = d["cal"].get(c)
            if cal is None:
                edges[c] = None
                continue
            edges[c] = (T_IN + bloom_edge(cal["resid"], "inner"), T_OUT + bloom_edge(cal["resid"], "outer"),
                        cal["resid"].get("inner", (0, 0, None))[2], cal["resid"].get("outer", (0, 0, None))[2])
        rows.append({"src": label, "ref": d["ref"], "r": r_mm, "path": j.get("path"), "cam": cam,
                     "cams": j.get("cameras_used"), "pub": j.get("sector"), "truth": truth_score,
                     "corrected": corrected, "edges": edges})
    return rows


def replay(run_dir):
    rows = []
    for fx in ("r18", "r22", "r29"):
        for win in ("dev", "open"):
            out = os.path.join(run_dir, f"{fx}-{win}.txt")
            cen = os.path.join(run_dir, f"census-{fx}-{win}.txt")
            if not (os.path.exists(out) and os.path.exists(cen)):
                continue
            truth = {}
            for line in open(cen, errors="replace"):
                m = re.search(r"CAMERA-DART .* dart=(\S+) thrown=(\S+) .* win=(\d+)", line)
                if m:
                    truth[int(m.group(3))] = (m.group(1), m.group(2))
            edges = {}
            for line in open(out, errors="replace"):
                m = MODEL.search(line)
                if m:
                    cal = parse_model(m.group(2))
                    edges[int(m.group(1))] = (T_IN + bloom_edge(cal["resid"], "inner"),
                                              T_OUT + bloom_edge(cal["resid"], "outer"),
                                              cal["resid"].get("inner", (0, 0, None))[2],
                                              cal["resid"].get("outer", (0, 0, None))[2])
                m = re.search(r"I1512ENTRY window=(\d+) outcome=(\S+) solved=1 .* r=([\d.]+) .* score=(\S+) .* "
                              r"byCam=(\S+) .* story=.* through cam (\d)", line)
                if m and int(m.group(1)) in truth:
                    dart, thrown = truth[int(m.group(1))]
                    rows.append({"src": f"{fx}-{win}", "ref": dart, "r": float(m.group(3)), "path": "geometry",
                                 "cam": int(m.group(6)), "cams": m.group(5), "pub": m.group(4), "truth": thrown,
                                 "corrected": True, "edges": dict(edges)})
    return rows


def fmt_edge(e):
    if e is None:
        return "   -       "
    inner, _, ri, ro = e
    ri = "  -" if ri is None else f"{ri:+.1f}"
    ro = "  -" if ro is None else f"{ro:+.1f}"
    return f"{inner:5.1f}({ri}/{ro})"


def report(rows, title):
    near = [x for x in rows if BAND_LO <= x["r"] <= BAND_HI and ring_of(x["truth"]) in ("T", "S")]
    print(f"\n=== {title}: {len(rows)} darts with a solved radius, {len(near)} with truth T/S solved at "
          f"{BAND_LO:.0f}..{BAND_HI:.0f} mm ===")
    print("I1936 COLUMNS src ref path cam(read-through) r_mm  d_in(r-99)  pub truth  truth-src  "
          "edge-inner(res_in/res_out) cam1 cam2 cam3  verdict")
    for x in sorted(near, key=lambda x: x["r"]):
        pr, tr = ring_of(x["pub"]), ring_of(x["truth"])
        verdict = "ok" if x["pub"] == x["truth"] else f"RING pub {pr} truth {tr}" if pr != tr else "wedge"
        e = x["edges"]
        print(f"I1936 DART {x['src']:<12} {x['ref'][-8:]:<8} {str(x['path']):<8} cam={x['cam'] if x['cam'] else '-'} "
              f"r={x['r']:6.2f} d_in={x['r'] - T_IN:+6.2f} pub={x['pub']:<4} truth={x['truth']:<4} "
              f"{'corrected' if x['corrected'] else 'presumed '} "
              f"{fmt_edge(e.get(1))} {fmt_edge(e.get(2))} {fmt_edge(e.get(3))}  {verdict}")
    # The decision numbers.
    for scope, keep in (("all", lambda x: True), ("corrected-only", lambda x: x["corrected"])):
        sub = [x for x in near if keep(x)]
        T = sorted(x["r"] for x in sub if ring_of(x["truth"]) == "T")
        S_in = sorted(x["r"] for x in sub if ring_of(x["truth"]) == "S" and x["r"] < 103)
        S_out = sorted(x["r"] for x in sub if ring_of(x["truth"]) == "S" and x["r"] >= 103)
        def med(a):
            return f"{a[len(a) // 2]:.2f}" if a else "-"
        TS = [x for x in sub if ring_of(x["truth"]) == "T" and ring_of(x["pub"]) == "S"]
        ST = [x for x in sub if ring_of(x["truth"]) == "S" and ring_of(x["pub"]) == "T"]
        print(f"I1936 SUMMARY {title} {scope}: truth-T n={len(T)} r={','.join(f'{v:.1f}' for v in T)} "
              f"median={med(T)} (band 99..107, centre 103) | truth-S inside n={len(S_in)} max="
              f"{(max(S_in) if S_in else float('nan')):.1f} | truth-S outside n={len(S_out)} min="
              f"{(min(S_out) if S_out else float('nan')):.1f} | treble lost (truth T, published S) {len(TS)} | single made treble (truth S, published T) {len(ST)}")
        # The best inner threshold the data supports: every edge between consecutive radii.
        lab = sorted([(x["r"], ring_of(x["truth"])) for x in sub if x["r"] < 103])
        best = None
        cands = sorted(set([94.0 + 0.25 * k for k in range(29)]))
        for th in cands:
            err = sum(1 for r, t in lab if (r > th) != (t == "T"))
            if best is None or err < best[1]:
                best = (th, err, [th])
            elif err == best[1]:
                best[2].append(th)
        if lab:
            spec = sum(1 for r, t in lab if (r > T_IN) != (t == "T"))
            print(f"I1936 THRESHOLD {title} {scope}: inner edge errors at the spec 99.0 mm = {spec} of {len(lab)}; "
                  f"fewest = {best[1]} at {min(best[2]):.2f}..{max(best[2]):.2f} mm (the solve's offset at the "
                  f"inner treble wire, if one constant explains it, is 99 minus that)")
    counterfactual(rows, title)
    return near


def measured(e):
    """A camera's treble edges, or None where #1553 had no held-out measurement to correct by."""
    if e is None:
        return None
    inner, outer, ri, ro = e
    return (inner if ri is not None else None, outer if ro is not None else None)


def borrowed_edges(x):
    """The edges the read-through camera would judge at if it BORROWED the other cameras'
    measured treble edges where it has none of its own (the median of those that measured)."""
    e = x["edges"]
    own = measured(e.get(x["cam"]))
    if own is None:
        return None
    out = []
    for k in (0, 1):
        if own[k] is not None:
            out.append(own[k])
            continue
        others = sorted(m[k] for c in (1, 2, 3) if c != x["cam"] for m in [measured(e.get(c))]
                        if m is not None and m[k] is not None)
        if not others:
            out.append(T_IN if k == 0 else T_OUT)
        elif len(others) % 2:
            out.append(others[len(others) // 2])
        else:
            out.append(0.5 * (others[len(others) // 2 - 1] + others[len(others) // 2]))
    return tuple(out)


def counterfactual(rows, title):
    """Geometry darts read through a camera with no treble measurement of its own, re-called at
    the borrowed edges. Only the ring word moves; the wedge is the published one."""
    moved = {"gain": [], "loss": [], "same-wrong": [], "same-right": 0}
    for x in rows:
        if x["path"] != "geometry" or not x["cam"] or ring_of(x["pub"]) not in ("S", "T"):
            continue
        if ring_of(x["truth"]) not in ("S", "T", "D"):
            continue
        if not (BAND_LO <= x["r"] <= BAND_HI):
            continue
        b = borrowed_edges(x)
        if b is None:
            continue
        ring = "T" if b[0] < x["r"] <= b[1] else "S"
        if ring == ring_of(x["pub"]):
            continue
        new_score = ring + x["pub"][1:]
        was_right = x["pub"] == x["truth"]
        now_right = new_score == x["truth"]
        tag = f"{x['src']} {x['ref'][-8:]} r={x['r']:.2f} edges={b[0]:.1f}..{b[1]:.1f} pub={x['pub']} -> {new_score} truth={x['truth']} ({'corrected' if x['corrected'] else 'presumed'})"
        if now_right and not was_right:
            moved["gain"].append(tag)
        elif was_right and not now_right:
            moved["loss"].append(tag)
        else:
            moved["same-wrong"].append(tag)
    for k in ("gain", "loss", "same-wrong"):
        for t in moved[k]:
            print(f"I1936 BORROW-{k.upper()} {t}")
    gc = sum(1 for t in moved["gain"] if "(corrected)" in t)
    lc = sum(1 for t in moved["loss"] if "(corrected)" in t)
    print(f"I1936 BORROW {title}: re-called {len(moved['gain']) + len(moved['loss']) + len(moved['same-wrong'])} | "
          f"gained {len(moved['gain'])} ({gc} corrected) | lost {len(moved['loss'])} ({lc} corrected, the rest "
          f"presumed-right uncorrected darts) | changed but still wrong {len(moved['same-wrong'])}")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    if argv[1] == "live":
        report(live(argv[2], argv[3], argv[4] if len(argv) > 4 else "live"), argv[4] if len(argv) > 4 else "live")
    elif argv[1] == "replay":
        report(replay(argv[2]), "replay")
    elif argv[1] == "all":
        base = argv[2]
        pooled = []
        for day in sorted(os.listdir(base)):
            d = os.path.join(base, day)
            if not os.path.isdir(d):
                continue
            files = os.listdir(d)
            for tf in sorted(f for f in files if "truth" in f):
                stem = tf.replace(".truth.json", "").replace("_truth-line.json", "")
                log = next((os.path.join(d, f) for f in (stem + ".log", stem + "_log.json") if f in files), None)
                if log:
                    rows = live(log, os.path.join(d, tf), f"{day[5:]}/{stem[:8]}")
                    report(rows, f"{day} {stem}")
                    pooled += rows
        report(pooled, "POOLED-LIVE")
        if len(argv) > 3:
            report(replay(argv[3]), "REPLAY " + os.path.basename(os.path.dirname(argv[3].rstrip('/'))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""#1497: read the census rows and lay the number ring out as a table.

Arithmetic and printing only. It decides nothing, asserts nothing and compares against no
threshold -- the shell harness owns the structural control and the maintainer owns #1498.
"""
import sys


def rows(path, tag):
    out = []
    with open(path) as fh:
        for line in fh:
            if not line.startswith(tag + " "):
                continue
            d = {}
            for field in line.split()[1:]:
                if "=" in field:
                    k, v = field.split("=", 1)
                    d[k] = v
            out.append(d)
    return out


def num(d, k, default=0.0):
    try:
        return float(d[k])
    except (KeyError, ValueError):
        return default


def median(xs):
    s = sorted(xs)
    if not s:
        return 0.0
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def main(path):
    cams = rows(path, "I1497CAM")
    cells = rows(path, "I1497CELL")
    if not cells:
        print("  no cells: no camera on this fixture gave a board plane to place one on")
        return 0

    for cam in cams:
        idx = cam.get("cam")
        mine = [c for c in cells if c.get("cam") == idx]
        if not mine:
            continue
        print("  camera %s -- doubles semi-axis %s px, tilt %s, anchored=%s"
              % (idx, cam.get("doublesPx"), cam.get("tilt"), cam.get("anchored")))
        print("    wedge  centre   radial  tangent   px/mm     px/mm   obliq  corners   grey    grey   lap")
        print("            deg        px       px     max       min     deg  inframe  p05-p95    sd    sd")
        for c in mine:
            print("    %5s  %6.1f  %7.1f  %7.1f  %6.3f  %6.3f  %6.1f  %4s/4  %4s-%-4s  %5.1f %5.1f"
                  % (c.get("wedge"), num(c, "centreDeg"), num(c, "radialPx"),
                     num(c, "tangentialPx"), num(c, "pxPerMmMax"), num(c, "pxPerMmMin"),
                     num(c, "obliquityDeg"), c.get("cornersInFrame"),
                     c.get("greyP05"), c.get("greyP95"), num(c, "greySd"), num(c, "lapSd")))
        rad = [num(c, "radialPx") for c in mine]
        tan = [num(c, "tangentialPx") for c in mine]
        obl = [num(c, "obliquityDeg") for c in mine]
        clipped = [c.get("wedge") for c in mine if c.get("cornersInFrame") != "4"]
        print("    ring depth  min %.1f  median %.1f  max %.1f px" % (min(rad), median(rad), max(rad)))
        print("    wedge width min %.1f  median %.1f  max %.1f px" % (min(tan), median(tan), max(tan)))
        print("    obliquity   min %.1f  median %.1f  max %.1f deg" % (min(obl), median(obl), max(obl)))
        if clipped:
            print("    OFF THE FRAME: wedges %s have a corner outside the picture" % ",".join(clipped))
        else:
            print("    every one of the twenty cells is wholly inside the frame")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

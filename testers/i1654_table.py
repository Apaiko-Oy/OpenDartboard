"""#1654: summarise testers/i1654_comb_census.cpp's I1654 lines per camera and window.

    python3 i1654_table.py <census.txt> [...] [--rows]

Per camera and window, over the looks that calibrated (sees=1): how many, and the spread
(sd, and min..max) of
    own    the comb offset in the look's own plane
    rot    the whole comb's rotation carried into the reference plane
    w129   the 12/9 wire at 88.6 mm carried into the reference plane
    mw129  the same through the doubles band's centre-line plane (#1653)
and the least-squares slope of w129 on the look's plane centre (bull, or the wire centre
where ctr= is logged), in degrees per pixel, with the share of w129's variance it explains.
A reporter: it asserts nothing.
"""
import re
import sys
import math


def parse(path):
    rows = []
    for line in open(path):
        if not line.startswith("I1654 "):
            continue
        d = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
        rows.append(d)
    return rows


def stats(v):
    if not v:
        return "-"
    m = sum(v) / len(v)
    sd = math.sqrt(sum((x - m) ** 2 for x in v) / (len(v) - 1)) if len(v) > 1 else 0.0
    return "%+.2f sd %.2f [%+.2f..%+.2f]" % (m, sd, min(v), max(v))


def fit2(xs, ys, zs):
    """z = a + b x + c y, least squares; returns (b, c, r2)."""
    n = len(zs)
    if n < 4:
        return None
    mx, my, mz = sum(xs) / n, sum(ys) / n, sum(zs) / n
    sxx = sum((x - mx) ** 2 for x in xs); syy = sum((y - my) ** 2 for y in ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sxz = sum((x - mx) * (z - mz) for x, z in zip(xs, zs)); syz = sum((y - my) * (z - mz) for y, z in zip(ys, zs))
    det = sxx * syy - sxy * sxy
    if abs(det) < 1e-9:
        return None
    b = (sxz * syy - syz * sxy) / det
    c = (syz * sxx - sxz * sxy) / det
    ss = sum((z - mz) ** 2 for z in zs)
    res = sum((z - mz - b * (x - mx) - c * (y - my)) ** 2 for x, y, z in zip(xs, ys, zs))
    return b, c, (1 - res / ss) if ss > 0 else 0.0


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    show_rows = "--rows" in sys.argv
    rows = []
    for p in paths:
        rows += parse(p)
    keys = []
    for r in rows:
        k = (r["clip"].split("/")[0], r["cam"])
        if k not in keys:
            keys.append(k)
    for k in keys:
        allc = [r for r in rows if (r["clip"].split("/")[0], r["cam"]) == k and r.get("sees") == "1" and "w129" in r]
        print("== %s cam %s" % k)
        for win in ("open", "dev"):
            rs = [r for r in allc if r["win"] == win]
            avg = [r for r in rs if r["look"] == "avg"]
            looks = [r for r in rs if r["look"] != "avg"]
            print("  %-4s avg: %s" % (win, ("bull=%s%s own=%s w129=%s mw129=%s R=%s" % (
                avg[0]["bull"], (" ctr=" + avg[0]["ctr"]) if "ctr" in avg[0] else "", avg[0]["own"], avg[0]["w129"],
                avg[0].get("mw129", "-"), avg[0]["R"])) if avg else "refused"))
            print("  %-4s %2d looks  own %s | rot %s | w129 %s | mw129 %s" % (
                win, len(looks), stats([float(r["own"]) for r in looks]), stats([float(r["rot"]) for r in looks]),
                stats([float(r["w129"]) for r in looks]), stats([float(r["mw129"]) for r in looks if "mw129" in r])))
            if show_rows:
                for r in rs:
                    print("      look=%-3s R=%s bull=%s%s own=%s rot=%s w129=%s mw129=%s" % (
                        r["look"], r["R"], r["bull"], (" ctr=" + r["ctr"]) if "ctr" in r else "", r["own"], r["rot"],
                        r["w129"], r.get("mw129", "-")))
        xs, ys, zs = [], [], []
        for r in allc:
            c = r.get("ctr", r["bull"]).split(",")
            xs.append(float(c[0])); ys.append(float(c[1])); zs.append(float(r["w129"]))
        f = fit2(xs, ys, zs)
        if f:
            print("  w129 on the plane centre, both windows: %+.2f deg/px in x, %+.2f deg/px in y, explains %.0f%%" % (
                f[0], f[1], 100 * f[2]))


if __name__ == "__main__":
    main()

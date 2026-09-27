"""#1654: summarise testers/i1654_comb_census.cpp's I1654 lines per camera and window.

    python3 i1654_table.py <census.txt> [...] [--rows]

Per camera and window, over the looks that calibrated (sees=1): how many, and the spread
(sd, and min..max) of
    own    the comb offset in the look's own plane
    rot    the whole comb's rotation carried into the reference plane
    w129   the 12/9 wire at 88.6 mm carried into the reference plane
    mw129  the same through the doubles band's centre-line plane (#1653)
and the least-squares slope of w129 on the look's plane centre (bull, or the wire centre
where ctr= is logged), in degrees per pixel, with the share of w129's variance it explains;
then with the traced doubles conic's minor axis added, the other half of what planeOf
builds the plane from.
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


def fitk(cols, zs):
    """z = a + sum b_j x_j by least squares; returns (coefficients, r2) or None."""
    n, k = len(zs), len(cols)
    if n < k + 3:
        return None
    means = [sum(c) / n for c in cols]
    mz = sum(zs) / n
    X = [[c[i] - m for c, m in zip(cols, means)] for i in range(n)]
    z = [v - mz for v in zs]
    A = [[sum(X[i][a] * X[i][b] for i in range(n)) for b in range(k)] + [sum(X[i][a] * z[i] for i in range(n))]
         for a in range(k)]
    for col in range(k):  # Gauss-Jordan with partial pivoting
        piv = max(range(col, k), key=lambda r: abs(A[r][col]))
        if abs(A[piv][col]) < 1e-9:
            return None
        A[col], A[piv] = A[piv], A[col]
        for r in range(k):
            if r != col:
                f = A[r][col] / A[col][col]
                A[r] = [x - f * y for x, y in zip(A[r], A[col])]
    b = [A[r][k] / A[r][r] for r in range(k)]
    ss = sum(v * v for v in z)
    res = sum((z[i] - sum(b[j] * X[i][j] for j in range(k))) ** 2 for i in range(n))
    return b, (1 - res / ss) if ss > 0 else 0.0


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
        bx, by, cw, zs = [], [], [], []
        for r in allc:
            c = r.get("ctr", r["bull"]).split(",")
            bx.append(float(c[0])); by.append(float(c[1])); zs.append(float(r["w129"]))
            cw.append(float(r["conic"].split("x")[0]))
        def report(names, cols, what):
            keep = [(nm, c) for nm, c in zip(names, cols) if max(c) > min(c)]
            f = fitk([c for _, c in keep], zs) if keep else None
            if f:
                print("  w129 on %s: %s, explains %.0f%%" % (what, ", ".join(
                    "%+.3f deg/px of %s" % (b, nm) for (nm, _), b in zip(keep, f[0])), 100 * f[1]))
            elif not keep:
                print("  w129 on %s: every look has the same %s" % (what, "/".join(names)))
        report(["bull x", "bull y"], [bx, by], "the plane centre")
        report(["bull x", "bull y", "conic minor"], [bx, by, cw], "the centre and the conic")

if __name__ == "__main__":
    main()

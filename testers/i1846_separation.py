# unrun-tester: a reporter over Turnaus truth lines and their board logs (real sessions kept off GitHub) for turnaus#1846; it asserts nothing about the tree, its inputs are not in the repository, and docs/rig.md records its counts. --self-test checks its own arithmetic.
"""turnaus#1846: what separates a flagged dart that is corrected from a flagged dart that stands?

    python3 i1846_separation.py [--log <log.txt> ...] <truth-line.txt> [<truth-line.txt> ...]
    python3 i1846_separation.py --self-test

Reads each truth line through i1815_census.read_truth_line and keeps the FLAGGED darts (the
board's own `flagged`, which #1815 recomputes 1328/1329 and 293/294). The verdict is #1815's:
`picked` alternative / candidate / typed is CORRECTED, `-` / `published` is STOOD (nobody
corrected it -- not proof it was right), `turn` / `removed` are SET ASIDE, counted, and in
neither class. A reference that appears in two truth lines is read once (the first).

A log given with --log is joined on the reference of each `TURNAUS: COUNTED` or `DROPPED` line (a
dart pushed to no Contest is DROPPED and is still in the truth line): the lines
since the previous COUNTED are that dart's. From them: `log_lines_failed` (a `LINES: 2 of 3`
line: one camera's line did not solve), `log_line_fault` (why: not straight / not a shaft /
rim only / ...), `log_vote_cams` (the string vote's `Consensus score: X from N cameras`, 1 for
`No consensus, using single camera score`), `log_vote_disagrees` (the vote's or the LONE-WIRE
camera's own reading names a different score than the one published -- the only per-camera
reading this build logs for a geometric dart), `log_rim_carried` (I1707 RIM CARRIED).

THE MEASURE is the AUC of one feature alone: the chance that a corrected flagged dart drawn at
random has a larger value than a stood one drawn at random, ties counting a half; 0.5 is no
separation, and the table prints max(AUC, 1-AUC) with the direction (`+` larger is corrected).
`strat` is the same AUC counting only pairs from the same truth line, so a feature that merely
tells one session from another earns nothing. The interval is a 2000-resample bootstrap
(classes resampled apart, seed fixed), 2.5th to 97.5th percentile of the oriented AUC.

    I1846 READ <board> flagged= corrected= stood= set_aside=(turn=,removed=) joined_log=
    I1846 FEATURE <name> n_corr= n_stood= auc= dir= ci=lo..hi strat= corr_rate_lo= corr_rate_hi=
        corr_rate_lo/hi: the corrected share among decided flags at or below / above the
        feature's median (binary: 0 / 1)
    I1846 LEVEL <name>=<value> corrected= stood= rate=  (categorical features)
    I1846 BASELINE <board|k0.7> flags= corrected= stood= set_aside= false_rate=
        the flags as the board raised them, and as #1815's OD_FLAG_SIGMAS=0.7 would
    I1846 RULE <name> base= removes= stood= corrected= set_aside= kept_corrected= kept_stood= false_rate_after=
    I1846 LOST <rule> <board> <reference> published= corrected= picked= <features>

Stdlib only. Exit 0 when it read flagged darts, 2 when there were none.
"""

import collections
import json
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i1815_census import num, read_truth_line, ring_margin, ring_of, rule_of, verdict, z_of  # noqa: E402

COUNTED_RE = re.compile(r"TURNAUS: (?:COUNTED|DROPPED) (\{.*\})")
LINES_RE = re.compile(r"LINES: (\d+) of (\d+) cameras' lines solved")
FAULT_RE = re.compile(r"cam \d+: (?:no usable axis: )?([a-z ]+?)(?::|\)|$)")
CONSENSUS_RE = re.compile(r"Consensus score: (\S+) from (\d+) cameras")
NOCONS_RE = re.compile(r"No consensus, using single camera score: (\S+) from camera")
LONE_RE = re.compile(r"LONE-WIRE: camera \d+'s (\S+) is")
SIGMA_FLOOR_MM = 5.0
BOOT = 2000


def auc(pos, neg):
    """Mann-Whitney AUC: P(pos > neg) + 0.5 P(tie). None when a class is empty."""
    if not pos or not neg:
        return None
    s = sorted([(v, 1) for v in pos] + [(v, 0) for v in neg])
    rank_sum, i = 0.0, 0
    while i < len(s):
        j = i
        while j < len(s) and s[j][0] == s[i][0]:
            j += 1
        mid = (i + j + 1) / 2.0
        rank_sum += mid * sum(1 for k in range(i, j) if s[k][1])
        i = j
    n1, n0 = len(pos), len(neg)
    return (rank_sum - n1 * (n1 + 1) / 2.0) / (n1 * n0)


def strat_auc(groups):
    """AUC over within-stratum pairs only: groups is [(pos, neg)]."""
    num_, den = 0.0, 0
    for pos, neg in groups:
        a = auc(pos, neg)
        if a is not None:
            num_ += a * len(pos) * len(neg)
            den += len(pos) * len(neg)
    return num_ / den if den else None


def boot_ci(pos, neg, flip, rng):
    vals = []
    for _ in range(BOOT):
        p = [rng.choice(pos) for _ in pos]
        n = [rng.choice(neg) for _ in neg]
        a = auc(p, n)
        vals.append(1 - a if flip else a)
    vals.sort()
    return vals[int(0.025 * BOOT)], vals[int(0.975 * BOOT) - 1]


def read_log(path):
    """{reference: {log features}} from a board log."""
    out, block = {}, []
    logs_lines = any("LINES: " in raw for raw in open(path, "r", errors="replace"))
    for raw in open(path, "r", errors="replace"):
        m = COUNTED_RE.search(raw)
        if not m:
            block.append(raw)
            continue
        try:
            j = json.loads(m.group(1))
        except ValueError:
            block = []
            continue
        ref, published = j.get("reference"), j.get("sector")
        f = {"log_lines_failed": 0 if logs_lines else None, "log_line_fault": "-" if logs_lines else "unlogged", "log_vote_cams": None, "log_vote_disagrees": None,
             "log_rim_carried": 0}
        readings = []
        for line in block:
            lm = LINES_RE.search(line)
            if lm:
                f["log_lines_failed"] = int(lm.group(2)) - int(lm.group(1))
                fm = FAULT_RE.search(line[lm.end():])
                f["log_line_fault"] = fm.group(1).strip().replace(" ", "-") if fm else "other"
            cm = CONSENSUS_RE.search(line)
            if cm:
                readings.append(cm.group(1))
                f["log_vote_cams"] = int(cm.group(2))
            nm = NOCONS_RE.search(line)
            if nm:
                readings.append(nm.group(1))
                f["log_vote_cams"] = 1
            om = LONE_RE.search(line)
            if om:
                readings.append(om.group(1))
            if "I1707 RIM CARRIED" in line:
                f["log_rim_carried"] = 1
        if readings and published:
            f["log_vote_disagrees"] = int(any(r != published for r in readings))
        if ref:
            out[ref] = f
        block = []
    return out


def features(r):
    """The numeric features of one flagged dart (None where the dart does not carry it)."""
    cams = r["cameras"]
    ncams = 0 if cams in ("-", "") else len(cams.split(","))
    cands = r["candidates"]
    clist = [] if cands in ("-", "") else cands.split(",")
    sigma = num(r["sigma_mm"])
    geo = r["path"] == "geometry"
    f = {
        "z": z_of(r) if geo else None,
        "margin_mm": num(r["margin_mm"]),
        "sigma_mm": sigma,
        "sigma_on_floor": (1 if sigma is not None and sigma <= SIGMA_FLOOR_MM else 0) if geo else None,
        "crossing_deg": num(r["crossing_deg"]),
        "two_line": (1 if r["crossing_deg"] != "-" else 0) if geo else None,
        "cameras": ncams,
        "agreeing": num(r["agreeing"]),
        "lone_wire_mm": num(r["lone_wire_mm"]),
        "ring_wire_mm": ring_margin(r) if not geo else num(r["ring_wire_mm"]),
        "radius": num(r["radius"]),
        "degraded": int(r["degraded"] == "1"),
        "bounced": int(r["bounced"] == "1"),
        "vote_path": int(r["path"] == "vote"),
        "wire_ring": (1 if r["wire_kind"] == "ring" else 0) if r["wire_kind"] != "-" else None,
        "alt_is_miss": int(r["alternative"] in ("None", "MISS")),
        "alt_is_first_candidate": int(bool(clist) and clist[0] == r["alternative"]),
        "published_double": int(ring_of(r["published"]) == "double"),
        "published_treble": int(ring_of(r["published"]) == "triple"),
        "published_bull_or_25": int(ring_of(r["published"]) in ("bull", "outer")),
        "candidates": len(clist),
    }
    for k in ("log_lines_failed", "log_vote_cams", "log_vote_disagrees", "log_rim_carried"):
        if k in r:
            f[k] = r[k]
    return f


CATEGORICAL = ("rule", "log_line_fault", "board")


def load(truths, logs):
    joined = {}
    for lp in logs:
        joined.update(read_log(lp))
    seen, flagged = set(), []
    for p in truths:
        board = os.path.basename(p).replace("truth-", "").replace(".txt", "")
        rows = read_truth_line(p)
        c, nj = collections.Counter(), 0
        for r in rows:
            if r["reference"] in seen or r["flagged"] != "1":
                continue
            seen.add(r["reference"])
            r["board"] = board
            r["rule"] = rule_of(r)
            r["verdict"] = verdict(r)
            c[r["verdict"]] += 1
            c["aside_" + r["picked"]] += r["verdict"] == "ambiguous"
            if r["reference"] in joined:
                r.update(joined[r["reference"]])
                nj += 1
            flagged.append(r)
        print("I1846 READ %s flagged=%d corrected=%d stood=%d set_aside=%d(turn=%d,removed=%d) joined_log=%d" % (
            board, c["true"] + c["false"] + c["ambiguous"], c["true"], c["false"], c["ambiguous"],
            c["aside_turn"], c["aside_removed"], nj))
    return flagged


def rank(flagged, rng):
    decided = [r for r in flagged if r["verdict"] in ("true", "false")]
    fs = [(r, features(r)) for r in decided]
    names = sorted({k for _, f in fs for k in f})
    table = []
    for name in names:
        pos = [f[name] for r, f in fs if r["verdict"] == "true" and f.get(name) is not None]
        neg = [f[name] for r, f in fs if r["verdict"] == "false" and f.get(name) is not None]
        a = auc(pos, neg)
        if a is None or len(pos) < 3:
            continue
        flip = a < 0.5
        lo, hi = boot_ci(pos, neg, flip, rng)
        groups = collections.defaultdict(lambda: ([], []))
        for r, f in fs:
            if f.get(name) is not None:
                groups[r["board"]][0 if r["verdict"] == "true" else 1].append(f[name])
        sa = strat_auc(list(groups.values()))
        allv = sorted(pos + neg)
        med = allv[len(allv) // 2] if len(set(allv)) > 2 else 0
        lo_p = sum(v <= med for v in pos); lo_n = sum(v <= med for v in neg)
        hi_p, hi_n = len(pos) - lo_p, len(neg) - lo_n
        table.append((max(a, 1 - a), name, len(pos), len(neg), "-" if flip else "+", lo, hi,
                      (1 - sa if flip else sa) if sa is not None else None,
                      lo_p, lo_p + lo_n, hi_p, hi_p + hi_n, med))
    table.sort(reverse=True)
    for t in table:
        print("I1846 FEATURE %-24s n_corr=%d n_stood=%d auc=%.3f dir=%s ci=%.2f..%.2f strat=%s split<=%s "
              "corr_rate_lo=%d/%d corr_rate_hi=%d/%d" % (
                  t[1], t[2], t[3], t[0], t[4], t[5], t[6], "%.3f" % t[7] if t[7] is not None else "-",
                  ("%g" % t[12]), t[8], t[9], t[10], t[11]))
    for name in CATEGORICAL:
        lv = collections.defaultdict(collections.Counter)
        for r in decided:
            if name in r:
                lv[r[name]][r["verdict"]] += 1
        for v in sorted(lv):
            c = lv[v]
            print("I1846 LEVEL %s=%s corrected=%d stood=%d rate=%.3f" % (
                name, v, c["true"], c["false"], c["true"] / (c["true"] + c["false"])))
    return table


RULES = (
    ("A-unflag-geo-two-line-ring", "a geometric flag on a ring wire from a two-line solve is not raised",
     lambda r, f: r["path"] == "geometry" and r["wire_kind"] == "ring" and r["crossing_deg"] != "-"),
    ("B-unflag-geo-where-vote-agrees", "a geometric flag is not raised where the string vote's own reading names "
     "the published score (logged boards only)",
     lambda r, f: r["path"] == "geometry" and f.get("log_vote_disagrees") == 0),
    ("C-unflag-consensus-vote", "a vote flag with two or more agreeing cameras is not raised",
     lambda r, f: r["path"] == "vote" and (f.get("agreeing") or 0) >= 2),
    ("D-three-line-k0.5", "a three-line geometric flag is raised only at z < 0.5",
     lambda r, f: r["path"] == "geometry" and f.get("two_line") == 0 and f["z"] is not None and f["z"] >= 0.5),
    ("C+D", "both C and D",
     lambda r, f: (r["path"] == "vote" and (f.get("agreeing") or 0) >= 2)
     or (r["path"] == "geometry" and f.get("two_line") == 0 and f["z"] is not None and f["z"] >= 0.5)),
)
K_DEFAULT = 0.7


def apply_rules(flagged, base, label):
    """Each rule over the flags `base` keeps; `label` names that baseline."""
    pool = [r for r in flagged if base(r)]
    decided_all = collections.Counter(r["verdict"] for r in pool)
    print("I1846 BASELINE %s flags=%d corrected=%d stood=%d set_aside=%d false_rate=%.3f" % (
        label, len(pool), decided_all["true"], decided_all["false"], decided_all["ambiguous"],
        decided_all["false"] / (decided_all["true"] + decided_all["false"])))
    for name, desc, pred in RULES:
        c, lost = collections.Counter(), []
        for r in pool:
            f = features(r)
            if pred(r, f):
                c[r["verdict"]] += 1
                if r["verdict"] == "true":
                    lost.append((r, f))
        kept_t = decided_all["true"] - c["true"]
        kept_f = decided_all["false"] - c["false"]
        print("I1846 RULE %s base=%s removes=%d stood=%d corrected=%d set_aside=%d kept_corrected=%d kept_stood=%d "
              "false_rate_after=%.3f -- %s" % (
                  name, label, c["true"] + c["false"] + c["ambiguous"], c["false"], c["true"], c["ambiguous"], kept_t,
                  kept_f, kept_f / (kept_t + kept_f) if kept_t + kept_f else 0.0, desc))
        for r, f in lost:
            print("I1846 LOST %s base=%s %s %s published=%s alternative=%s corrected=%s picked=%s rule=%s z=%s "
                  "cameras=%s vote_disagrees=%s" % (
                      name, label, r["board"], r["reference"], r["published"], r["alternative"], r["corrected"],
                      r["picked"], r["rule"], "%.2f" % f["z"] if f["z"] is not None else "-", r["cameras"],
                      f.get("log_vote_disagrees", "-")))


def census(args):
    logs, truths = [], []
    it = iter(args)
    for a in it:
        if a == "--log":
            logs.append(next(it))
        else:
            truths.append(a)
    flagged = load(truths, logs)
    if not flagged:
        return 2
    c = collections.Counter(r["verdict"] for r in flagged)
    aside = collections.Counter(r["picked"] for r in flagged if r["verdict"] == "ambiguous")
    print("I1846 TOTAL flagged=%d corrected=%d stood=%d set_aside=%d(turn=%d,removed=%d)" % (
        len(flagged), c["true"], c["false"], c["ambiguous"], aside["turn"], aside["removed"]))
    rank(flagged, random.Random(1846))
    apply_rules(flagged, lambda r: True, "board")
    apply_rules(flagged, lambda r: r["path"] != "geometry" or z_of(r) <= K_DEFAULT, "k0.7")
    for r in flagged:
        if r["verdict"] == "true":
            f = features(r)
            print("I1846 CORRECTED %s %s %s published=%s alternative=%s corrected=%s picked=%s z=%s cameras=%s "
                  "agreeing=%s vote_disagrees=%s line_fault=%s" % (
                      r["board"], r["reference"], r["rule"], r["published"], r["alternative"], r["corrected"],
                      r["picked"], "%.2f" % f["z"] if f["z"] is not None else "-", r["cameras"], r["agreeing"],
                      f.get("log_vote_disagrees", "-"), r.get("log_line_fault", "-")))
    return 0


def self_test():
    fails = 0

    def check(name, got, want):
        nonlocal fails
        if got != want:
            fails += 1
            print("FAIL %s: got %r, want %r" % (name, got, want))

    check("auc perfect", auc([3, 4], [1, 2]), 1.0)
    check("auc reversed", auc([1, 2], [3, 4]), 0.0)
    check("auc all ties", auc([1, 1], [1, 1, 1]), 0.5)
    check("auc half", auc([1, 3], [2]), 0.5)
    check("auc binary", auc([1, 1, 0, 0], [0, 0, 0, 1]), 0.625)
    check("auc empty", auc([], [1]), None)
    check("strat drops cross pairs", strat_auc([([5], [4]), ([1], [0])]), 1.0)
    check("strat weights", strat_auc([([1], [0, 2]), ([1, 1], [0])]), (0.5 * 2 + 1.0 * 2) / 4)
    rng = random.Random(1)
    lo, hi = boot_ci([3, 4, 5], [1, 2, 3], False, rng)
    check("ci brackets", lo <= auc([3, 4, 5], [1, 2, 3]) <= hi, True)
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as t:
        t.write("[x][INFO][SCORE_PROCESSING] - LINES: 2 of 3 cameras' lines solved this dart (cam 2: no usable axis: "
                "not straight: the kept)\n")
        t.write("[x][INFO][SCORE_PROCESSING] - LONE-WIRE: camera 1's S20 is 13.7 mm from a wedge wire\n")
        t.write('[x][INFO][TURNAUSCLIENT] - TURNAUS: COUNTED {"reference":"R1","sector":"S1"}\n')
        t.write("[x][INFO][SCORE_PROCESSING] - Consensus score: S7 from 2 cameras\n")
        t.write('[x][INFO][TURNAUSCLIENT] - TURNAUS: COUNTED {"reference":"R2","sector":"S7"}\n')
        name = t.name
    got = read_log(name)
    os.unlink(name)
    check("log R1 fault", (got["R1"]["log_lines_failed"], got["R1"]["log_line_fault"]), (1, "not-straight"))
    check("log R1 disagrees", got["R1"]["log_vote_disagrees"], 1)
    check("log R2 agrees", (got["R2"]["log_vote_disagrees"], got["R2"]["log_vote_cams"]), (0, 2))
    print("I1846 SELF-TEST %s (%d failed)" % ("ok" if not fails else "FAILED", fails))
    return 1 if fails else 0


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if not args:
        sys.stderr.write(__doc__)
        return 2
    return census(args)


if __name__ == "__main__":
    sys.exit(main())

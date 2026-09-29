#!/usr/bin/env python3
# unrun-tester: reads the log of testers/i1683_realtime.sh, which is load-dependent by design and so never a gate; run by hand beside it.
"""#1683: the darts a replay of mocks/rig-20260929 published, visit by visit, against
GROUND-TRUTH.md, with the video frame each window opened on.

    python3 testers/i1683_visits.py <out.txt> [GROUND-TRUTH.md]

Works on a real-time replay (OD_REALTIME_REPLAY=on) and on a capture-clock one. The frame
of a window comes from the replay itself: the capture logs `I1683 SCORING STARTS after N
capture read(s)`, and a window's `opened` cycle is read `opened + N - 1` (on the
capture clock that is frame `opened + 88` with N = 90, the fixture's opening window).
Under the real-time replay the read's frame is the one its `I1683RT` line names; on the
capture clock it is the read's ordinal minus one.

Output: one line per visit, `v<k> truth=<...> published=<...> landed=<a> got=<b> exact=<c>`,
where `got` counts published darts matched to a landed dart of the visit by score, and
then the per-read timing: median and 90th-percentile processing time per cycle and the
share of cycles that skipped a frame.
"""
import re
import statistics as st
import sys


def truth_visits(path):
    text = open(path, errors='replace').read()
    # the truth is the file's first line of comma-separated visits
    first = next((l for l in text.splitlines() if l.count(',') >= 2), None)
    if first is None:
        return None
    out = []
    for v in first.strip().strip('`').split(','):
        darts = []
        for d in v.split():
            d = d.lower()
            if d == 'miss':
                darts.append('MISS')
            elif d[0] in 'td':
                darts.append(d[0].upper() + d[1:])
            elif d in ('bull', 'db', 'ob', '25', '50'):
                darts.append(d.upper())
            else:
                darts.append('S' + d)
        out.append(darts)
    return out


def main():
    log = sys.argv[1]
    gt = sys.argv[2] if len(sys.argv) > 2 else 'mocks/rig-20260929/GROUND-TRUTH.md'
    lines = open(log, errors='replace').read().splitlines()
    reads_before = None
    frame_of_read = {}
    proc, skipped = [], []
    opened = {}
    visits = [[]]
    pub = None
    for l in lines:
        m = re.search(r'I1683 SCORING STARTS after (\d+) capture read', l)
        if m and reads_before is None:
            reads_before = int(m.group(1))
        m = re.search(r'I1683RT read=(\d+) frame=\[([-\d,]+)\] skip=\[([-\d,]+)\] proc_ms=(\S+)', l)
        if m:
            fr = [int(x) for x in m.group(2).split(',')]
            frame_of_read[int(m.group(1))] = max(fr)
            p = float(m.group(4))
            if p >= 0:
                proc.append(p)
            skipped.append(max(int(x) for x in m.group(3).split(',')))
            continue
        m = re.search(r'I1511AXIS window=(\d+) opened=(\d+)', l)
        if m and m.group(1) not in opened:
            opened[m.group(1)] = int(m.group(2))
        m = re.search(r'I1555PUBLISH window=(\d+) path=(\S+) score=(\S+) conf=(\S+) .*outcome=(\S+) vote=(\S+)', l)
        if m:
            pub = m.groups()
            continue
        m = re.search(r'I1556PUBLISH window=(\d+) .*flagged=(\d) score=\S+ alt=(\S+)', l)
        if m and pub:
            w, path, sc, conf, oc, vote = pub
            o = opened.get(w)
            frame = None
            # a log from before #1683 does not say; 90 is the fixture's opening window
            nb = reads_before if reads_before is not None else 90
            if o is not None:
                r = o + nb - 1
                frame = frame_of_read.get(r, r - 1 if not frame_of_read else None)
            alt = ('~' + m.group(3)) if m.group(2) == '1' else ''
            visits[-1].append((sc, frame, alt))
            pub = None
        if 'SCORE: END' in l:
            visits.append([])
    if visits and not visits[-1]:
        visits.pop()

    truth = truth_visits(gt) or []
    total_landed = total_got = total_exact = total_pub = 0
    n = max(len(truth), len(visits))
    print('I1683 VISITS log=%s mode=%s' % (log, 'realtime' if frame_of_read else 'capture'))
    for k in range(n):
        t = truth[k] if k < len(truth) else []
        p = visits[k] if k < len(visits) else []
        landed = [d for d in t if d != 'MISS']
        pool = list(landed)
        got = 0
        for sc, _, _ in p:
            if sc in pool:
                pool.remove(sc)
                got += 1
        exact = 1 if [s for s, _, _ in p] == landed else 0
        total_landed += len(landed)
        total_got += got
        total_exact += exact
        total_pub += len(p)
        ps = ' '.join('%s%s@f%s' % (sc, alt, fr if fr is not None else '?') for sc, fr, alt in p) or '-'
        print('  v%-2d truth=%-16s published=%-60s landed=%d published_n=%d right=%d' % (
            k + 1, ' '.join(t), ps, len(landed), len(p), got))
    print('I1683 TOTAL landed=%d published=%d right=%d visits_exact=%d/%d' % (
        total_landed, total_pub, total_got, total_exact, n))
    if proc:
        q = st.quantiles(proc, n=10)
        print('I1683 TIMING cycles=%d proc_ms median=%.1f p10=%.1f p90=%.1f max=%.1f skipped_cycles=%.0f%% frames_skipped=%d' % (
            len(proc), st.median(proc), q[0], q[8], max(proc),
            100.0 * sum(1 for s in skipped if s > 0) / len(skipped), sum(skipped)))


if __name__ == '__main__':
    main()

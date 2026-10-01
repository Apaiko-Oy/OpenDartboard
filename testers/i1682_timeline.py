#!/usr/bin/env python3
# unrun-tester: reads the motion trace a #1682 replay leaves (testers/i1682_run.sh); a tool a person or agent runs to take the census, not a check of the tree.
"""#1682: the multi-dart window census, from a motion trace (OD_TRACE).

A window "holds two darts" when its fresh diff carries two arrivals: either two bursts
inside one motion event, or a burst that opened no event (under the entry, or in cooldown)
CARRIED into the next window, whose diff is cut against the last called dart. Each
camera's count is the number of those bursts in which that camera cleared --burst.

A motion-trace timeline. Every motion event (IDLE->SPIKE ... ->COOLDOWN) is one
window; every burst (cycles where some camera's board ratio clears BURST) is a candidate
arrival. Prints each window with the bursts inside it and each camera's burst count, and
every burst that opened no window (and why: IDLE under the entry, or in cooldown).

  timeline.py <run>.trace.csv <run>.txt [--burst 0.004] [--gap 3]
"""
import csv, re, sys, argparse
ap = argparse.ArgumentParser()
ap.add_argument("trace"); ap.add_argument("log")
ap.add_argument("--burst", type=float, default=0.004)
ap.add_argument("--gap", type=int, default=3)
ap.add_argument("--summary", action="store_true")
a = ap.parse_args()
rows = [r for r in csv.DictReader(open(a.trace))]
for r in rows:
    r["cycle"] = int(r["cycle"]); r["si"] = int(r["state_in"]); r["so"] = int(r["state_out"])
    r["r"] = [float(r["r0"]), float(r["r1"]), float(r["r2"])]
# events: start where so==1 and si in (0,4); end where so==4 and si in (2,3) (a window) or so==0 from 1 (abandoned)
events = []; cur = None
for r in rows:
    if r["so"] == 1 and r["si"] in (0, 4):
        cur = {"start": r["cycle"], "end": None, "kind": None}
    if cur and r["si"] in (1, 2, 3) and r["so"] == 4:
        cur["end"] = r["cycle"]; cur["kind"] = "window"; events.append(cur); cur = None
    elif cur and r["si"] in (1, 2) and r["so"] == 0:
        cur["end"] = r["cycle"]; cur["kind"] = "abandoned"; events.append(cur); cur = None
if cur:
    cur["end"] = rows[-1]["cycle"]; cur["kind"] = "unfinished"; events.append(cur)
# bursts
bursts = []; b = None
for r in rows:
    hot = [x > a.burst for x in r["r"]]
    if any(hot):
        if b and r["cycle"] - b["last"] <= a.gap:
            b["last"] = r["cycle"]
        else:
            b = {"start": r["cycle"], "last": r["cycle"], "peak": [0, 0, 0], "cams": [False]*3,
                 "state": r["si"], "camfirst": [None]*3}
            bursts.append(b)
        for i in range(3):
            b["peak"][i] = max(b["peak"][i], r["r"][i])
            if hot[i]:
                b["cams"][i] = True
                if b["camfirst"][i] is None: b["camfirst"][i] = r["cycle"]
# windows from the log, in order: serial -> published / opened cycle
log = open(a.log, errors="replace").read()
pub = {int(m.group(1)): m.group(2) for m in re.finditer(r"I1555PUBLISH window=(\d+) path=\S+ score=(\S+)", log)}
serials = sorted(set(int(x) for x in re.findall(r"window=(\d+)", log)))
nwin = sum(1 for e in events if e["kind"] == "window")
print("# events=%d windows=%d abandoned=%d log_max_window=%s bursts=%d" % (
    len(events), nwin, sum(1 for e in events if e["kind"] == "abandoned"), max(serials) if serials else "-", len(bursts)))
wi = 0
def inside(bb, e): return e["start"] <= bb["start"] <= e["end"]
used = set()
prev_end = -1
for e in events:
    if e["kind"] == "window":
        wi += 1
    inb = [k for k, bb in enumerate(bursts) if inside(bb, e) or (bb["start"] < e["start"] <= bb["last"])]
    used.update(inb)
    carried = [k for k, bb in enumerate(bursts) if prev_end < bb["start"] < e["start"] and k not in inb
               and not any(inside(bb, o) for o in events)]
    prev_end = e["end"]
    inb = carried + inb
    counts = [sum(1 for k in inb if bursts[k]["cams"][i]) for i in range(3)]
    label = ("w%d %s" % (wi, pub.get(wi, "(no dart)"))) if e["kind"] == "window" else e["kind"].upper()
    flag = "  <== %d BURSTS" % len(inb) if len(inb) > 1 else ""
    if not a.summary or len(inb) > 1:
        print("%-16s cyc %5d-%5d bursts=%d cam_bursts=%s%s" % (label, e["start"], e["end"], len(inb), "/".join(map(str, counts)), flag))
        if len(inb) > 1 or not a.summary:
            for k in inb:
                bb = bursts[k]
                print("      %s burst" % ("carried" if k in carried else "inside "), end="")
                print(" %5d-%5d len=%2d peak=%s cams=%s state_at_start=%d" % (
                    bb["start"], bb["last"], bb["last"]-bb["start"]+1, "/".join("%.4f" % p for p in bb["peak"]),
                    "".join("123"[i] if bb["cams"][i] else "." for i in range(3)), bb["state"]))
for k, bb in enumerate(bursts):
    if k in used: continue
    st = {0: "IDLE (under the entry)", 4: "COOLDOWN", 1: "SPIKE", 2: "STAB"}.get(bb["state"], str(bb["state"]))
    print("NO WINDOW burst %5d-%5d len=%2d peak=%s cams=%s in %s" % (bb["start"], bb["last"], bb["last"]-bb["start"]+1,
          "/".join("%.4f" % p for p in bb["peak"]), "".join("123"[i] if bb["cams"][i] else "." for i in range(3)), st))

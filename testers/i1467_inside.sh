#!/bin/bash
# #1467, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1467.
#
# It never ends on an `echo`: #1463 is open about six scripts that do, and a tester whose
# last statement is an echo exits 0 whatever it measured. This one ends on `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
LOOKS="${LOOKS:-3}"
SPACING="${SPACING:-90}"

build_census() { # $1 tree root, $2 output binary
  g++ -std=c++17 -O1 -I "$1/src" -I "$1/src/utils" -I "$1/src/detector/geometry/calibration" \
    -o "$2" "$1/testers/i1467_fit_census.cpp" \
    "$1"/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
    > "$2.build.log" 2>&1
}

collect() { # $1 binary, $2 out-file ; the environment the caller set decides the path
  : > "$2"
  local c
  for c in 1 2 3; do "$1" "$SRC/mocks/cam_$c.mp4" $((c-1)) "$LOOKS" "$SPACING" 2>/dev/null | grep -E '^I1467' >> "$2"; done
  for c in 1 2 3; do "$1" "$SRC/mocks/rig-20260918/cam_$c.mp4" $((c-1)) "$LOOKS" "$SPACING" 2>/dev/null | grep -E '^I1467' >> "$2"; done
}

echo "=== building the fit census ==="
if ! build_census "$SRC" /run1467/census; then
  tail -30 /run1467/census.build.log
  echo "FAIL the fit census did not build; nothing below measures anything"
  exit 2
fi

# #1295: the preconditions are ASSERTED rather than relied on. A census run against a
# fixture that is not there produces no rows, and a section counting rows it does not
# have passes by having nothing to disagree with.
for f in mocks/cam_1.mp4 mocks/cam_2.mp4 mocks/cam_3.mp4 \
         mocks/rig-20260918/cam_1.mp4 mocks/rig-20260918/cam_2.mp4 mocks/rig-20260918/cam_3.mp4; do
  if [ ! -s "$SRC/$f" ]; then
    echo "FAIL $f is not in this tree, so nothing below measures what it claims to"
    exit 2
  fi
done
EXPECT=$((6 * LOOKS))

# The threshold section 5 judges against, read from the tree rather than restated here.
/run1467/census 2>/dev/null | sed -n 's/^I1467MIN min=\([^ ]*\).*/\1/p' | head -1 > /run1467/min.txt
if [ ! -s /run1467/min.txt ]; then
  echo "FAIL the census did not state this tree's coherence minimum, so section 5 judges against nothing"
  exit 2
fi
echo "This tree refuses a fit below coherence $(cat /run1467/min.txt)"

echo
echo "=== 1. the model's ring, on every look of both fixtures ========================"
collect /run1467/census /run1467/model.txt
grep '^I1467 ' /run1467/model.txt
ROWS=$(grep -c '^I1467 ' /run1467/model.txt)
if [ "$ROWS" -eq "$EXPECT" ]; then
  say "OK   $ROWS rows, which is $LOOKS looks on each of six cameras" ok
else
  say "FAIL $ROWS rows where $EXPECT were expected -- a clip was left out, and a census that skips a fixture is not a control" no
fi

# Only the looks that HAVE a doubles ellipse can be asked about a plane: the fit is
# strictly downstream of it, and #1467 says so in its own words. The looks without one
# are counted and named rather than quietly dropped.
WITH=$(grep '^I1467 ' /run1467/model.txt | grep -c ' doubles=1 ')
NODOUBLES=$((ROWS - WITH))
OK20=$(grep '^I1467 ' /run1467/model.txt | grep ' doubles=1 ' | grep -c ' wires=20 ok=1 ')
say "     $WITH of $ROWS looks fitted a doubles ellipse; $NODOUBLES did not and are outside this model by construction" ok

# THE PRECONDITION, ASKED RATHER THAN ASSUMED (#1295). The model's conic has to BE the
# doubles ring, and `wire_processing::conicOfDoublesFor` is the stage's own verdict on
# that -- it holds the ray tracer's ellipse against #1423's ring identity and the census
# prints `conic=1` where the two agree. A look where they do not agree has a collapsed
# ellipse fit, which is #1378's own failure and is on mocks/cam_1 in this tree, and is a
# camera this stage should refuse on its own terms. So the two populations are counted
# apart and the refusal of the second is asserted rather than waved through. This is not
# a grandfather list: the partition is a measurement the tree makes, printed per row, and
# a tree that stopped making it would fail the first branch instead.
SOUND=$(grep '^I1467 ' /run1467/model.txt | grep ' doubles=1 ' | grep -c 'conic=1$')
UNSOUND=$((WITH - SOUND))
SOUND_OK=$(grep '^I1467 ' /run1467/model.txt | grep ' doubles=1 ' | grep 'conic=1$' | grep -c ' wires=20 ok=1 ')
UNSOUND_OK=$(grep '^I1467 ' /run1467/model.txt | grep ' doubles=1 ' | grep -v 'conic=1$' | grep -c ' ok=1 ')
say "     of those $WITH, $SOUND have a conic the ring identity agrees is the doubles ring and $UNSOUND do not" ok
if [ "$SOUND" -gt 0 ] && [ "$SOUND_OK" -eq "$SOUND" ]; then
  say "OK   $SOUND_OK of $SOUND looks with a sound conic returned a TRUSTED twenty" ok
else
  say "FAIL only $SOUND_OK of $SOUND looks with a sound conic returned a trusted twenty" no
fi
if [ "$UNSOUND_OK" -eq 0 ]; then
  say "OK   and none of the $UNSOUND whose conic is not the doubles ring was allowed to calibrate" ok
else
  say "FAIL $UNSOUND_OK of $UNSOUND looks whose conic is not the doubles ring still calibrated" no
fi

# Twenty boundaries on top of each other is not a board's ring. The smallest image-space
# gap between any two of the twenty a camera shipped is asserted, not the count alone --
# #1466's whole point is that a count of twenty can be the wrong twenty.
python3 - /run1467/model.txt <<'PY'
import sys
rows=[dict(kv.split("=",1) for kv in r.split() if "=" in kv) for r in open(sys.argv[1]) if r.startswith("I1467 ")]
live=[r for r in rows if r.get("doubles")=="1" and r.get("ok")=="1"]
gaps=[float(r["gaps"]) for r in live if float(r.get("gaps",-1))>0]
if not gaps:
    print("FAIL no ring had a measurable gap"); sys.exit(1)
print("     smallest gap between two of the twenty: min %.2f deg, median %.2f over %d rings"
      % (min(gaps), sorted(gaps)[len(gaps)//2], len(gaps)))
# A board's tightest real neighbours measure 9.03 deg in image space (#1466). Half of
# that is the floor: below it two boundaries are one wire counted twice.
print("OK   every ring's tightest pair clears 4.5 deg" if min(gaps) >= 4.5
      else "FAIL a ring has two boundaries %.2f deg apart, which is one wire twice" % min(gaps))
sys.exit(0 if min(gaps) >= 4.5 else 1)
PY
[ $? -eq 0 ] || FAILED=1

echo
echo "=== 2. the same binary on the counting path, which is what this replaces ======="
# OD_WIRE_MODEL=count is the falsifier, in OD_RING=span's shape: one binary, the path
# chosen at run time, so "a different build" is never a confound.
export OD_WIRE_MODEL=count
collect /run1467/census /run1467/count.txt
unset OD_WIRE_MODEL
grep '^I1467 ' /run1467/count.txt | sed 's/ tilt=.*//'
CWITH=$(grep '^I1467 ' /run1467/count.txt | grep -c ' doubles=1 ')
COK=$(grep '^I1467 ' /run1467/count.txt | grep ' doubles=1 ' | grep -c ' ok=1 ')
say "     counting path: $COK of $CWITH looks with a ring returned twenty; model: $OK20 of $WITH" ok
if [ "$OK20" -ge "$COK" ]; then
  say "OK   the model is no worse than counting on the looks counting already got right" ok
else
  say "FAIL the model returns a ring on $OK20 looks where counting returned one on $COK" no
fi
# NO REGRESSION, asked LOOK BY LOOK rather than as a total: a model that lost one look
# and gained two passes a comparison of counts and is still a regression.
python3 - /run1467/count.txt /run1467/model.txt <<'REG'
import sys
def ok(path):
    out={}
    for l in open(path):
        if not l.startswith("I1467 "): continue
        f=dict(kv.split("=",1) for kv in l.split() if "=" in kv)
        out[(f["clip"],f["look"])] = (f.get("ok")=="1")
    return out
c, m = ok(sys.argv[1]), ok(sys.argv[2])
lost=[k for k in c if c[k] and not m.get(k)]
gained=[k for k in m if m[k] and not c.get(k)]
print("     the model gains %d looks and loses %d against counting, look by look" % (len(gained), len(lost)))
for k in sorted(gained): print("       gained: %s look %s" % (k[0].replace("/app/",""), k[1]))
for k in sorted(lost):   print("       LOST:   %s look %s" % (k[0].replace("/app/",""), k[1]))
print("OK   no look counting got right is refused by the model" if not lost
      else "FAIL the model refuses %d look(s) counting calibrated" % len(lost))
sys.exit(0 if not lost else 1)
REG
[ $? -eq 0 ] || FAILED=1

CASKED=$(grep '^I1467 ' /run1467/count.txt | grep -c ' asked=1 ')
if [ "$CASKED" -eq 0 ]; then
  say "OK   no fit was asked for on the counting path, so the falsifier really falsifies" ok
else
  say "FAIL $CASKED looks still fitted a plane under OD_WIRE_MODEL=count" no
fi

echo
echo "=== 3. what the residual distribution looks like in BOARD space ================"
python3 - /run1467/model.txt <<'PY'
import sys
res=[]
for line in open(sys.argv[1]):
    if not line.startswith("I1467RES "): continue
    f=dict(kv.split("=",1) for kv in line.split() if "=" in kv)
    if f.get("n","0")=="0": continue
    res += [abs(float(x)) for x in f["res"].split(",") if x]
if not res:
    print("FAIL no residuals at all"); sys.exit(1)
res.sort()
n=len(res)
def p(q): return res[min(n-1,int(q*n))]
rms=(sum(r*r for r in res)/n)**0.5
under3=sum(1 for r in res if r<=3.0)
print("     %d candidates pooled: rms %.2f deg, mean %.2f, median %.2f, p90 %.2f, p99 %.2f, max %.2f"
      % (n, rms, sum(res)/n, p(0.5), p(0.9), p(0.99), res[-1]))
print("     %d of %d (%.1f%%) sit within the 3 deg cut" % (under3, n, 100.0*under3/n))
ok = True
# #1467 claims 1.24 deg pooled rms against the 4.33 deg the IMAGE angles carry, and
# that uniform noise on +-9 deg would read 5.2. A distribution that is not much tighter
# than uniform is a fit that found nothing.
if rms >= 3.0:
    print("FAIL pooled rms %.2f deg is no tighter than noise on a sector" % rms); ok=False
else:
    print("OK   pooled rms %.2f deg, well inside the 5.2 deg uniform noise would give" % rms)
if under3/n < 0.75:
    print("FAIL only %.1f%% of candidates are inliers; a ring cannot be generated from that" % (100.0*under3/n)); ok=False
else:
    print("OK   %.1f%% of candidates are inliers at the stated 3 deg cut" % (100.0*under3/n))
sys.exit(0 if ok else 1)
PY
[ $? -eq 0 ] || FAILED=1

echo
echo "=== 4. the control: a fit cannot manufacture its own periodicity ==============="
# If the model had invented the twenty-fold structure, every fold would score alike.
python3 - /run1467/model.txt <<'PY'
import sys
rows=[dict(kv.split("=",1) for kv in l.split() if "=" in l.split()[0] or "=" in kv)
      for l in open(sys.argv[1]) if l.startswith("I1467FOLD ")]
if not rows: print("FAIL no fold rows"); sys.exit(1)
folds=[16,18,19,20,21,22,24]
means={f: sum(float(r["n%d"%f]) for r in rows)/len(rows) for f in folds}
print("     mean coherence by fold over %d looks: " % len(rows) +
      "  ".join("n%d=%.3f" % (f, means[f]) for f in folds))
other=max(means[f] for f in folds if f!=20)
if means[20] > 2.0*other:
    print("OK   twenty scores %.3f against %.3f for the best of the others -- a factor of %.2f"
          % (means[20], other, means[20]/other if other else 999)); sys.exit(0)
print("FAIL twenty scores %.3f and another fold scores %.3f, so this fit is not about a dartboard"
      % (means[20], other)); sys.exit(1)
PY
[ $? -eq 0 ] || FAILED=1

echo
echo "=== 5. the known risk, made loud: the bull is displaced and R is read back ====="
# THE ACCEPTANCE CRITERION, not a footnote. The model rides entirely on the bull centre.
# What makes that survivable is that the fit's own coherence falls with bull error, so
# the failure announces itself in the number it is a failure of.
python3 - /run1467/model.txt <<'PY'
import sys, collections
by=collections.defaultdict(list)
for l in open(sys.argv[1]):
    if not l.startswith("I1467BULL "): continue
    f=dict(kv.split("=",1) for kv in l.split() if "=" in kv)
    by[int(f["d"])].append((float(f["R"]), float(f["maxerr"])))
if not by: print("FAIL no perturbation rows"); sys.exit(1)
print("     worst of eight directions, averaged over %d looks:" % len(next(iter(by.values()))))
print("     %-6s %-8s %-10s %s" % ("d px","mean R","mean maxerr","looks whose worst boundary moves >4 deg"))
rows={}
for d in sorted(by):
    v=by[d]
    mr=sum(x[0] for x in v)/len(v); me=sum(x[1] for x in v)/len(v)
    bad=sum(1 for x in v if x[1] > 4.0)
    rows[d]=(mr,me,bad,len(v))
    print("     %-6d %-8.3f %-10.2f %d/%d" % (d, mr, me, bad, len(v)))
ok=True
# The claim: R FALLS with bull error. Monotone-in-the-large is what is asserted, not
# monotone at every step -- one pixel is inside the noise of a fit on real footage.
if rows[0][0] <= rows[20][0]:
    print("FAIL coherence does not fall with bull displacement at all (%.3f at 0 px, %.3f at 20 px)"
          % (rows[0][0], rows[20][0])); ok=False
else:
    print("OK   coherence falls from %.3f at 0 px to %.3f at 20 px" % (rows[0][0], rows[20][0]))
# And the refusal must SEE it: a displacement that really moves the ring has to take R
# below the minimum this tree refuses at.
import subprocess
MIN=float(open("/run1467/min.txt").read().strip())
broke=[d for d in sorted(rows) if rows[d][2] > 0]
caught=[d for d in broke if rows[d][0] < MIN]
if not broke:
    print("FAIL no displacement, up to 20 px, ever moved a boundary more than 4 deg -- this footage cannot measure the risk"); ok=False
elif rows[max(rows)][0] >= MIN:
    print("FAIL at %d px the ring is wrong on %d looks and R is still %.3f, above the %.2f this tree refuses at: the failure is SILENT"
          % (max(rows), rows[max(rows)][2], rows[max(rows)][0], MIN)); ok=False
else:
    print("OK   the worst displacement measured (%d px) breaks %d looks' rings and takes R to %.3f, under the %.2f minimum: the camera is refused by name"
          % (max(rows), rows[max(rows)][2], rows[max(rows)][0], MIN))
    print("     first displacement that moves a boundary >4 deg: %d px; first that R refuses: %s px"
          % (broke[0], caught[0] if caught else "none"))
sys.exit(0 if ok else 1)
PY
[ $? -eq 0 ] || FAILED=1

echo
echo "=== 6. MUTATION: the same census on a tree whose model has no bull in it ======="
# #1423's shape, and the defect planted is this issue's own claim with the load-bearing
# half removed: H = A rather than A M, which is exactly #1466's affine unprojection and
# was measured THERE as worse than doing nothing (4.80 deg rms against the image angles'
# 4.33). If section 1 can pass on this tree, section 1 was measuring nothing.
rm -rf /run1467/planted
cp -r "$SRC" /run1467/planted 2>/dev/null
python3 - <<'PY'
p="/run1467/planted/src/detector/geometry/calibration/wire_model.cpp"
s=open(p).read()
old="        plane.H = A * M;"
assert old in s, "the mutation's anchor has moved; this section is measuring nothing"
open(p,"w").write(s.replace(old, "        plane.H = A; (void)M;", 1))
print("planted: the board plane is built affine, with no bull in it")
PY
if [ $? -ne 0 ]; then
  say "FAIL the defect could not be planted, so section 1 is unproved" no
elif ! build_census /run1467/planted /run1467/planted_census; then
  tail -20 /run1467/planted_census.build.log
  say "FAIL the planted tree did not build, so section 1 is unproved" no
else
  SRC=/run1467/planted
  collect /run1467/planted_census /run1467/planted.txt
  SRC=/app
  grep '^I1467 ' /run1467/planted.txt | sed 's/ tilt=.*//'
  PWITH=$(grep '^I1467 ' /run1467/planted.txt | grep -c ' doubles=1 ')
  POK=$(grep '^I1467 ' /run1467/planted.txt | grep ' doubles=1 ' | grep -c ' wires=20 ok=1 ')
  if [ "$PWITH" -gt 0 ] && [ "$POK" -eq 0 ]; then
    say "OK   $POK of $PWITH on the planted tree against $SOUND_OK of $SOUND on this one: section 1 could not have passed without the bull" ok
  else
    say "FAIL $POK of $PWITH looks still returned a trusted twenty with the bull taken out of the model, so section 1 proves nothing" no
  fi
fi

echo
exit $FAILED

set -u
FAILED=0
say() { echo "$1"; [ "${2:-no}" = ok ] || FAILED=1; }
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

echo "=== 1. the findings, asked directly, with no camera open ==="
g++ -std=c++17 -O1 -I /app/src/utils -o /run1319/check /app/testers/i1319_format_check.cpp $CVFLAGS || exit 1
/run1319/check
[ $? -eq 0 ] && say "OK   every branch answers as written" ok || say "FAIL the checks did not pass" no

echo
echo "=== 2a. FALSIFY: make the no-format branch unreachable ==="
# The pre-#1319 code, restored in a copy: a read-back code goes through decodeFourCC,
# which returns "" for 0. If the check below still passes, it is not measuring anything.
rm -rf /run1319/mutant && cp -r /app/src/utils /run1319/mutant
python3 - <<'PY'
p='/run1319/mutant/capture.hpp'
s=open(p).read()
s=s.replace('        if (format.report == FormatReport::NotReported)',
            '        if (false) // #1319 mutation: the no-format branch made unreachable')
s=s.replace('        finding.text = where + " negotiated " + format.name + " at " + mode +',
            '        finding.text = where + " negotiated " + decodeFourCC(code) + " at " + mode +')
open(p,'w').write(s)
PY
g++ -std=c++17 -O1 -I /run1319/mutant -o /run1319/mutant_check /app/testers/i1319_format_check.cpp $CVFLAGS || exit 1
/run1319/mutant_check > /run1319/mutant1.txt 2>&1
MRC=$?
grep -E "^(FAIL|     Camera 1 negotiated)" /run1319/mutant1.txt | head -6
if [ $MRC -ne 0 ]; then say "OK   the mutation is caught, and the empty name is back in the sentence" ok
else say "FAIL the no-format checks pass with the branch removed; they measure nothing" no; fi

echo
echo "=== 2b. FALSIFY: make the rate check compare a value against itself ==="
rm -rf /run1319/mutant2 && cp -r /app/src/utils /run1319/mutant2
python3 - <<'PY'
p='/run1319/mutant2/capture.hpp'
s=open(p).read()
s=s.replace('        const double gap = granted - requested;',
            '        const double gap = granted - granted; // #1319 mutation: compared against itself')
open(p,'w').write(s)
PY
g++ -std=c++17 -O1 -I /run1319/mutant2 -o /run1319/mutant2_check /app/testers/i1319_format_check.cpp $CVFLAGS || exit 1
/run1319/mutant2_check > /run1319/mutant2.txt 2>&1
MRC=$?
grep -E "^FAIL" /run1319/mutant2.txt | head -6
if [ $MRC -ne 0 ]; then say "OK   the mutation is caught: a self-comparison never fires" ok
else say "FAIL the rate checks pass when the requested rate is not consulted" no; fi

echo
echo "=== 2c. FALSIFY: drop the floor and hand --fps straight to the camera ==="
# The pre-#1319 code, restored in a copy: whatever --fps says is what the device is
# asked for. On the rig's mode list that selects the 10 fps yuyv422 mode by five frames
# a second, which is the defect. If section 7 still passes with the floor gone, it is
# not measuring the fix.
rm -rf /run1319/mutant3 && cp -r /app/src/utils /run1319/mutant3
python3 - <<'PY'
p='/run1319/mutant3/capture.hpp'
s=open(p).read()
s=s.replace('        return (floor_rate > operator_fps) ? floor_rate : operator_fps;',
            '        return operator_fps; // #1319 mutation: the floor dropped, --fps goes to the camera raw')
open(p,'w').write(s)
PY
g++ -std=c++17 -O1 -I /run1319/mutant3 -o /run1319/mutant3_check /app/testers/i1319_format_check.cpp $CVFLAGS || exit 1
/run1319/mutant3_check > /run1319/mutant3.txt 2>&1
MRC=$?
grep -E "^FAIL" /run1319/mutant3.txt | head -6
if [ $MRC -ne 0 ]; then say "OK   the mutation is caught: without the floor the camera is asked for 15 and lands on 10" ok
else say "FAIL the rate-request checks pass with the floor removed; they measure nothing" no; fi

echo
echo "=== 2d. FALSIFY: make the rate-as-evidence branch unreachable ==="
# With it gone, a working board at 1280x720 @ 30 goes back to being told that three
# uncompressed cameras would want 165.9 MB/s against a bus carrying 35 -- a warning
# about a problem the fix removed, which is what the rig printed and what section 9
# exists to stop. If section 9 still passes with the branch removed, it measures nothing.
rm -rf /run1319/mutant4 && cp -r /app/src/utils /run1319/mutant4
python3 - <<'PY'
p='/run1319/mutant4/capture.hpp'
s=open(p).read()
s=s.replace('            if (each > usbTwoBusMegabytesPerSecond())',
            '            if (false) // #1319 mutation: the rate is no longer read as evidence')
open(p,'w').write(s)
PY
g++ -std=c++17 -O1 -I /run1319/mutant4 -o /run1319/mutant4_check /app/testers/i1319_format_check.cpp $CVFLAGS || exit 1
/run1319/mutant4_check > /run1319/mutant4.txt 2>&1
MRC=$?
grep -E "^FAIL" /run1319/mutant4.txt | head -6
if [ $MRC -ne 0 ]; then say "OK   the mutation is caught: the working board is warned about a bus it is not filling" ok
else say "FAIL the bandwidth checks pass with the branch removed; they measure nothing" no; fi

echo
echo "=== 3. the fps finding, driven through the real capture path ==="
# mocks/rig-20260918 is 30 fps at 1280x720 on all three clips.
OD_MAX_CYCLES=5 /app/build/opendartboard \
  --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
  --width 1280 --height 720 --fps 15 > /run1319/rig15.out 2>&1
OD_MAX_CYCLES=5 /app/build/opendartboard \
  --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
  --width 1280 --height 720 --fps 30 > /run1319/rig30.out 2>&1
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1319/control.out 2>&1
for f in rig15 rig30 control; do sed 's/\x1b\[[0-9;]*m//g' /run1319/$f.out > /run1319/$f.txt; done

grep -E "runs at [0-9]+ fps where" /run1319/rig15.txt | head -3
N=$(grep -cE "Video [0-9]+ runs at 30 fps where --fps says 15" /run1319/rig15.txt || true)
[ "$N" = "3" ] && say "OK   30 fps footage asked for 15 says so, once per clip (3)" ok \
                || say "FAIL expected 3 rate lines, got $N" no
N=$(grep -cE "runs at [0-9]+ fps where" /run1319/rig30.txt || true)
[ "$N" = "0" ] && say "OK   the same footage asked for 30 says nothing" ok \
                || say "FAIL $N rate lines on footage running at the rate it was asked for" no

echo
echo "=== 4. the control: mocks/cam_*.mp4 still calibrates, with no new ERROR or WARN ==="
grep -E "^\[(WARN|ERROR)\]" /run1319/control.txt | sed 's/^/     /' | head -10
N=$(grep -cE "^\[(WARN|ERROR)\]" /run1319/control.txt || true)
[ "$N" = "0" ] && say "OK   no ERROR and no WARN on the control footage" ok \
                || say "FAIL $N ERROR/WARN lines on the control footage" no
N=$(grep -cE "negotiated|uncompressed .* MB/s" /run1319/control.txt || true)
[ "$N" = "0" ] && say "OK   the format verification and the bandwidth claim stay device-only" ok \
                || say "FAIL $N negotiation/bandwidth lines on a file source" no

echo
[ "$FAILED" = "0" ] && echo "i1319 PASSED" || echo "i1319 FAILED"
exit $FAILED

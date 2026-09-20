#!/bin/bash
# #1276: the 1100-cycle mock-footage control, #1257's command unchanged, run by i1276_run.sh.
# No credential exists in the run's cfg/, so the client pushes nothing.
#
# WHAT THIS SCRIPT'S EXIT STATUS CARRIES, which is #1463's standard: the detector's own,
# and nothing else. This is a control rather than a check -- it asserts nothing about what
# was scored, and leaves control.out and control.err in the run directory for a reader --
# but "the binary ran 1100 cycles over the mocks and exited 0" is a claim, and until #1479
# this script threw it away.
set -u
export OD_MAX_CYCLES=1100
BIN=/app/build/opendartboard

# Assert the precondition rather than relying on it: a binary that is not there makes the
# run below bash's own 127, which is not a status the detector chose and reads like one.
if [ ! -x "$BIN" ]; then
  echo "FAIL  $BIN is not in this tree's build/, so this control measured nothing"
  exit 2
fi

"$BIN" --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1276/control.out 2> /run1276/control.err
RC=$?
echo "PROGRAM_RC=$RC"

# run_all.sh prints the first few '^FAIL ' lines of a tester's own log so the gate is
# readable without opening a file, and the detector writes neither shape. Put the reason
# where that grep can see it, as #1412 did for the check it repaired.
if [ "$RC" != 0 ]; then
  echo "FAIL  the control run exited $RC rather than 0"
  tail -20 /run1276/control.err
fi

# The control must exit on what it measured. The harness above it exits on the container's
# status and run_all.sh reads that and nothing else, so ending on `echo "PROGRAM_RC=$?"` --
# an echo returns 0 whatever it printed -- left this control unable to go red however the
# detector died. That is the defect #1412 fixed one directory over (#1479).
exit $RC

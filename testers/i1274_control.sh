#!/bin/bash
# unrun-tester: kept because it is a copy, and unrun because the original is already in the
# gate. It is i1249_control.sh with /run1249 changed to /run1274 and the issue number in
# this line changed, and byte for byte identical otherwise; run_all.sh runs that exact
# command against this exact build as 1249-control. Registering this one would buy a second
# identical 1100-cycle detector run. Reported for the first time by #1430: until then the
# sentence in i1274_run.sh naming it counted as a call.
# #1274: the 1100-cycle mock-footage control, #1259's command unchanged, run by i1274_run.sh.
# No credential exists in the run's cfg/, so the client pushes nothing.
export OD_MAX_CYCLES=1100
/app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1274/control.out 2> /run1274/control.err
echo "PROGRAM_RC=$?"

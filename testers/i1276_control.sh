#!/bin/bash
# #1276: the 1100-cycle mock-footage control, #1257's command unchanged, run by i1276_run.sh.
# No credential exists in the run's cfg/, so the client pushes nothing.
export OD_MAX_CYCLES=1100
/app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1276/control.out 2> /run1276/control.err
echo "PROGRAM_RC=$?"

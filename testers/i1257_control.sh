#!/bin/bash
# #1257: the 1100-cycle mock-footage control, run inside the container by i1257_run.sh.
# #827/#825's command: dev build, --debug, the three mock files, a fresh empty directory,
# stdout and stderr to separate files. No credential exists in the run's cfg/, so the
# client pushes nothing, and the run has no network either way.
export OD_MAX_CYCLES=1100
/app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1257/control.out 2> /run1257/control.err
echo "PROGRAM_RC=$?"

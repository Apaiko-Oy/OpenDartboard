set -u
# #1188's subscriber check with #822's stub, as its merge (8c56dec) ran it: --turnaus-stub,
# the three mock files, 1100 cycles. Run from /app so the tool's defaults resolve.
#
# This is the one phase under phases1247/ that computes a verdict: check_subscribers.py
# exits 0 when every check held and 1 when one did not. The other four record, and say so
# in their own first lines.
cd /app
python3 tools/score_socket/check_subscribers.py --turnaus-stub \
  --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4 --workdir /runs/subs \
  > /runs/subs.out 2> /runs/subs.err
RC=$?
echo "CHECK_RC=$RC"

# The verdict is written to /runs/subs.out inside the run directory, and run_all.sh
# prints the first few '^FAIL ' lines of a tester's OWN log so the gate is readable
# without opening a file. Neither shape the check writes reaches that grep unhelped: it
# indents each failing check by two spaces and ends on 'FAIL: N check(s) did not hold'.
# So put them at column 0, the count first, and truncate -- an expected/got pair here is
# four hundred characters wide and run_all.sh shows four lines.
if [ "$RC" != 0 ]; then
  if grep -qE '^ +FAIL |^FAIL: ' /runs/subs.out 2>/dev/null; then
    grep -E '^FAIL: ' /runs/subs.out | sed 's/^FAIL: /FAIL  /'
    grep -E '^ +FAIL ' /runs/subs.out | sed 's/^ *//' | cut -c1-160
  else
    # No verdict in the file at all: the check itself died before writing one.
    echo "FAIL  check_subscribers.py wrote no verdict; its stderr follows"
    tail -20 /runs/subs.err
  fi
fi

# The phase must exit on what it measured. i1247_run.sh exits on the container's status
# and run_all.sh reads that and nothing else, so ending on `echo` -- which returns 0
# whatever it printed -- made 1188-subscribers unable to fail (#1412). That is #1335's
# defect exactly, one level below where #1335 looked.
exit $RC

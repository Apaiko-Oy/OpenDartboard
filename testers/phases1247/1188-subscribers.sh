set -u
# #1188's subscriber check with #822's stub, as its merge (8c56dec) ran it: --turnaus-stub,
# the three mock files, 1100 cycles. Run from /app so the tool's defaults resolve.
cd /app
python3 tools/score_socket/check_subscribers.py --turnaus-stub \
  --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4 --workdir /runs/subs \
  > /runs/subs.out 2> /runs/subs.err
echo "CHECK_RC=$?"

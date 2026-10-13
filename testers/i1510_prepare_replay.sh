#!/bin/bash
# Local-only derivative: freeze the selected recording's own empty frame at startup.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: $0 truth.json original-clips-directory NEW-output-directory" >&2
  exit 2
fi
TRUTH=$1
SOURCES=$2
OUTPUT=$3
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python3 "$HERE/i1510_replay_report.py" --truth "$TRUTH" --sources "$SOURCES"
START=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["empty_frame_ms"]/1000)' "$TRUTH")
if [ -e "$OUTPUT" ]; then
  echo "FAIL output already exists: $OUTPUT (use a fresh directory)" >&2
  exit 2
fi
mkdir -p "$OUTPUT"
for camera in 1 2 3; do
  ffmpeg -nostdin -hide_banner -loglevel error -n -ss "$START" \
    -i "$SOURCES/cam_$camera.mp4" -map 0:v:0 \
    -vf "tpad=start_duration=$START:start_mode=clone" \
    -c:v libx264 -crf 0 -preset ultrafast -threads 2 -an "$OUTPUT/cam_$camera.mp4"
done
echo "Frozen-start inputs ready. Original timebase retained after $START seconds."
echo "Sources remain unchanged; do not commit these local video derivatives."

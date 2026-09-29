#!/usr/bin/env python3
# unrun-tester: reads the fixtures' MP4 frame-timing tables to state their frame rate (#1655). It is a fact about the footage, run by hand when footage changes; it asserts nothing.
"""#1655: the frame rate the rig's fixtures were recorded at, read off the MP4 container.

The image has no ffprobe (and no python cv2), so this reads the boxes itself: the video
track's mdhd timescale and its stts table, which IS the list of frame durations every
decoder, OpenCV's CAP_PROP_POS_MSEC included, derives presentation times from. The
capture clock (OD_MOTION_CLOCK=capture) is those presentation times, so what this prints
is exactly the period the capture clock advances by, frame to frame.

    python3 testers/i1655_fps_probe.py mocks/rig-20260918/cam_*.mp4 mocks/rig-20260922/cam_*.mp4

One line per file:
    I1655 FPS file=... timescale=T frames=N duration_s=D fps_avg=F deltas_ms={d:count,...}
"""
import struct
import sys


def boxes(buf, start, end):
    off = start
    while off + 8 <= end:
        size, kind = struct.unpack(">I4s", buf[off:off + 8])
        hdr = 8
        if size == 1:
            size = struct.unpack(">Q", buf[off + 8:off + 16])[0]
            hdr = 16
        elif size == 0:
            size = end - off
        if size < hdr:
            return
        yield kind.decode("latin-1"), off + hdr, off + size
        off += size


def find(buf, start, end, path):
    for kind, s, e in boxes(buf, start, end):
        if kind == path[0]:
            if len(path) == 1:
                return s, e
            r = find(buf, s, e, path[1:])
            if r:
                return r
    return None


def probe(path):
    buf = open(path, "rb").read()
    moov = find(buf, 0, len(buf), ["moov"])
    if not moov:
        return None
    for kind, s, e in boxes(buf, *moov):
        if kind != "trak":
            continue
        hdlr = find(buf, s, e, ["mdia", "hdlr"])
        if not hdlr or buf[hdlr[0] + 8:hdlr[0] + 12] != b"vide":
            continue
        mdhd = find(buf, s, e, ["mdia", "mdhd"])
        ver = buf[mdhd[0]]
        if ver == 1:
            timescale, duration = struct.unpack(">IQ", buf[mdhd[0] + 20:mdhd[0] + 32])
        else:
            timescale, duration = struct.unpack(">II", buf[mdhd[0] + 12:mdhd[0] + 20])
        stts = find(buf, s, e, ["mdia", "minf", "stbl", "stts"])
        n = struct.unpack(">I", buf[stts[0] + 4:stts[0] + 8])[0]
        deltas = {}
        frames = 0
        for k in range(n):
            count, delta = struct.unpack(">II", buf[stts[0] + 8 + 8 * k:stts[0] + 16 + 8 * k])
            ms = round(1000.0 * delta / timescale, 3)
            deltas[ms] = deltas.get(ms, 0) + count
            frames += count
        return timescale, frames, duration / float(timescale), deltas
    return None


def main(paths):
    rc = 0
    for p in paths:
        r = probe(p)
        if not r:
            print("I1655 FPS file=%s unreadable (no video track)" % p)
            rc = 1
            continue
        ts, frames, dur, deltas = r
        d = ",".join("%g:%d" % (k, v) for k, v in sorted(deltas.items(), key=lambda kv: -kv[1]))
        print("I1655 FPS file=%s timescale=%d frames=%d duration_s=%.3f fps_avg=%.3f deltas_ms={%s}"
              % (p, ts, frames, dur, frames / dur if dur else 0.0, d))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

// #1392: the gate itself, asked directly, with the numbers the footage produced.
//
// The phases beside this file run the detector on real clips and read what it said. That
// is the evidence that matters, and it cannot answer one question: what happens to a
// camera that is MOUNTED CLOSER. Every clip in this repository was shot from where it was
// shot, and the two ways to fake it both change something else -- i1339_scaled_footage
// resamples, which degrades the colour mask (measured: mocks/cam_1 at 0.60 has its ring
// break into a fragment), and i1392_closer_footage crops, which changes the frame's
// aspect and, past a point, runs the board off its own picture, where ADR-0079 §2 refuses
// it for a different and correct reason.
//
// The question is exact, though, so it can be asked exactly. Mounting the same board at
// half the distance multiplies every length in the picture by two: the ring's pixel count
// by four, the board's span by two, the frame by nothing at all. So the whole claim of
// this issue is one line of arithmetic on one Evidence, and this program is that line
// with the measured numbers in it.
//
// It links board_look.hpp and nothing else -- the header is header-only by construction,
// because DartboardCalibration is fwritten raw -- so it needs no OpenCV and builds in a
// second. Run by testers/phases1392/1392-annulus.sh; it prints every case and exits
// non-zero on the first that is not what it says.
#include "../src/detector/geometry/calibration/board_look.hpp"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int failed = 0;

static void say(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what.c_str());
    if (!ok)
    {
        failed = 1;
    }
}

static const char *name(board_look::Refused r)
{
    switch (r)
    {
    case board_look::Refused::None: return "None";
    case board_look::Refused::NoFrame: return "NoFrame";
    case board_look::Refused::TooMuchRedGreen: return "TooMuchRedGreen";
    case board_look::Refused::FloodedFrame: return "FloodedFrame";
    case board_look::Refused::BoardClipped: return "BoardClipped";
    case board_look::Refused::RingNotTraced: return "RingNotTraced";
    case board_look::Refused::TooMuchRingColour: return "TooMuchRingColour";
    }
    return "?";
}

/** A camera that got all the way to STEP 6.5 with a ring fitted, at 1280x720. */
static board_look::Evidence board(int flood, int ring, double span, int rays)
{
    board_look::Evidence e;
    e.frame_cols = 1280;
    e.frame_rows = 720;
    e.frame_pixels = 1280 * 720;
    e.red_green_pixels = flood;
    e.ring_pixels = ring;
    e.board_span_px = span;
    e.traced_doubles = true;
    e.outer_points = rays;
    e.inner_points = rays;
    return e;
}

/** The same camera with every length in its picture multiplied by k: mounted 1/k as far. */
static board_look::Evidence mountedCloser(const board_look::Evidence &e, double k)
{
    board_look::Evidence n = e;
    n.red_green_pixels = (int)std::lround(e.red_green_pixels * k * k);
    n.ring_pixels = (int)std::lround(e.ring_pixels * k * k);
    n.board_span_px = e.board_span_px * k;
    return n; // the frame does not move, which is the entire point
}

int main()
{
    const board_look::Limits limits;

    // ---- 1. every board camera this repository measured is admitted -----------------
    //
    // Measured on 2026-09-19 on one binary, at STEP 6.5, gates held open. flood is what
    // the colour stage kept on the full frame; ring is countNonZero(masks.doublesMask).
    struct Row { const char *what; int flood; int ring; double span; int rays; };
    const std::vector<Row> cameras = {
        {"mocks/cam_1.mp4", 38532, 32580, 291.0, 97},
        {"mocks/cam_2.mp4", 39731, 34427, 315.0, 103},
        {"mocks/cam_3.mp4", 48380, 43944, 309.0, 109},
        {"rig-20260918/cam_1.mp4", 50853, 28129, 195.0, 61},
        {"rig-20260918/cam_2.mp4", 65242, 32363, 196.0, 88},
        {"rig-20260918/cam_3.mp4", 46859, 31474, 197.0, 64},
    };
    std::printf("=== 1. the six cameras of the two fixtures, at the numbers they measured ===\n");
    for (const Row &r : cameras)
    {
        const board_look::Evidence e = board(r.flood, r.ring, r.span, r.rays);
        std::printf("     %-24s ring %5.1f%% of its board circle, flood %4.1f%% of its frame\n",
                    r.what, board_look::ringFraction(e) * 100.0, board_look::floodFraction(e) * 100.0);
        say(board_look::seesBoard(e, limits),
            std::string(r.what) + " is admitted (" + name(board_look::verdict(e, limits)) + ")");
    }

    // ---- 2. THE ISSUE: the same camera, mounted closer -------------------------------
    //
    // mocks/cam_3.mp4 is the one with the most ring in it, so it is the one that reaches
    // the old line first. Under OD_LOOK=frame it is refused somewhere between 1.5x and
    // 2x closer; under #1392 nothing it says changes at any distance.
    std::printf("\n=== 2. the same board, mounted closer, on the same numbers ================\n");
    const board_look::Evidence far = board(48380, 43944, 309.0, 109);
    for (double k : {1.0, 1.25, 1.5, 1.75, 2.0, 3.0})
    {
        const board_look::Evidence near = mountedCloser(far, k);
        std::printf("     %.2fx closer: ring is %5.1f%% of its board circle and %5.1f%% of its frame\n",
                    k, board_look::ringFraction(near) * 100.0,
                    (double)near.ring_pixels * 100.0 / (double)near.frame_pixels);
        const double moved = std::fabs(board_look::ringFraction(near) - board_look::ringFraction(far));
        say(moved < 1e-9, "at " + std::to_string(k).substr(0, 4) +
                              "x the ring share of its own board circle has not moved at all");
        say(board_look::seesBoard(near, limits),
            "at " + std::to_string(k).substr(0, 4) + "x it is still admitted (" +
                name(board_look::verdict(near, limits)) + ")");
    }

    // ---- 3. the falsifier is held to the old stage's own arithmetic -------------------
    //
    // This half can only be checked where OD_LOOK=frame is set, so the phase script runs
    // this program twice. Under the old measure the SAME camera loses its whole slot
    // somewhere under 2x, which is the defect in one line.
    std::printf("\n=== 3. what the pre-#1392 measure says about the same six evidences ======\n");
    const bool old_stage = board_look::measuredAgainstTheFrame();
    std::printf("     OD_LOOK=frame is %s\n", old_stage ? "SET" : "not set");
    int refusedByDistance = 0;
    for (double k : {1.0, 1.5, 2.0, 3.0})
    {
        const board_look::Evidence near = mountedCloser(far, k);
        const board_look::Refused v = board_look::verdict(near, limits);
        std::printf("     %.2fx closer: %-18s %s\n", k, name(v),
                    board_look::refusal(near, limits).substr(0, 96).c_str());
        if (old_stage && v == board_look::Refused::TooMuchRedGreen)
        {
            refusedByDistance++;
        }
    }
    if (old_stage)
    {
        say(refusedByDistance > 0,
            "under the pre-#1392 measure this board camera is refused outright for being mounted closer");
        say(board_look::verdict(far, limits) == board_look::Refused::None,
            "... and the very same camera, where it is, is admitted -- so the switch is a before/after "
            "and not a refusal machine");
        // The old numbers, to the decimal, on the evidence the old code really had.
        const board_look::Evidence webcam = board(237036, 237036, 300.0, 60);
        say(board_look::refusal(webcam, limits).find("25% of its frame keys as dartboard red or green "
                                                     "and this check allows at most 12%") != std::string::npos,
            "#1318's own webcam, 237036 mask pixels at 1280x720, is refused in #1318's own words at 25% "
            "against 12%");
    }
    else
    {
        say(board_look::verdict(mountedCloser(far, 3.0), limits) == board_look::Refused::None,
            "under #1392 the same camera at 3x closer is admitted, where the old measure refuses it");
    }

    // ---- 4. a shape with no hole in it is refused ------------------------------------
    //
    // The upper control, and it is definitional rather than fitted: a filled shape fills
    // its own smallest enclosing circle. #1318's failure was a face that got a bull and a
    // fitted ring, so this is the case the ring line exists for, and it is refused at every
    // distance for the same reason it is refused at one.
    std::printf("\n=== 4. a filled shape fills its own circle, and is refused at any distance ===\n");
    for (double span : {150.0, 300.0, 600.0})
    {
        board_look::Evidence blob = board(0, 0, span, 100);
        blob.ring_pixels = (int)std::lround(M_PI * span * span * 0.98);
        blob.red_green_pixels = blob.ring_pixels;
        std::printf("     a filled disc of radius %.0f px: ring is %5.1f%% of its own circle -> %s\n",
                    span, board_look::ringFraction(blob) * 100.0, name(board_look::verdict(blob, limits)));
        if (!old_stage)
        {
            say(board_look::verdict(blob, limits) == board_look::Refused::TooMuchRingColour,
                "a filled disc of radius " + std::to_string((int)span) + " px is refused as a ring");
        }
    }

    // ---- 5. the flood ceiling follows the frame, not the board -----------------------
    std::printf("\n=== 5. the flood ceiling is the frame's, and the frame's alone ============\n");
    struct Shape { int w; int h; double ceiling; };
    for (const Shape &f : std::vector<Shape>{{1280, 720, 44.2}, {1280, 960, 58.9}, {720, 720, 78.5}})
    {
        board_look::Evidence e;
        e.frame_cols = f.w;
        e.frame_rows = f.h;
        e.frame_pixels = f.w * f.h;
        const double got = board_look::floodCeiling(e) * 100.0;
        std::printf("     %dx%d: a whole board can account for at most %.1f%% of it\n", f.w, f.h, got);
        say(std::fabs(got - f.ceiling) < 0.1,
            std::to_string(f.w) + "x" + std::to_string(f.h) + " has a ceiling of " +
                std::to_string(f.ceiling).substr(0, 4) + "%");
    }
    {
        // A picture coloured edge to edge is refused on every frame shape there is; a board
        // at the ceiling is admitted on every one. Neither depends on a constant.
        for (const Shape &f : std::vector<Shape>{{1280, 720, 0}, {1280, 960, 0}, {720, 720, 0}})
        {
            board_look::Evidence flooded;
            flooded.frame_cols = f.w;
            flooded.frame_rows = f.h;
            flooded.frame_pixels = f.w * f.h;
            flooded.red_green_pixels = f.w * f.h;
            flooded.board_span_px = 300.0;
            if (!old_stage)
            {
                say(board_look::verdict(flooded, limits) == board_look::Refused::FloodedFrame,
                    "a " + std::to_string(f.w) + "x" + std::to_string(f.h) +
                        " picture coloured edge to edge is refused as flooded");
            }
            board_look::Evidence atCeiling = flooded;
            atCeiling.red_green_pixels = (int)std::lround(f.w * (double)f.h * board_look::floodCeiling(flooded));
            atCeiling.traced_doubles = true;
            atCeiling.ring_pixels = (int)std::lround(M_PI * 300.0 * 300.0 * 0.15);
            if (!old_stage)
            {
                say(board_look::verdict(atCeiling, limits) == board_look::Refused::None,
                    "a " + std::to_string(f.w) + "x" + std::to_string(f.h) +
                        " picture with a board filling the whole ceiling is admitted");
            }
        }
    }

    std::printf("\nCHECK_RC=%d\n", failed);
    return failed;
}

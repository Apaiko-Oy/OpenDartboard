#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// #1318: whether the camera that produced a calibration was looking at a dartboard.
//
// Until this file existed, nothing in the program asked. A camera that OPENED was a
// camera the board scored with, and on a laptop the device at index 0 is the built-in
// webcam: on the maintainer's Windows rig on 2026-09-18 the detector calibrated the
// operator's face as camera 1, wrote a portrait to debug_frames/roi_processing/ and went
// on to score with it. Opening is not the same as being the right camera.
//
// The evidence is taken from the calibration the program already performs, because that
// is the only thing in the system that has actually looked at the picture. Two numbers
// carry it, and both come out of steps 2-6 of calibrateSingleCamera:
//
//   red_green_pixels  the doubles mask, i.e. how much of the picture keys as dartboard
//                     red or dartboard green. A dartboard's doubles and trebles are a
//                     thin annulus around a bull; skin in warm room light is a filled
//                     blob. Measured on the rig: 27,624 pixels for a board camera
//                     against 237,036 for the webcam pointed at a face, in the same run,
//                     at 1280x720.
//
//   outer_points      how many of the 120 rays cast out from the bull found the outer
//                     edge of the doubles ring and survived the ring-width validation.
//                     A dartboard is a closed ring around the bull, so a board camera
//                     scores most of them: 68 and 103 on that same rig. There is no ring
//                     in a picture of a room, so the rays land on whatever is furthest
//                     and the ellipse fit either fails outright or is fitted through a
//                     handful of survivors.
//
// MJPG capability was considered and is deliberately NOT the test. --autocams already
// rejects a camera that will not negotiate MJPG, and that check earns its place -- three
// 1280x720 cameras do not fit uncompressed on one USB bus -- but it is a statement about
// a camera's electronics, not about where it is pointed. Most laptop webcams negotiate
// MJPG perfectly well, so a filter built on it would have admitted the very face this
// issue is about. What separates a board camera from a webcam is that one of them can
// see a dartboard, and the only thing that can answer that is a look at the picture.
//
// ===========================================================================
// #1392: WHAT THOSE PIXELS ARE DIVIDED BY, AND WHY IT IS NOT THE FRAME.
// ===========================================================================
//
// Until #1392 the two numbers above were turned into a verdict by dividing the first of
// them BY THE FRAME. That is a statement about where somebody bolted the camera, not
// about what it is looking at. Ring pixels scale with the square of how much of the
// frame the board fills, so the same board camera that measured 3.0% of its frame
// measures 12.0% behind a lens of twice the focal length, or mounted at half the
// distance -- and 12% was the line. A rig that is merely CLOSER was refused outright,
// the whole camera dropped from the board, with a sentence saying it was not looking at
// a dartboard. #1340's fraction-of-frame census found this one alongside #1393 and
// #1394, and this is the dangerous one: its failure is refusal rather than a fallback.
//
// The headroom was never theoretical. color_processing records boards already enclosing
// 19.4% to 20.6% of the frame on mocks/cam_*.mp4, and the only reason the gate had not
// fired is that a ring is thinner than the disc it encloses -- an accident of ring
// width, not anything the check stated.
//
// SO THE QUESTION IS ASKED ABOUT THE BOARD -- and it turned out to be two questions,
// which the census below settled rather than argument. Both were measured on one binary
// on 2026-09-19, with the gates held open so that every camera reached every stage.
//
//   THE RING, at STEP 6.5, which is the site this issue was filed against.
//   `bull_processing::measureBoard` has already measured a radius by then -- on the FULL
//   frame, at STEP 1, before a region is drawn -- so the doubles mask is asked what share
//   of a circle of that radius it fills:
//
//       ring_of_board_disc = ring_pixels / (pi * board_span_px^2)
//
//   Double every pixel and the numerator goes up by four and so does the denominator; the
//   number does not move. Measured: 11.1% to 14.7% on mocks/cam_*.mp4 and 23.6% to 26.9%
//   on mocks/rig-20260918, against 100% for any shape with no hole in it.
//
//   THE FLOOD, at STEP 1.5, which shares this file's verdict and had been sharing its
//   number. It is asked of everything the colour stage kept on the WHOLE frame, over the
//   whole frame, and that really is a question about the frame: "is this camera's whole
//   picture coloured". What could not stay is the constant. The line is now drawn from
//   `floodCeiling` -- the most of its own frame a WHOLE board could account for, which
//   ADR-0079 §2 bounds and which the frame's own shape decides -- so a board mounted
//   closer is already granted the whole ceiling and cannot walk a camera towards the line.
//
// THE OBVIOUS SHAPE WAS TRIED FIRST AND IT DOES NOT WORK, which is worth writing down
// because it is the thing anybody reading this issue would reach for. Dividing STEP 1's
// numerator -- all the colour on the frame -- by the same board disc puts
// mocks/rig-20260918 at 38.3%, 42.7% and 54.3% against a face at 54.5%: no gap at all.
// The span is that rig's TREBLE ring, 0.78 of its board (#1340's census), so the
// denominator is 0.61 of the disc while the numerator is the whole frame's colour. Two
// different numerators need two different denominators, and pretending otherwise is how
// one constant came to be asked of both in the first place.
//
// THE NUMERATOR IS A MASK AND NOT AN ANNULUS, which is the trap in the arithmetic. A
// dartboard's doubles ring is a ring of known width -- 170 mm outer, 162 mm inner in
// perspective_processing's own PhysicalBoard, so (170^2-162^2)/170^2 = 9.2% of the disc
// it sits in -- but `countNonZero(masks.doublesMask)` counts what the COLOUR mask kept
// and what the morphology then did to it, and #1378 measured that on
// mocks/rig-20260918 the doubles ring drops out of that mask altogether, leaving the
// TREBLE ring as the largest surviving contour. A treble ring against its own enclosing
// circle is (107^2-99^2)/107^2 = 14.4%. Both are rings and both are small; neither is
// 9.2% on the nose, and a threshold derived from the physical annulus alone would be a
// number fitted to a mask nobody measured. The numbers in Limits below are measured on
// both fixtures on one binary, and the physical shares above are what they are checked
// AGAINST rather than what they are taken from.
namespace board_look
{
    /**
     * Pi, named here rather than taken from the POSIX `M_PI`.
     *
     * `M_PI` is a POSIX extension, not standard C++. GCC and Clang define it from
     * `<cmath>` anyway, so #1392 compiled on Linux and on the Pi and nothing said a word
     * -- and MSVC does not, unless `_USE_MATH_DEFINES` is set before the first `<cmath>`
     * in the translation unit. The Windows job of `release.yml` therefore broke at
     * `error C2065` on the two lines below, on the first build after #1392 landed, while
     * the arm64 `.deb` in the same run built clean. #1299's two independent jobs are why
     * that was visible as one failure rather than none.
     *
     * The fix is deliberately a constant in this header rather than `_USE_MATH_DEFINES`
     * in `CMakeLists.txt`. That define would work, and it would put the thing this header
     * needs in a different file, where an unrelated edit removes it and the failure comes
     * back on the one platform nobody here builds by hand. That is the
     * shield-in-another-translation-unit shape #1355 was filed about. This header is the
     * only user of pi in `src/`, so it owns it.
     */
    inline constexpr double kPi = 3.14159265358979323846;

    /**
     * #1392's falsification, in the shape od_fix, #1339, #1340 and #1378 established:
     * one binary, the measure chosen at run time, so "different build" is never a
     * confound.
     *
     * OD_LOOK=frame restores exactly what this file decided before #1392 -- the
     * numerator divided by the FRAME, the 12% line, and the sentence that quoted a share
     * of a frame -- and is how a board mounted closer can be made to lose its whole
     * camera again on the very binary that calibrates it. Anything but that exact word
     * is ignored rather than obeyed.
     */
    inline bool measuredAgainstTheFrame()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_LOOK");
            return e && std::string(e) == "frame";
        }();
        return v;
    }

    /**
     * What one camera's calibration measured, as plain numbers, so that it can be
     * logged, compared between cameras and carried in DartboardCalibration -- which is
     * written to the cache with a raw fwrite, so nothing here may own memory.
     */
    struct Evidence
    {
        int frame_pixels = 0;        // cols * rows of the frame the calibration ran on
        int red_green_pixels = 0;    // white pixels in the doubles mask
        int outer_points = 0;        // validated outer boundary points, of 120 rays
        int inner_points = 0;        // validated inner boundary points
        bool traced_doubles = false; // the doubles ring was fitted at all

        // #1331 / ADR-0079 §2: whether the whole board is in this camera's frame. The
        // board is found on the full frame before any region is drawn around it, and if
        // what the colour stage kept of that frame runs off the frame's own edge then this
        // camera does not see a whole board -- a fact about where the hardware is bolted,
        // not about anybody's aim (ADR-0079 §3). `board_edge_gap` is the smallest distance
        // in pixels from the extremes of that kept colour to any of the four frame edges,
        // so a refusal carries a number rather than a verdict; a gap of 0 or less is a
        // board with part of itself outside the picture.
        bool board_clipped = false;
        int board_edge_gap = 0;

        // #1392: the frame's own shape, because the flood ceiling below is derived from
        // it. `frame_pixels` is cols*rows and is what NoFrame is asked of; these two are
        // what says whether this camera is 16:9, 4:3 or square, which changes how much of
        // a picture the biggest possible whole board can fill.
        int frame_cols = 0;
        int frame_rows = 0;

        // #1392: the ring this camera's ellipse stage was handed, in pixels -- STEP 6.5's
        // `countNonZero(masks.doublesMask)`, which is the carved red/green mask reduced to
        // its LARGEST CONNECTED COMPONENT. Zero until that stage runs. It is kept apart
        // from `red_green_pixels` rather than overwriting it, which is what the code did
        // before this issue: a ring counted into a field called red_green_pixels, beside a
        // frame_pixels that was still the whole picture, is what made "a share of the
        // frame" look like a sensible thing to ask.
        int ring_pixels = 0;

        // #1392: the radius `bull_processing::measureBoard` measured on the FULL frame,
        // which is what `ring_pixels` is divided by. Zero where no board was found at all
        // -- a camera with no board in its picture is refused by the stage that could not
        // find one, in that stage's own words, and has no circle to be a share of.
        double board_span_px = 0.0;
    };

    /**
     * Where the line is drawn. Stated as a struct rather than as constants so that a
     * test can move it and watch the answer move -- a filter nothing can fail is not
     * evidence that anything was filtered.
     */
    struct Limits
    {
        // #1392, THE RING. What share of the circle its own board spans keys as dartboard
        // red or green, asked at STEP 6.5 of the mask the ellipse stage was handed. It is
        // scale-free: double every pixel and the numerator goes up by four and so does the
        // denominator.
        //
        // MEASURED, on one binary, on 2026-09-19, on every fixture this repository has:
        //
        //   mocks/cam_*.mp4        12.2%  11.1%  14.7%      3 of 3 calibrate
        //   mocks/rig-20260918     23.6%  26.9%  25.7%      3 of 3 calibrate
        //   mocks/cam_* at 0.60    43.6%  14.2%  17.3%      camera 1's ring had broken to a
        //                                                   fragment; the WIRE stage refuses
        //                                                   it, at 19 of 20 boundaries
        //   mocks/cam_* cropped    ----   11.1%  13.7%      the same boards in a 720x720
        //     to 720x720                                    frame -- a longer lens
        //   a FILLED shape        100.0%                    by construction: a shape with no
        //                                                   hole fills its own circle
        //
        // The two rows that look like disagreement are the whole reason this is measured
        // rather than derived from millimetres. A doubles ring is (170^2-162^2)/170^2 =
        // 9.2% of the board's disc and a treble ring is (107^2-99^2)/107^2 = 14.4% of its
        // own; every coloured ring on a board together is 15.8% of the board's disc. But
        // the denominator is not the board's disc -- it is `measureBoard`'s span, which
        // #1340's census measured at 0.78 to 1.34 of the fitted board. At 0.78, which is
        // what mocks/rig-20260918 reads because its doubles ring has dropped out of the
        // colour mask and the span is its TREBLE ring, the same 15.8% reads 15.8/0.78^2 =
        // 26.0%. The rig measures 23.6, 26.9 and 25.7. The arithmetic lands on the
        // measurement to within a percentage point, and the measurement is what is used.
        //
        // 0.65 SITS IN THE GAP BETWEEN THE TWO RATHER THAN BESIDE EITHER, in ratio: it is
        // 1.49x the highest board camera anything here can produce and a filled shape is
        // 1.54x it. Against the shipped fixtures alone -- highest 26.9% -- it is 2.4x. It
        // cannot be reached by mounting a board closer, which is what this issue is about,
        // and nothing in it is fitted to a rig.
        //
        // WHAT IT IS FOR, which is not what the sentence it replaces was for. #1318's real
        // failure was a face that got all the way through: a bull was found in it, a
        // doubles ring was fitted through it, and the old check refused it at STEP 6.5 on
        // 237,036 mask pixels over a 1280x720 frame -- 25.7%. A blob with a hole in it
        // fills most of its own circle and a ring does not, so that face is refused here,
        // in terms that do not move when a board is mounted closer. The flood check below
        // catches the other shape of the same failure: a warm room that colours a whole
        // picture, which never reaches this stage because there is no bull in it.
        double max_ring_of_board_disc = 0.65;

        // #1392, THE FLOOD. How much of the whole picture keys as dartboard red or green,
        // asked at STEP 1.5, before a region is drawn. This one really IS about the frame,
        // because the question is "is this camera's whole picture coloured". What moved is
        // the line it is held to: see `floodCeiling` below, which is the largest share of
        // ITS OWN FRAME a whole board could account for if every pixel of it were coloured
        // -- 44.2% on 1280x720, 58.9% on 4:3, 78.5% on a square frame. The line is drawn
        // this far along the gap between that ceiling and a picture coloured edge to edge,
        // which is #1318's own rule for where a line goes, applied to a ceiling that
        // follows the frame instead of to a constant that does not.
        //
        //   line = ceiling + (1 - ceiling) * 0.5   ->   72.1% at 16:9, 89.3% at 1:1
        //
        // MEASURED, on the same binary and the same day:
        //
        //   mocks/cam_*.mp4         4.2%   4.3%   5.2%    of a 1280x720 frame
        //   mocks/rig-20260918      5.5%   7.1%   5.1%
        //   mocks/cam_* at 0.60     2.0%   2.2%   2.9%
        //   mocks/cam_* at 720x720  7.7%   7.6%   9.9%    of a square frame, ceiling 78.5%
        //   a warm room with a face          100.0%       testers/i1318_make_source.cpp
        //   a grey office wall                 0.0%       the other half of that control
        //
        // A constant is what this issue is about, and it is worth one line why a constant
        // could not be kept here even generously. 12% was one. On mocks/rig-20260918 the
        // ring fills 26.9% of the circle its board spans, ADR-0079 §2 lets that circle
        // fill at most 44.2% of a 16:9 frame, so that rig mounted as close as a WHOLE
        // board can be mounted reads 26.9% x 44.2% = 11.9% on the old measure -- against a
        // line of 12%. One tenth of one percentage point, and on a 4:3 sensor the same rig
        // reads 15.8% and loses the camera outright. Nothing the old check stated had
        // anything to do with that margin.
        double flood_share_of_the_gap = 0.5;

        // The pre-#1392 line, kept for OD_LOOK=frame and reachable by nothing else. 12%,
        // sitting in the gap between the two measurements #1318 took -- 3.0% for a board
        // camera and 25.7% for a face, both at 1280x720 in one run -- rather than beside
        // either of them. Both of those numbers are shares of a frame and so both move
        // when a camera moves, which is what #1392 is about; they are kept here because a
        // falsifier that reproduces another issue's numbers to the decimal is restoring
        // the old stage, and one that merely refuses proves nothing.
        double max_red_green_fraction = 0.12;
    };

    /** The area of the circle the board was measured out to, in pixels. */
    inline double boardDiscPixels(const Evidence &e)
    {
        return M_PI * e.board_span_px * e.board_span_px;
    }

    /** A share as a percentage with one decimal, because 5.7 and 6 are different claims. */
    inline std::string percentOf(double share)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", share * 100.0);
        return std::string(buf);
    }

    /**
     * #1392: the largest share of ITS OWN FRAME that a camera looking at a whole dartboard
     * could possibly key as dartboard red or green, derived and not fitted.
     *
     * ADR-0079 §2 already refuses a camera whose board runs off its picture, so the board
     * this camera sees fits inside the frame both ways: its diameter is at most
     * min(cols, rows). Grant it every pixel of its own disc -- a board is half black and
     * white, so this is generous by better than a factor of four -- and the share is
     *
     *     pi * min(W,H)^2 / (4 * W * H)
     *
     * which is 44.2% on 1280x720, 58.9% on 4:3 and 78.5% on a square frame. It is a
     * statement about the frame and about nothing that was measured, so a board mounted
     * closer, or behind a longer lens, cannot walk a camera towards it: the board is
     * already granted the whole ceiling.
     *
     * That is what the flood line is drawn FROM, which is why the flood check is not a
     * constant share of a frame. A constant share is what this issue is about.
     */
    inline double floodCeiling(const Evidence &e)
    {
        const double w = e.frame_cols, h = e.frame_rows;
        if (!(w > 0.0 && h > 0.0))
        {
            return 1.0; // nothing is known about this frame, so nothing is refused on it
        }
        const double shortest = w < h ? w : h;
        return M_PI * shortest * shortest / (4.0 * w * h);
    }

    /** Where the flood line sits: that far along the gap from the ceiling to all of it. */
    inline double floodAllowed(const Evidence &e, const Limits &limits)
    {
        const double ceiling = floodCeiling(e);
        return ceiling + (1.0 - ceiling) * limits.flood_share_of_the_gap;
    }

    /**
     * The pre-#1392 measure, kept whole for OD_LOOK=frame and reachable by nothing else.
     *
     * It is the old arithmetic exactly, including which numerator each site had put in
     * front of it: STEP 1 divided the full frame's kept colour by the frame, and the two
     * stages below it OVERWROTE that field with a mask while leaving the denominator at
     * the whole picture's size -- `masks.doublesMask.total()` is a Mat's full size whether
     * or not anything in it is masked. `ring_pixels` is where that mask now goes, so the
     * old number is reproduced rather than approximated.
     */
    inline double frameFraction(const Evidence &e)
    {
        if (e.frame_pixels <= 0)
        {
            return 0.0;
        }
        const int numerator = e.ring_pixels > 0 ? e.ring_pixels : e.red_green_pixels;
        return (double)numerator / (double)e.frame_pixels;
    }

    /** How much of this camera's whole picture keys as dartboard red or green. */
    inline double floodFraction(const Evidence &e)
    {
        return e.frame_pixels > 0 ? (double)e.red_green_pixels / (double)e.frame_pixels : 0.0;
    }

    /** How much of the circle this camera's own board spans is ring. */
    inline double ringFraction(const Evidence &e)
    {
        const double disc = boardDiscPixels(e);
        return disc > 0.0 ? (double)e.ring_pixels / disc : 0.0;
    }

    /** Whether this camera has a ring to be asked about at all. */
    inline bool ringWasTraced(const Evidence &e)
    {
        return e.traced_doubles && e.ring_pixels > 0 && e.board_span_px > 0.0;
    }

    /** Which of the tests refused this camera, or `None`. */
    enum class Refused
    {
        None,
        NoFrame,
        TooMuchRedGreen, // OD_LOOK=frame only: the pre-#1392 single test
        FloodedFrame,
        BoardClipped,
        RingNotTraced,
        TooMuchRingColour
    };

    inline Refused verdict(const Evidence &e, const Limits &limits = Limits())
    {
        if (e.frame_pixels <= 0)
        {
            return Refused::NoFrame;
        }

        // The whole of the pre-#1392 decision, restored on one branch so that a falsifier
        // reproduces the old stage rather than a modern imitation of it. One test, one
        // number, the frame underneath whichever numerator the calling site had put in
        // front of it.
        if (measuredAgainstTheFrame())
        {
            if (frameFraction(e) > limits.max_red_green_fraction)
            {
                return Refused::TooMuchRedGreen;
            }
            if (e.board_clipped)
            {
                return Refused::BoardClipped;
            }
            if (!e.traced_doubles)
            {
                return Refused::RingNotTraced;
            }
            return Refused::None;
        }

        // Asked first, because it is the one that explains everything below it: a picture
        // that keys as board colour from edge to edge is why the rays found nothing, and
        // "more of this picture is dartboard red and green than a whole board could
        // account for" is the sentence that sends somebody to look at where the camera is
        // pointed rather than at the ellipse fitter.
        //
        // #1392: this is the ONE question in this file that really is about the frame, and
        // it is about the frame because it is the question "is this camera's whole picture
        // coloured". What moved is that the line it is held to is no longer a constant
        // share of a frame -- it is a multiple of what a whole board in THIS frame could
        // account for, which is derived from the frame's own shape and cannot be walked
        // towards by moving a camera nearer a board.
        if (floodFraction(e) > floodAllowed(e, limits))
        {
            return Refused::FloodedFrame;
        }
        // Asked after the flood and before the ring, and the order is the argument. A
        // camera pointed at a face is clipped too -- a face runs off the frame -- and
        // "this picture is coloured edge to edge" is the sentence that sends somebody to
        // the right place. A camera really looking at a board that the frame cuts is not
        // flooded, so it reaches this, and it must not be told instead that no ring could
        // be traced: nothing tried to trace one.
        if (e.board_clipped)
        {
            return Refused::BoardClipped;
        }
        if (!e.traced_doubles)
        {
            return Refused::RingNotTraced;
        }
        // #1392, and this is the site the issue was filed against. A doubles ring is a
        // thin ring around a bull and fills a small share of the circle it sits in; a
        // filled blob fills its own circle. Both halves of that scale together, so this
        // number does not move when the same board is mounted closer -- which the old
        // one, the same count over the frame, did with the square of the distance.
        if (ringWasTraced(e) && ringFraction(e) > limits.max_ring_of_board_disc)
        {
            return Refused::TooMuchRingColour;
        }
        return Refused::None;
        // #1392 removed a `min_outer_points` floor from the end of this function, and it
        // is worth one paragraph because the issue asked for the floor to be revisited
        // rather than for it to go.
        //
        // It was `50`, and the file said out loud that it could not fire: it was
        // `EllipseParams::minValidRays` said a second time, and `validOuterPoints` is
        // only ever written inside the `>= minValidRays` branch of processEllipse, which
        // is the same branch that sets `hasValidDoubles`. So `traced_doubles &&
        // outer_points < 50` is unreachable while the two numbers are equal, and it was
        // kept on the argument that this is where a STRICTER floor would be raised.
        //
        // There is no stricter floor to raise it to that is not fitted, and #1392's census
        // is what settles it rather than the argument above. Measured on one binary on
        // 2026-09-19, the board cameras that CALIBRATE score
        //
        //   61  64  70  88  95  96  97  103  104  105  107  109   of 120 rays
        //
        // and the 61 and the 64 are mocks/rig-20260918's own cameras 1 and 3, on the
        // fixture as it ships. So a floor anywhere above 60 refuses a shipped fixture
        // outright -- which is the failure this file exists to prevent, committed by the
        // guard meant to prevent it -- and the only window left for a floor that can fire
        // at all is 51..60, where nothing this repository has ever measured has landed.
        // Worse again, #1340's fixture -- a doubles ring painted out over 150 degrees,
        // which is 50 of the 120 rays -- is deliberately refused by the WIRE stage, the
        // one that can see the damage, and a floor in the sixties would take that refusal
        // away from it. A number nothing has ever produced, chosen so that a guard can be
        // said to fire, is exactly #1322.
        //
        // So the floor lives in one place, `EllipseParams::minValidRays`, enforced where
        // the fit happens and printed there with its own count. `outer_points` is still
        // carried and still logged, because it is evidence a refusal is argued from; it is
        // no longer a second, identical gate pretending to be a first.
    }

    /**
     * Why this camera is refused, or an empty string when it is not.
     *
     * #1321's rule, which this obeys: the count is asserted against the threshold that
     * refused it in the same sentence, so a line reporting the wrong number can be seen
     * to be wrong without knowing anything about the pipeline.
     */
    inline std::string refusal(const Evidence &e, const Limits &limits = Limits())
    {
        switch (verdict(e, limits))
        {
        case Refused::NoFrame:
            return "no frame to look at";
        case Refused::TooMuchRedGreen:
            // The pre-#1392 sentence, kept WORD FOR WORD, so an OD_LOOK=frame run
            // reproduces the old line and not merely the old verdict.
            return "is not looking at the dartboard: " +
                   std::to_string((int)(frameFraction(e) * 100.0 + 0.5)) +
                   "% of its frame keys as dartboard red or green and this check allows at most " +
                   std::to_string((int)(limits.max_red_green_fraction * 100.0 + 0.5)) +
                   "% -- a board's doubles and trebles are a few per cent of a frame, and skin in warm "
                   "room light is most of one";
        case Refused::FloodedFrame:
            return "is not looking at the dartboard: " + percentOf(floodFraction(e)) +
                   "% of its whole picture keys as dartboard red or green and the biggest board that "
                   "fits in a " +
                   std::to_string(e.frame_cols) + "x" + std::to_string(e.frame_rows) +
                   " frame could account for at most " + percentOf(floodCeiling(e)) +
                   "% if every pixel of it were coloured, so this check allows " +
                   percentOf(floodAllowed(e, limits)) +
                   "% -- skin in warm room light colours a whole picture and a dartboard cannot";
        case Refused::BoardClipped:
            return "is not looking at a WHOLE dartboard: the coloured region that is its doubles ring "
                   "comes within " +
                   std::to_string(e.board_edge_gap) +
                   " px of the edge of its own frame and a board wholly in shot leaves at least 1 -- so "
                   "part of this board is outside this camera's picture, which is where the cameras are "
                   "bolted rather than how they are aimed";
        case Refused::RingNotTraced:
            return "is not looking at the dartboard: no doubles ring could be traced around the bull";
        case Refused::TooMuchRingColour:
            return "is not looking at the dartboard: " + percentOf(ringFraction(e)) +
                   "% of the circle its own board spans -- " +
                   std::to_string((int)(e.board_span_px + 0.5)) +
                   " px of radius -- keys as dartboard red or green and this check allows at most " +
                   percentOf(limits.max_ring_of_board_disc) +
                   "% -- a doubles ring is a thin ring around a bull and fills about an eighth of the "
                   "circle it sits in however close the camera is bolted, and a filled blob fills all "
                   "of one";
        default:
            return "";
        }
    }

    inline bool seesBoard(const Evidence &e, const Limits &limits = Limits())
    {
        return verdict(e, limits) == Refused::None;
    }

    /**
     * What is known on the FULL frame, at STEP 1, before a region is drawn and before
     * anything has looked for a bull or traced a ring.
     *
     * Deliberately NOT `measured()` below. That line carries `ring=`, `outer_points=` and
     * `inner_points=`, all of which are zero this early because nothing has tried -- and a
     * log line that prints a zero for a thing nobody has measured yet is a line something
     * downstream will read. testers/i1331_inside.sh takes `outer_points=` with `head -1`,
     * and this file learned that the expensive way.
     */
    inline std::string measuredOnTheFullFrame(const Evidence &e)
    {
        return "flood=" + std::to_string(e.red_green_pixels) + " of frame " +
               std::to_string(e.frame_cols) + "x" + std::to_string(e.frame_rows) +
               " (" + percentOf(floodFraction(e)) + "%, and a whole board could account for " +
               percentOf(floodCeiling(e)) + "% of a frame this shape)" +
               " board span " + std::to_string((int)(e.board_span_px + 0.5)) + " px" +
               ", so its circle is " + std::to_string((long)(boardDiscPixels(e) + 0.5)) + " px";
    }

    /**
     * The numbers themselves, for the log, so a refusal can be argued with.
     *
     * #1392: THREE shares are printed, always. The flood share and the ring share are
     * the two this file decides on. The third -- what the ring would have been as a share
     * of the frame -- is the number this issue REMOVED from the decision, and it is
     * printed beside the one that replaced it because the whole claim of this issue is
     * that one of them moves when a camera is mounted closer and the other does not. A
     * log that carried only the deciding number would make that unreadable.
     */
    inline std::string measured(const Evidence &e)
    {
        return "flood=" + std::to_string(e.red_green_pixels) + " of frame " +
               std::to_string(e.frame_cols) + "x" + std::to_string(e.frame_rows) +
               " (" + percentOf(floodFraction(e)) + "%, ceiling " + percentOf(floodCeiling(e)) + "%)" +
               " ring=" + std::to_string(e.ring_pixels) + " of board disc " +
               std::to_string((long)(boardDiscPixels(e) + 0.5)) + " at span " +
               std::to_string((int)(e.board_span_px + 0.5)) + " px" +
               " (" + percentOf(ringFraction(e)) + "%; of the frame it would be " +
               percentOf(e.frame_pixels > 0 ? (double)e.ring_pixels / (double)e.frame_pixels : 0.0) + "%)" +
               " fitted=" + std::string(e.traced_doubles ? "traced" : "not traced") +
               " outer_points=" + std::to_string(e.outer_points) +
               " inner_points=" + std::to_string(e.inner_points);
    }
}

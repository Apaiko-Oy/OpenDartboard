#include "dart_processing.hpp"
#include <iostream>
#include <cstdlib>
#include <limits>
#include "logging.hpp"
#include "utils.hpp"
#include "utils/streamer.hpp"

using namespace cv;
using namespace std;

namespace dart_processing
{
    // #1345: the board one camera's changed pixels are counted again inside -- #1339's
    // Region, in the stage that does NOT measure against it. A local cache rather than a
    // reach into motion_processing, so #1339's denominator is read from the same
    // calibration and not touched: the mask is built on first sight of a camera's board
    // and rebuilt only if the board or the frame size moves, so the per-window cost is a
    // bitwise_and.
    struct Region
    {
        bool known = false;
        Mat mask;
        int pixels = 0;
        Size frame;
        RotatedRect edge;
        // #1364: the mask the TIP is searched in -- the board's PHYSICAL extent, the
        // fitted double-edge ellipse scaled by 225.5/170 (a board is 225.5mm to its rim
        // where the double's outer wire is at 170mm). The deciding shares above stay
        // fractions of the scoring area; this wider one exists because clipping the tip
        // search at the double ring cost the mocks' near-edge S7 its tip -- the shaft
        // crossed the edge and the on-board remnant fell under the contour floor --
        // while the search must still exclude the thrower at the frame's edge, which is
        // what #1364 measured the old whole-frame search finding instead of the dart.
        Mat tip_mask;
    };
    static vector<Region> regions;

    static const Region &regionFor(size_t i, const vector<motion_processing::BoardExtent> &boards, Size frame)
    {
        if (regions.size() <= i)
            regions.resize(i + 1);
        Region &r = regions[i];
        const motion_processing::BoardExtent extent = i < boards.size() ? boards[i] : motion_processing::BoardExtent();

        const bool same = r.known == extent.known && r.frame == frame &&
                          r.edge.center == extent.edge.center && r.edge.size == extent.edge.size &&
                          r.edge.angle == extent.edge.angle;
        if (same && (!r.known || !r.mask.empty()))
            return r;

        r = Region();
        r.frame = frame;
        r.edge = extent.edge;
        r.known = extent.known;
        if (!r.known)
        {
            // Not a refusal: this stage decides on the frame either way. It means only
            // that how much of this camera's figure was on the board cannot be said, and
            // the window's account then omits the clause rather than printing a zero.
            log_warning("DART REGION: camera " + to_string(i + 1) + " has no fitted board, so how much "
                        "of its dart figure is on the board cannot be reported");
            return r;
        }
        r.mask = Mat::zeros(frame, CV_8UC1);
        ellipse(r.mask, r.edge, Scalar(255), FILLED);
        r.pixels = countNonZero(r.mask);
        // #1364: the physical board, for the tip search. 225.5/170 is the board's own
        // rim-to-double ratio, not a tuned constant.
        RotatedRect physical = r.edge;
        physical.size.width *= 225.5f / 170.0f;
        physical.size.height *= 225.5f / 170.0f;
        r.tip_mask = Mat::zeros(frame, CV_8UC1);
        ellipse(r.tip_mask, physical, Scalar(255), FILLED);
        if (r.pixels <= 0)
        {
            r.known = false;
            log_warning("DART REGION: camera " + to_string(i + 1) + " fitted a board of no area, so how much "
                        "of its dart figure is on the board cannot be reported");
        }
        return r;
    }

    // #1358 measurement instrument, off unless asked for. The refused-window account
    // (#1350/#1345) prints only when the vote changed nothing, so a window that SCORED
    // says nothing about where its evidence was. OD_WINDOW_CENSUS=1 prints one line per
    // completed window, scored or refused, carrying each camera's changed pixels inside
    // its own fitted board -- which is the figure #1358 is measured in, before and
    // after. It adds no output to an ordinary run.
    static bool windowCensus()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_WINDOW_CENSUS");
            return e && std::string(e) == "1";
        }();
        return v;
    }

    // #1348 falsification switch, off unless asked for -- the shape #1339's and #1358's
    // switches use, so the rule this issue replaced and the one it wrote can be measured
    // on ONE binary and one fixture rather than on two builds. It restores the vote as it
    // was before #1348, both halves of it: the absolute count of 2 whatever the voting
    // population, AND a calibration that never asks the vote's arithmetic -- which is
    // what let a one-voting-camera board beat READY. Anything else, unset included, is
    // the majority rule and the gate.
    //
    // On a board of three slots the two rules agree on every reachable population, and
    // that is a measurement rather than an oversight: a majority of 1, 2 or 3 voters
    // floored at 2 IS 2. So what this switch moves on a fixture is the GATE. The rule's
    // own difference appears at four voters, which `whyNoEventIsPossible` will not let a
    // whole-binary run reach, and is measured by driving processDartState directly --
    // testers/i1348_quorum_check.cpp, the way #1355 measured its fourth camera.
    bool stateQuorumIsAbsolute()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_STATE_QUORUM");
            return e && std::string(e) == "absolute";
        }();
        return v;
    }

    /**
     * #1518's falsification switch, in the od_fix shape #1339, #1348 and #1495
     * established: one binary, the rule chosen at run time, so "different build" is
     * never a confound.
     *
     * `OD_CLEAN_REFERENCE=calibration` restores the rule as it was before #1518: the
     * CLEAN test's reference is the calibration background for ever -- no adoption at a
     * reconciled CLEAN, no reversion vote -- which is the rule #1514 measured wedging
     * mocks/rig-20260922 at DART_3 for 26 windows. Anything else, unset included, lets
     * the reference track the scene.
     *
     * It is a pin and nothing reads it on an ordinary run.
     */
    static bool cleanReferenceIsCalibration()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_CLEAN_REFERENCE");
            return e != nullptr && std::string(e) == "calibration";
        }();
        return v;
    }

    // Static state tracking. #1355: no count is written here -- processDartState sizes
    // it from the frames it was handed, beside the other three per-camera arrays.
    static vector<DartBoardState> previous_states;
    static DartBoardState best_previous_state = DartBoardState::CLEAN;
    static int stability_frame_count = 0;
    static bool initialized = false;

    // Frame averaging state machine - OPTIMIZED
    static bool collecting_frames = false;
    static vector<Mat> accumulated_frames; // Pre-computed sum per camera (CV_32F)
    static int frames_collected = 0;       // cycles in the window
    static bool window_pending = false;    // #1358: an event settled while a window was averaging
    static vector<int> frames_accumulated; // #798: frames each camera really contributed

    // Working backgrounds - one per camera
    static vector<Mat> working_backgrounds;

    // #1518: what CLEAN currently looks like, one grayscale frame per camera. Empty
    // until the first reconciled CLEAN adopts a window's settled frames; until then the
    // calibration background is the reference, exactly as it always was. #1349's
    // working backgrounds one level up: those track the board per DART, this tracks the
    // scene per TAKEOUT, and both move only on the reconciled vote, never on one
    // camera's candidate.
    static vector<Mat> clean_references;

    // #1535: the tips each camera has reported for the darts the VOTE accepted this
    // visit -- the memory isAReReportOfAnEarlierTip reads. Appended after the vote
    // advances the board (a camera is not what calls a dart, #1495's sentence), and
    // cleared at the reconciled CLEAN beside the working backgrounds (#1349's point;
    // deliberately NOT inside the OD_CLEAN_REFERENCE guard, so that pin cannot change
    // this rule's behaviour). A tip found in a window whose vote refused the advance is
    // never recorded: no dart was called, so nothing was reported.
    static vector<vector<Point2f>> reported_tips;

    // #1518: each camera's cumulative board figure from the LAST completed window, which
    // is what a reversion is a fall FROM (readsAsReversion, dart_processing.hpp). -1 is
    // "no previous window", which is no verdict; it is reset to 0 when the reference is
    // adopted, because the adopted scene is by definition what zero change looks like
    // and a fall measured from before the adoption would read a new dart as a departure.
    static vector<int> previous_board_change;

    // Streamers for debugging
    static unique_ptr<streamer> dart_diff_streamer;
    static unique_ptr<streamer> dart_thresh_streamer;
    static unique_ptr<streamer> dart_thresh_diff_streamer;
    static unique_ptr<streamer> dart_tip_streamer;

    // getDartBoardStateName lives inline in the header since #1350.

    // ---- #1492 measurement instrument, off unless asked for --------------------------
    //
    // `detectTipAndCenter` is the whole of what is known about where a tip comes from,
    // and #1492's first half is a measurement rather than a repair: nobody had said what
    // a camera that places a dart 176 mm from its neighbours actually FOUND. So this
    // prints the figure the tip was picked out of -- every contour it admitted, the
    // centroid it measured from, the convex hull it chose from and the point it chose --
    // and, where asked, writes the same thing as a picture.
    //
    //   OD_TIP_CENSUS=1          one I1492TIP line per call, on stdout
    //   OD_TIP_PROBE=<dir>       plus an annotated JPEG of the binary figure per call
    //
    // Neither is read on an ordinary run and neither changes a pixel of what is decided.
    static bool tipCensus()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_TIP_CENSUS");
            return e != nullptr && std::string(e) == "1";
        }();
        return v;
    }

    /**
     * #1492's falsification, in the shape od_fix, #1339 and #1358 established: one
     * binary, the contour floor chosen at run time, so "different build" is never a
     * confound.
     *
     * `detectTipAndCenter` admits a contour of more than 400 px into the point cloud the
     * tip is picked from and drops everything under it. On mocks/rig-20260918 that floor
     * is what leaves a camera holding the dart's FLIGHT alone -- the shaft below it
     * arrives as four fragments of 9 to 328 px, every one of them dropped -- and the
     * "tip" is then a corner of the flight, 250 mm from where the neighbouring cameras
     * put the same dart.
     *
     * OD_TIP_PIECE_FLOOR=<n> pins that number so the obvious repair can be MEASURED
     * rather than argued, on the binary that ships. It is a pin and nothing reads it on
     * an ordinary run: unset, and anything that is not a non-negative number, leaves the
     * floor at 400. #1492 measured `0` and records the answer in
     * testers/i1492_inside.sh -- it moves 22 of the 51 readings, does not reduce the
     * off-board count, and walks three currently-good tips off the bottom of the board --
     * which is why this issue did not change the number.
     */
    static double piecesFloor()
    {
        static double v = []
        {
            const char *e = std::getenv("OD_TIP_PIECE_FLOOR");
            if (e == nullptr || *e == '\0')
            {
                return 400.0;
            }
            char *end = nullptr;
            const double n = std::strtod(e, &end);
            return (end != e && n >= 0.0) ? n : 400.0;
        }();
        return v;
    }

    /**
     * #1495's falsification, in the od_fix shape #1339, #1358 and #1492 established: one
     * binary, the rule chosen at run time, so "different build" is never a confound.
     *
     * `OD_ADVANCE_RESET=per-camera` restores the reference move as it was before #1495 --
     * `working_backgrounds[i] = averaged_frame` inside the per-camera loop, made from
     * THIS camera's own candidate, before the vote that can refuse it. Anything else,
     * unset included, makes the move after the vote and for every camera, from the
     * reconciled final state, which is where #1349 put the CLEAN reset for the same
     * reason.
     *
     * It is a pin and nothing reads it on an ordinary run.
     */
    static bool advanceResetIsPerCamera()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_ADVANCE_RESET");
            return e != nullptr && std::string(e) == "per-camera";
        }();
        return v;
    }

    /**
     * #1494's falsification, in the same od_fix shape. `OD_TIP_FIGURE=union` restores the
     * figure as it was before #1494: every contour between the 400 px floor and the
     * 20,000 px cap, unioned into one point cloud, one hull over the lot. Anything else,
     * unset included, takes the group the LARGEST contour belongs to and no floor at all.
     *
     * It is a pin and nothing reads it on an ordinary run.
     */
    static bool figureIsUnion()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_TIP_FIGURE");
            return e != nullptr && std::string(e) == "union";
        }();
        return v;
    }

    /**
     * #1535's falsification, in the od_fix shape #1339, #1358, #1492, #1494, #1495 and
     * #1518 established: one binary, the rule chosen at run time, so "different build"
     * is never a confound.
     *
     * `OD_TIP_IDENTITY=off` restores the tip machinery as it was before #1535: every
     * found tip is reported, including a "new" tip that is the pixel this camera
     * already reported for an earlier dart of the same visit -- the false second
     * witness that earned rig-20260918 visit 4's off-board dart its S20@0.9 (#1505's
     * measurement; the census is on isAReReportOfAnEarlierTip in dart_processing.hpp).
     * Anything else, unset included, applies the rule.
     *
     * It is a pin and nothing reads it on an ordinary run.
     */
    bool tipIdentityIsOff()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_TIP_IDENTITY");
            return e != nullptr && std::string(e) == "off";
        }();
        return v;
    }

    // ---- #1511: the shaft-axis observation's three switches ---------------------------
    //
    // The observation itself is ALWAYS computed and carried on CameraDetectionResult --
    // it is a struct nothing reads yet, so computing it moves no published byte -- and
    // everything that PRINTS is behind a pin, because a probe nobody asked for is a
    // probe somebody will one day parse by accident (i1510p2_inside.sh's control run
    // asserts the same zero about this census).
    //
    //   OD_SHAFT_CENSUS=1   one I1511AXIS line per camera per window the vote advanced,
    //                       on stdout -- the line i1511's harness parses.
    //   OD_SHAFT_PROBE=<dir>  plus the annotated overlay per camera: support, kept and
    //                       trimmed centreline columns, the fitted axis, the sigma fan.
    //   OD_AXIS_GATE=off    the falsification switch, in the od_fix shape #1339, #1492
    //                       and #1535 established: the fit still MEASURES every gate
    //                       figure but refuses on none of them, so one binary shows the
    //                       gates are load-bearing -- the issue's own required mutation
    //                       ("removing the quality gate must make negative controls
    //                       fail"). Anything else, unset included, leaves the gates on.
    static bool shaftCensusOn()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_SHAFT_CENSUS");
            return e != nullptr && std::string(e) == "1";
        }();
        return v;
    }

    static const std::string &shaftProbeDir()
    {
        static const std::string v = []
        {
            const char *e = std::getenv("OD_SHAFT_PROBE");
            return e != nullptr ? std::string(e) : std::string();
        }();
        return v;
    }

    static bool axisGateIsOff()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_AXIS_GATE");
            return e != nullptr && std::string(e) == "off";
        }();
        return v;
    }

    // The smallest distance between two contours, in pixels. Bounding boxes first: a
    // figure here holds nine contours at its worst (measured over the whole of
    // mocks/rig-20260918: 51 readings, 1 to 9 contours, median 3), and CHAIN_APPROX_SIMPLE
    // leaves tens of points on each, so this is a handful of comparisons -- but the board
    // this runs on is a Pi Zero 2 W and the rejection is free.
    static double gapBetween(const vector<Point> &a, const vector<Point> &b, double no_further_than)
    {
        const Rect ra = boundingRect(a), rb = boundingRect(b);
        const double dx = std::max({0.0, (double)(rb.x - (ra.x + ra.width)), (double)(ra.x - (rb.x + rb.width))});
        const double dy = std::max({0.0, (double)(rb.y - (ra.y + ra.height)), (double)(ra.y - (rb.y + rb.height))});
        if (std::sqrt(dx * dx + dy * dy) > no_further_than)
        {
            return std::numeric_limits<double>::max();
        }
        double best = std::numeric_limits<double>::max();
        for (const Point &p : a)
        {
            for (const Point &q : b)
            {
                const double d = norm(Point2f(p) - Point2f(q));
                if (d < best) best = d;
            }
        }
        return best;
    }

    static const std::string &tipProbeDir()
    {
        static std::string v = []
        {
            const char *e = std::getenv("OD_TIP_PROBE");
            return e != nullptr ? std::string(e) : std::string();
        }();
        return v;
    }

    // #1535: `gap_to_figure`, where asked for, is the distance in px from the chosen tip
    // to the nearest point of the largest fresh-diff contour -- the piece the centroid
    // was measured from. 0 for a tip that is a point of that piece; the census line
    // below has printed the same figure as tipGap since #1494. It is what
    // isAReReportOfAnEarlierTip means by "the fresh change lies elsewhere".
    // #1511: `pieces_out`, where asked for, is the linked figure itself -- the same
    // single-linkage group the tip is picked from -- handed out so the axis observation
    // is fitted to EXACTLY the support the tip machinery read, never to a second
    // segmentation that could quietly disagree with it.
    pair<Point2f, Point2f> detectTipAndCenter(const Mat &binary_thresh, bool debug_mode, int camera_id, vector<Mat> &dart_tips,
                                              double *gap_to_figure = nullptr,
                                              vector<vector<Point>> *pieces_out = nullptr)
    {
        Point2f tip_position(0, 0);
        Point2f center_position(0, 0);

        if (binary_thresh.empty())
        {
            return make_pair(tip_position, center_position);
        }

        // Find ALL contours
        vector<vector<Point>> contours;
        findContours(binary_thresh, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        if (contours.empty())
        {
            return make_pair(tip_position, center_position);
        }

        // #1494 and #1495, which are ONE decision. The figure used to be every contour in
        // a size band, unioned: a 400 px floor at the bottom and a 20,000 px cap at the
        // top. That is two faults pulling against each other. The floor drops a
        // fragmenting dart's own shaft -- on mocks/rig-20260918 it left camera 3 holding
        // dart 3's FLIGHT alone, 1,385 px, with the shaft below it arriving as 9, 16, 146
        // and 328 px and every one dropped, so the published tip was a corner of the
        // flight. And the union lets a SECOND object win the tip -- the hull spans every
        // admitted contour while the centroid is the largest one's alone, so on dart 8
        // camera 1 the furthest hull point was the top of the older dart's shaft. Removing
        // the floor makes the second fault worse (measured: off-board readings 14 -> 16)
        // and restricting the hull to one contour makes the first worse.
        //
        // So neither is a question about SIZE. The figure is the dart, and a contour is
        // part of the dart when it is nearer to the dart than the dart is thick: a shaft
        // fragment is separated from the flight by a gap the thresholding opened, which is
        // smaller than the object it opened it in, and a second dart or a shadow at the
        // rim is not. The tolerance is the largest contour's OWN minimum width -- the
        // short side of its minAreaRect -- so it is read off the figure in hand and is not
        // a number fitted to this fixture (#1322, #1478): the same rule reads a dart at
        // twice the size, and a camera twice as far away, without being told.
        //
        // With membership answered, the floor has nothing left to do and there is none.
        vector<vector<Point>> dart_pieces;
        if (figureIsUnion())
        {
            for (const auto &contour : contours)
            {
                double area = contourArea(contour);
                if (area > piecesFloor() && area < 20000) // More inclusive range for dart pieces
                {
                    dart_pieces.push_back(contour);
                }
            }
            if (dart_pieces.empty())
            {
                return make_pair(tip_position, center_position);
            }
            // Sort by area (largest first)
            sort(dart_pieces.begin(), dart_pieces.end(), [](const vector<Point> &a, const vector<Point> &b)
                 { return contourArea(a) > contourArea(b); });
        }
        else
        {
            vector<vector<Point>> candidates;
            for (const auto &contour : contours)
            {
                if (contourArea(contour) < 20000)
                {
                    candidates.push_back(contour);
                }
            }
            if (candidates.empty())
            {
                return make_pair(tip_position, center_position);
            }
            sort(candidates.begin(), candidates.end(), [](const vector<Point> &a, const vector<Point> &b)
                 { return contourArea(a) > contourArea(b); });

            const RotatedRect seed = minAreaRect(candidates[0]);
            const double reach = std::min(seed.size.width, seed.size.height);
            vector<bool> taken(candidates.size(), false);
            taken[0] = true;
            dart_pieces.push_back(candidates[0]);
            // Single linkage: a fragment may join through a fragment that has already
            // joined, which is how a shaft broken into four pieces comes back whole.
            for (bool grew = true; grew;)
            {
                grew = false;
                for (size_t k = 1; k < candidates.size(); k++)
                {
                    if (taken[k]) continue;
                    for (const auto &piece : dart_pieces)
                    {
                        if (gapBetween(piece, candidates[k], reach) <= reach)
                        {
                            taken[k] = true;
                            dart_pieces.push_back(candidates[k]);
                            grew = true;
                            break;
                        }
                    }
                }
            }
        }

        if (pieces_out != nullptr)
        {
            *pieces_out = dart_pieces;
        }

        // Combine ALL dart pieces into one big point cloud
        vector<Point> all_points;
        for (const auto &piece : dart_pieces)
        {
            all_points.insert(all_points.end(), piece.begin(), piece.end());
        }

        if (all_points.empty())
        {
            return make_pair(tip_position, center_position);
        }

        // Get Hull
        vector<Point> hull;
        convexHull(all_points, hull);

        if (hull.size() < 3)
        {
            return make_pair(tip_position, center_position);
        }

        // Get center of the biggest shape
        vector<Point> biggest_shape = dart_pieces[0];
        Moments m = moments(biggest_shape);
        if (m.m00 == 0)
        {
            return make_pair(tip_position, center_position);
        }

        Point2f biggest_shape_center(m.m10 / m.m00, m.m01 / m.m00);
        center_position = biggest_shape_center;

        // Find the HULL point furthest from the biggest shape's center
        double max_distance = 0;
        Point furthest_hull_point;

        for (const Point &hull_point : hull)
        {
            double distance = norm(Point2f(hull_point) - biggest_shape_center);
            if (distance > max_distance)
            {
                max_distance = distance;
                furthest_hull_point = hull_point;
            }
        }

        if (max_distance > 10) // Minimum distance threshold
        {
            tip_position = Point2f(furthest_hull_point);
        }
        else
        {
            log_debug("No tip found - max hull distance too small: " + to_string(max_distance));
        }

        // #1535: how far the chosen tip sits from the piece the centroid was measured
        // from -- the fresh figure itself. The census below has printed this as tipGap
        // since #1494; it is now computed on every call because the re-report rule
        // decides on it (isAReReportOfAnEarlierTip, dart_processing.hpp).
        double tip_gap = 0;
        if (norm(tip_position) > 0)
        {
            tip_gap = 1e9;
            for (const Point &q : biggest_shape)
            {
                const double d = norm(Point2f(q) - tip_position);
                if (d < tip_gap) tip_gap = d;
            }
        }
        if (gap_to_figure != nullptr)
        {
            *gap_to_figure = tip_gap;
        }

        if (tipCensus() || !tipProbeDir().empty())
        {
            static long probe_seq = 0;
            probe_seq++;
            // What the floor dropped, and what the tip would have been without it: the
            // two questions #1492's evidence turns on, asked of the same call.
            vector<Point> unfloored;
            std::string dropped;
            for (const auto &c : contours)
            {
                const double a = contourArea(c);
                if (a >= 20000) continue;
                unfloored.insert(unfloored.end(), c.begin(), c.end());
                if (a <= piecesFloor())
                {
                    dropped += (dropped.empty() ? "" : ";") + to_string((long)a);
                }
            }
            // Which admitted piece the published tip is a point of. `biggest_shape_center`
            // is measured from piece 0 alone while the hull spans every piece, so a tip
            // that is not a point of piece 0 was found on a DIFFERENT object -- the dart
            // already in the board, a shadow, a second fragment -- and the line from the
            // centroid to it crosses empty space. Exact, and it needs no threshold.
            int tip_piece = -1;
            if (norm(tip_position) > 0)
            {
                for (size_t k = 0; k < dart_pieces.size() && tip_piece < 0; k++)
                {
                    for (const Point &q : dart_pieces[k])
                    {
                        if (q == furthest_hull_point) { tip_piece = (int)k; break; }
                    }
                }
            }

            // tip_gap is computed above, on every call, since #1535.
            Point2f tip_unfloored(-1, -1);
            double unfloored_dist = 0;
            if (unfloored.size() >= 3)
            {
                vector<Point> uh;
                convexHull(unfloored, uh);
                for (const Point &hp : uh)
                {
                    const double dd = norm(Point2f(hp) - biggest_shape_center);
                    if (dd > unfloored_dist) { unfloored_dist = dd; tip_unfloored = Point2f(hp); }
                }
            }

            std::string census_line = "I1492TIP seq=" + to_string(probe_seq) + " cam=" + to_string(camera_id + 1) +
                               " pieces=" + to_string(dart_pieces.size()) +
                               " contours=" + to_string(contours.size()) +
                               " centroid=" + to_string(biggest_shape_center.x) + "," + to_string(biggest_shape_center.y) +
                               " maxdist=" + to_string(max_distance) +
                               " tip=" + to_string(tip_position.x) + "," + to_string(tip_position.y) +
                               " tipNoFloor=" + to_string(tip_unfloored.x) + "," + to_string(tip_unfloored.y) +
                               " maxdistNoFloor=" + to_string(unfloored_dist) +
                               " tipPiece=" + to_string(tip_piece) +
                               // #1494: HOW FAR ACROSS EMPTY SPACE the published line ran --
                               // the distance from the tip to the nearest point of the very
                               // contour the centroid was measured from. `tipPiece` says
                               // whether it left that contour at all; this says by how much,
                               // and it is the figure the repair is judged on, because a
                               // dart's own shaft fragment carrying the tip is right and a
                               // second dart 264 px away is not.
                               " tipGap=" + to_string((long)tip_gap) +
                               " droppedAreas=" + (dropped.empty() ? std::string("-") : dropped) +
                               " areas=";
            for (size_t k = 0; k < dart_pieces.size(); k++)
            {
                census_line += (k ? ";" : "") + to_string((long)contourArea(dart_pieces[k]));
            }
            census_line += " centroids=";
            for (size_t k = 0; k < dart_pieces.size(); k++)
            {
                Moments mk = moments(dart_pieces[k]);
                census_line += (k ? ";" : "") +
                    (mk.m00 > 0 ? to_string((int)(mk.m10 / mk.m00)) + "," + to_string((int)(mk.m01 / mk.m00))
                                : std::string("-"));
            }
            census_line += " hull=";
            for (size_t k = 0; k < hull.size(); k++)
            {
                census_line += (k ? ";" : "") + to_string(hull[k].x) + "," + to_string(hull[k].y);
            }
            std::cout << census_line << std::endl;

            // #1494's instrument: the GEOMETRY the two mechanisms turn on, which the line
            // above cannot carry -- for every contour under the 20,000 px cap, how far it
            // is from the piece the centroid was measured from, and how wide that piece
            // is. A fragment of the same dart and a second dart in the same figure are
            // both "another contour" to the census above; this is what tells them apart,
            // and it is printed so a rule can be chosen from measurement rather than from
            // a number somebody liked.
            if (tipCensus())
            {
                RotatedRect r0 = minAreaRect(biggest_shape);
                const double p0w = std::min(r0.size.width, r0.size.height);
                const double p0l = std::max(r0.size.width, r0.size.height);
                for (const auto &c : contours)
                {
                    const double a = contourArea(c);
                    if (a >= 20000) continue;
                    // `far` and `near` are macros from windows.h -- leftovers of the 16-bit
                    // segmented memory model that MSVC still expands, so a variable named
                    // `far` compiles on Linux, on the Pi and through the whole tester suite
                    // and breaks only on MSVC, with `syntax error: '='` on this line and a
                    // cascade after it. #1355's M_PI, one identifier over. Hence `far_edge`.
                    double gap = 1e9, far_edge = 0, nearest_hull = 1e9;
                    for (const Point &q : c)
                    {
                        for (const Point &b : biggest_shape)
                        {
                            const double d = norm(Point2f(q) - Point2f(b));
                            if (d < gap) gap = d;
                        }
                        const double dc = norm(Point2f(q) - biggest_shape_center);
                        if (dc > far_edge) far_edge = dc;
                        const double dh = norm(Point2f(q) - Point2f(furthest_hull_point));
                        if (dh < nearest_hull) nearest_hull = dh;
                    }
                    std::cout << "I1494PC seq=" << probe_seq << " cam=" << (camera_id + 1)
                              << " area=" << (long)a
                              << " admitted=" << (a > piecesFloor() ? 1 : 0)
                              << " isbiggest=" << (gap <= 0.0 && (long)a == (long)contourArea(biggest_shape) ? 1 : 0)
                              << " gap=" << (long)gap
                              << " far=" << (long)far_edge
                              << " toTip=" << (long)nearest_hull
                              << " p0w=" << (long)p0w << " p0l=" << (long)p0l
                              << std::endl;
                }
            }

            if (!tipProbeDir().empty())
            {
                Mat pic;
                cvtColor(binary_thresh, pic, COLOR_GRAY2BGR);
                vector<vector<Point>> hv = {hull};
                drawContours(pic, hv, -1, Scalar(255, 200, 0), 1);
                for (size_t k = 0; k < dart_pieces.size(); k++)
                {
                    vector<vector<Point>> pv = {dart_pieces[k]};
                    drawContours(pic, pv, -1, Scalar(0, 255, 0), 1);
                }
                circle(pic, biggest_shape_center, 6, Scalar(255, 0, 255), -1);
                if (norm(tip_position) > 0)
                {
                    circle(pic, tip_position, 9, Scalar(0, 0, 255), 2);
                    line(pic, biggest_shape_center, tip_position, Scalar(0, 0, 255), 1);
                }
                imwrite(tipProbeDir() + "/fig_" + to_string(probe_seq) + "_cam" + to_string(camera_id + 1) + ".jpg", pic);
            }
        }

        // Debug visualization
        if (debug_mode)
        {
            Mat debug_img;
            cvtColor(binary_thresh, debug_img, COLOR_GRAY2BGR);

            // Draw convex hull
            if (!hull.empty())
            {
                vector<vector<Point>> hull_vec = {hull};
                drawContours(debug_img, hull_vec, -1, Scalar(255, 0, 0), 2);
            }

            // Draw ALL dart pieces
            for (size_t i = 0; i < dart_pieces.size(); i++)
            {
                Scalar color;
                if (i == 0)
                    color = Scalar(0, 255, 0); // Green for largest
                else if (i == 1)
                    color = Scalar(0, 128, 0); // Less green for second largest
                else if (i == 2)
                    color = Scalar(0, 255, 255); // Yellow for third
                else
                    color = Scalar(255, 0, 255); // Magenta for others

                // Draw filled contour with orange color
                drawContours(debug_img, vector<vector<Point>>{dart_pieces[i]}, -1, color, -1, 8);
                drawContours(debug_img, vector<vector<Point>>{dart_pieces[i]}, -1, Scalar(255, 165, 255), 1);

                // Draw centroid and area
                Moments m = moments(dart_pieces[i]);
                if (m.m00 > 0)
                {
                    Point2f center(m.m10 / m.m00, m.m01 / m.m00);
                    circle(debug_img, center, 4, Scalar(0, 0, 0), -1);

                    int area = (int)contourArea(dart_pieces[i]);
                    putText(debug_img, to_string(area), center + Point2f(8, 0),
                            FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 2);
                }
            }

            // Draw center of biggest shape
            circle(debug_img, biggest_shape_center, 6, Scalar(0, 255, 255), -1);
            putText(debug_img, "CENTER", biggest_shape_center + Point2f(10, 0), FONT_HERSHEY_SIMPLEX, 0.4, Scalar(0, 255, 255), 1);

            // Draw line from center to tip
            if (norm(tip_position) > 0)
            {
                line(debug_img, biggest_shape_center, tip_position, Scalar(0, 0, 255), 2);

                // Draw the final tip (RED)
                circle(debug_img, tip_position, 8, Scalar(0, 0, 255), -1);
                circle(debug_img, tip_position, 8, Scalar(255, 255, 255), 2);
                putText(debug_img, "TIP", tip_position + Point2f(15, -10),
                        FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 0, 255), 2);

                // Add distance info
                putText(debug_img, to_string((int)max_distance) + "px", tip_position + Point2f(15, 10),
                        FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 255), 2);
            }

            // Add detailed summary
            string summary = "Pieces: " + to_string(dart_pieces.size()) +
                             " | Hull: " + to_string(hull.size()) + "pts" +
                             " | Biggest: " + to_string((int)contourArea(biggest_shape)) + "px" +
                             (norm(tip_position) > 0 ? " | TIP FOUND" : " | NO TIP");
            putText(debug_img, summary, Point(10, 25), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 2);

            if (norm(tip_position) > 0)
            {
                string coords = "TIP: (" + to_string((int)tip_position.x) + "," + to_string((int)tip_position.y) +
                                ") | Distance: " + to_string((int)max_distance) + "px";
                putText(debug_img, coords, Point(10, 50), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 255), 2);
            }

            // Save debug image
            odfs::ensureDirectory("debug_frames/dart_processing");
            imwrite("debug_frames/dart_processing/tip_detection_cam_" + to_string(camera_id) + ".jpg", debug_img);

            // Add to debug vector / streams
            dart_tips.push_back(debug_img);
        }

        return make_pair(tip_position, center_position);
    }

    DartStateResult processDartState(const vector<Mat> &current_frames, const vector<Mat> &background_frames,
                                     const vector<motion_processing::BoardExtent> &boards,
                                     bool movement_finished, bool debug_mode, const DartParams &params)
    {
        DartStateResult result;

        // #1355: every per-camera array is sized from the camera count, in one place.
        // `previous_states` was a static brace-initialised with exactly THREE CLEANs and
        // was never sized here beside the other three, while being indexed
        // `previous_states[i]` per REAL camera at four sites in the loop below and
        // iterated as the reconciliation's own bound at a fifth. On a four-camera board
        // the fourth camera was a read and a write past the end, and its state was never
        // reconciled to the vote. What kept that latent is `detectMotion` refusing to
        // initialise on any count but three -- a shield in a different translation unit,
        // which is the kind that disappears in somebody else's unrelated change.
        //
        // Sized on every window rather than only on the first, because `initialized` is
        // set once and never cleared: a count that changed after the first window would
        // otherwise leave all four arrays at the old size. `resize` rather than `assign`
        // so the ordinary case -- a count that never changes -- leaves every camera's
        // state and working background exactly where the last vote put them.
        if (previous_states.size() != current_frames.size() ||
            accumulated_frames.size() != current_frames.size() ||
            frames_accumulated.size() != current_frames.size() ||
            working_backgrounds.size() != current_frames.size() ||
            clean_references.size() != current_frames.size() ||
            previous_board_change.size() != current_frames.size() ||
            reported_tips.size() != current_frames.size())
        {
            previous_states.resize(current_frames.size(), DartBoardState::CLEAN);
            accumulated_frames.resize(current_frames.size());
            frames_accumulated.resize(current_frames.size(), 0);
            working_backgrounds.resize(current_frames.size());
            clean_references.resize(current_frames.size());
            previous_board_change.resize(current_frames.size(), -1);
            reported_tips.resize(current_frames.size());
        }

        // Check if we have initialized
        if (!initialized)
        {
#ifdef DEBUG_VIA_VIDEO_INPUT
            // #812: four more unauthenticated MJPEG listeners on 0.0.0.0. They were
            // behind debug_mode alone, so a release binary run with --debug opened
            // them. The rule is now the same everywhere: a listener needs the debug
            // BUILD and the debug FLAG, not one of the two.
            if (debug_mode)
            {
                // Initialize streamers for debugging (1fps is sufficient for debugging)
                dart_diff_streamer = make_unique<streamer>(8084, 1);
                dart_thresh_streamer = make_unique<streamer>(8085, 1);
                dart_thresh_diff_streamer = make_unique<streamer>(8086, 1);
                dart_tip_streamer = make_unique<streamer>(8087, 1);
            }
#endif

            regions.clear(); // #1345: a fresh board is measured against a fresh region
            initialized = true;
        }

        static long cycle_ordinal = 0;
        static long window_opened_at = 0;
        // #1511: the completed-window serial the axis observation carries as its event
        // identity. Its own counter rather than the window census's, because that one
        // only counts when OD_WINDOW_CENSUS is set and an identity that moves with an
        // instrument pin is not an identity.
        static long window_serial = 0;
        cycle_ordinal++;

        auto openWindow = [&]()
        {
            collecting_frames = true;
            window_opened_at = cycle_ordinal;
            frames_collected = 0;

            // Initialize accumulated frames to zero.
            // #798: a camera whose slot is marked still gets its accumulator zeroed, sized
            // from its own background, so it can join the rest of the window.
            frames_accumulated.assign(current_frames.size(), 0);
            for (size_t i = 0; i < current_frames.size(); i++)
            {
                const Mat &reference = !current_frames[i].empty() ? current_frames[i] : (i < background_frames.size() ? background_frames[i] : current_frames[i]);
                if (!reference.empty())
                {
                    accumulated_frames[i] = Mat::zeros(reference.size(), CV_32F);
                }
            }
        };

        // Frame collection state machine
        if (movement_finished && !collecting_frames)
        {
            openWindow();
        }
        else if (movement_finished)
        {
            // #1358: a second event finished while this window was still averaging its
            // frames, and until now that window was simply DROPPED -- the test above is
            // the only place `movement_finished` is read, and it is false by the next
            // cycle. A window averages `stability_frames` cycles; an event can settle in
            // three. Measured on mocks/rig-20260918: one of the rig's 24 finished events
            // never became a window, and once a cooldown stops swallowing the next throw
            // (see motion_processing) a dart landing three cycles behind another is
            // exactly the case this arm is about. It is remembered rather than served,
            // because serving it here would throw away the frames the window in flight
            // has already averaged.
            window_pending = true;
        }

        if (collecting_frames)
        {
            // Add current frames to accumulation (spreads the cost across frames)
            for (size_t i = 0; i < current_frames.size(); i++)
            {
                if (!current_frames[i].empty() && !accumulated_frames[i].empty())
                {
                    Mat current_gray, float_frame;
                    cvtColor(current_frames[i], current_gray, COLOR_BGR2GRAY);
                    current_gray.convertTo(float_frame, CV_32F);
                    accumulated_frames[i] += float_frame; // Accumulate sum
                    frames_accumulated[i]++;              // #798: per-camera divisor
                }
            }
            frames_collected++;

            if (frames_collected < params.stability_frames)
            {
                return result; // Still collecting, return empty result
            }

            // We have enough frames, process once then stop collecting
            collecting_frames = false;
            // log_info("Frame collection complete - processing");
        }
        else if (!movement_finished)
        {
            return result; // Not collecting and no movement finished, return empty
        }

        // initialise variables
        result.camera_results.resize(current_frames.size());

        // #1511: this window's identity, stamped on every camera's axis observation up
        // front -- an abstention must say WHICH window it abstained from, or the
        // coverage census cannot count it.
        window_serial++;
        for (size_t i = 0; i < result.camera_results.size(); i++)
        {
            result.camera_results[i].axis.camera = (int)i;
            result.camera_results[i].axis.windowOrdinal = window_serial;
            result.camera_results[i].axis.windowOpenedCycle = window_opened_at;
            result.camera_results[i].axis.windowClosedCycle = cycle_ordinal;
        }

        // #1511: each camera's linked figure, kept for the probe overlay; empty where
        // no fresh figure was read.
        vector<vector<vector<Point>>> axis_pieces(current_frames.size());

        // Process all cameras - use pre-computed averages
        vector<DartBoardState> camera_states;

        // #1495: the averaged frame each camera brought to THIS window, kept so the
        // reference that isolates the newest dart can be moved after the vote -- from the
        // reconciled board state -- rather than inside the per-camera loop. See
        // `advanceResetIsPerCamera` and the block below the vote.
        vector<Mat> window_frames(current_frames.size());

        // debuging verctors of frames
        vector<Mat> dart_diffs;
        vector<Mat> dart_threshs;
        vector<Mat> dart_thresh_diffs;
        vector<Mat> dart_tips;

        // #1354: whether ANY camera brings a fitted board to this window. Where one
        // does, a camera without one abstains from the vote rather than answering from
        // the frame; where none does, the frame is all there is and every camera
        // decides on it exactly as this stage always did.
        bool any_board_fitted = false;
        for (const motion_processing::BoardExtent &extent : boards)
        {
            if (extent.known)
            {
                any_board_fitted = true;
            }
        }

        for (size_t i = 0; i < current_frames.size(); i++)
        {
            // #798: a camera that contributed nothing to this window has no average to
            // take. It abstains: no state, no tip, and no vote - rather than being
            // divided by a count of frames it never supplied.
            if (i >= frames_accumulated.size() || frames_accumulated[i] == 0 ||
                accumulated_frames[i].empty() || i >= background_frames.size() || background_frames[i].empty())
            {
                result.camera_results[i].frame_available = false;
                result.camera_results[i].axis.refusal =
                    "no frame: this camera contributed nothing to the window, so there is no figure to fit";
                log_warning("DART: camera " + to_string(i) + " contributed no frames to this window - abstaining");
                if (debug_mode)
                {
                    dart_diffs.push_back(Mat());
                    dart_threshs.push_back(Mat());
                    dart_thresh_diffs.push_back(Mat());
                }
                continue;
            }

            // #1518: the CLEAN reference is the last scene the vote reconciled as CLEAN,
            // and the calibration background only until there has been one. Until #1518
            // the calibration background was the reference for ever, so any permanent
            // scene change after calibration -- mocks/rig-20260922's parked dart, pulled
            // ~1 s in -- sat in every later window's cumulative diff and CLEAN was
            // arithmetically unreachable (#1514's census, 26 windows wedged at DART_3).
            Mat background_gray;
            if (!cleanReferenceIsCalibration() && !clean_references[i].empty() &&
                clean_references[i].size() == accumulated_frames[i].size())
            {
                background_gray = clean_references[i];
            }
            else
            {
                cvtColor(background_frames[i], background_gray, COLOR_BGR2GRAY);
            }

            Mat averaged_frame;
            accumulated_frames[i].convertTo(averaged_frame, CV_8U, 1.0 / frames_accumulated[i]);
            window_frames[i] = averaged_frame;

            // Calculate difference from background
            Mat diff;
            absdiff(averaged_frame, background_gray, diff);

            // clean up the difference image
            // GaussianBlur(diff, diff, Size(5, 5), 1.0); // Softer blending of dart edges
            medianBlur(diff, diff, params.blur_kernel_size);                    // Smooth out noise in grayscale diff
            dilate(diff, diff, Mat(), Point(-1, -1), params.dilate_iterations); // Strengthen dart signals
            erode(diff, diff, Mat(), Point(-1, -1), params.erode_iterations);   // Remove small noise

            // Threshold the difference image
            Mat thresh;
            threshold(diff, thresh, params.background_diff_threshold, 255, THRESH_BINARY);

            // Apply morphological operations to clean up the thresholded image
            Mat morph_kernel = getStructuringElement(MORPH_RECT, Size(params.morph_kernel_size, params.morph_kernel_size));
            Mat morph_kernel2 = getStructuringElement(MORPH_RECT, Size(params.morph_kernel_size / 2, params.morph_kernel_size / 2));
            morphologyEx(thresh, thresh, MORPH_CLOSE, morph_kernel);  // Close small gaps in darts
            morphologyEx(thresh, thresh, MORPH_OPEN, morph_kernel);   // Open small noise
            morphologyEx(thresh, thresh, MORPH_CLOSE, morph_kernel2); // Close smaller gaps in darts
            morphologyEx(thresh, thresh, MORPH_OPEN, morph_kernel2);  // Open smaller noise

            // Count total changed pixels instead of contour analysis
            int total_changed_pixels = countNonZero(thresh);
            int total_pixels = thresh.rows * thresh.cols;
            float change_ratio = ((double)total_changed_pixels / (double)total_pixels) * 100.0f; // Percentage of changed pixels

            // Debug output per camera
            if (debug_mode)
            {
                // Save debug images
                odfs::ensureDirectory("debug_frames/dart_processing");
                imwrite("debug_frames/dart_processing/diff_cam_" + to_string(i) + ".jpg", diff);
                imwrite("debug_frames/dart_processing/thresh_cam_" + to_string(i) + ".jpg", thresh);
                imwrite("debug_frames/dart_processing/averaged_cam_" + to_string(i) + ".jpg", averaged_frame);

                // Store debug frames for visualization
                dart_diffs.push_back(diff);
                dart_threshs.push_back(thresh);
            }

            // #1345 counted the changed pixels again inside this camera's own board as
            // observation -- it measured camera 1 clearing the frame threshold six
            // windows out of six with 12,109 to 13,372 changed pixels and NONE of them
            // on its board, the thrower's shoes at the top of its frame. #1354 makes
            // that count the DECIDING figure where the board is fitted, which is
            // exactly what that measurement was collected to justify.
            const Region &region = regionFor(i, boards, thresh.size());
            int board_changed_pixels = 0;
            int board_pixels = 0;
            if (region.known)
            {
                Mat inside;
                bitwise_and(thresh, region.mask, inside);
                board_changed_pixels = countNonZero(inside);
                board_pixels = region.pixels;
            }

            // set camera result
            result.camera_results[i].total_changed_pixels = total_changed_pixels;
            result.camera_results[i].change_ratio = change_ratio;
            result.camera_results[i].total_pixels = total_pixels;
            result.camera_results[i].board_changed_pixels = board_changed_pixels;
            result.camera_results[i].board_pixels = board_pixels;

            // Crate a working background for this camera
            Mat single_thresh;

            // Determine candidate state. #1354: where this camera's board is fitted,
            // both figures are shares of THAT BOARD -- the cumulative diff against the
            // calibration background says whether the board is occupied at all, and the
            // fresh diff against the working background says whether something NEW
            // arrived since the last dart. Cumulative alone advanced the state on the
            // whole history: with one dart on the board, every later window's
            // cumulative figure still cleared the threshold and voted another dart.
            // Where no camera has a board, the frame figure decides as it always did.
            auto candidate_state = DartBoardState::CLEAN;
            const bool decides_on_board = region.known;
            const double cumulative_share = decides_on_board
                                                ? 100.0 * (double)board_changed_pixels / (double)board_pixels
                                                : change_ratio;
            const double decide_threshold = decides_on_board
                                                ? params.board_change_percent_threshold
                                                : params.change_percent_threshold;

            // #1518: whether this window's cumulative change just REVERTED by at least a
            // dart's worth. The cumulative diff is an absdiff and has no sign, so a
            // takeout on a board whose reference no longer matches the scene --
            // mocks/rig-20260922's calibration holds a parked dart -- leaves the board
            // over the CLEAN ceiling for ever and no takeout can reconcile (#1514: the
            // board wedged at DART_3 for 26 windows). The direction of change still
            // tells: a takeout is a simultaneous dart-sized FALL of this figure on a
            // quorum of cameras, and nothing else in either fixture is (the census is on
            // readsAsReversion in dart_processing.hpp). A falling camera votes CLEAN; if
            // the vote agrees, the adoption below re-bases every reference to this
            // window's settled scene and the ordinary CLEAN test works again.
            bool change_reads_as_removal = false;
            if (!cleanReferenceIsCalibration() && decides_on_board &&
                cumulative_share >= decide_threshold)
            {
                const int ceiling_pixels =
                    (int)((double)board_pixels * params.board_change_percent_threshold / 100.0);
                if (readsAsReversion(previous_board_change[i], board_changed_pixels, ceiling_pixels))
                {
                    change_reads_as_removal = true;
                    log_info("CLEAN BY REVERSION: camera " + to_string(i + 1) +
                             "'s cumulative board change fell from " +
                             to_string(previous_board_change[i]) + " to " +
                             to_string(board_changed_pixels) +
                             " px in one window -- at least a dart-sized departure (the CLEAN "
                             "ceiling is " + to_string(ceiling_pixels) + " px) -- so what it is "
                             "still over the reference by is something that LEFT the board, and "
                             "this camera votes CLEAN (#1518)");
                }
            }
            if (decides_on_board)
            {
                previous_board_change[i] = board_changed_pixels;
            }

            if (any_board_fitted && !decides_on_board)
            {
                // #1354: another camera brought a board and this one did not, so it has
                // no denominator to decide with. It abstains -- the frame figure it
                // would otherwise answer with is how the rig's camera 1 voted DART_1 on
                // the thrower's shoes, six windows of six (#1345).
                result.camera_results[i].abstained_no_board = true;
                result.camera_results[i].axis.refusal =
                    "no fitted board: this camera abstains from the vote (#1354), and a figure "
                    "that cannot be placed on a board is not axis evidence either";
                candidate_state = previous_states[i];
                single_thresh = thresh.clone();
            }
            else if (change_reads_as_removal)
            {
                // #1518: the board's change is a departure, so this camera's candidate
                // is CLEAN however large the remaining figure is -- what is left over
                // the reference is the silhouette of what left the scene. The reconciled
                // vote decides, exactly as it does for every other candidate, and that
                // is load-bearing rather than form: the thrower's shadow produces
                // single-camera falls of up to 133,021 px on rig-20260922 (w08, cam 3),
                // and the quorum is what stops one of those calling a takeout -- the
                // same way it stops one camera calling a dart (#1348, #1349).
                candidate_state = DartBoardState::CLEAN;
                result.camera_results[i].axis.refusal =
                    "departure: this camera's board change reads as a takeout (#1518), and what "
                    "left the board has no arriving axis";
                single_thresh = thresh.clone();
                if (debug_mode)
                {
                    Mat black_image = Mat::zeros(single_thresh.size(), CV_8UC1);
                    imwrite("debug_frames/dart_processing/tip_detection_cam_" + to_string(i) + ".jpg", black_image);
                }
            }
            else if (cumulative_share >= decide_threshold) // the board is occupied
            {
                if (!working_backgrounds[i].empty())
                {
                    Mat diff_working;
                    absdiff(averaged_frame, working_backgrounds[i], diff_working);
                    medianBlur(diff_working, diff_working, params.blur_kernel_size);                    // Smooth out noise in grayscale diff
                    dilate(diff_working, diff_working, Mat(), Point(-1, -1), params.dilate_iterations); // Strengthen dart signals
                    erode(diff_working, diff_working, Mat(), Point(-1, -1), params.erode_iterations);   // Remove small noise
                    threshold(diff_working, single_thresh, params.background_diff_threshold, 255, THRESH_BINARY);
                    morphologyEx(single_thresh, single_thresh, MORPH_CLOSE, morph_kernel);  // Close small gaps in darts
                    morphologyEx(single_thresh, single_thresh, MORPH_OPEN, morph_kernel);   // Open small noise
                    morphologyEx(single_thresh, single_thresh, MORPH_CLOSE, morph_kernel2); // Close smaller gaps in darts
                    morphologyEx(single_thresh, single_thresh, MORPH_OPEN, morph_kernel2);  // Open smaller noise
                }
                else
                {
                    // clone the one above: nothing has been on this board since it was
                    // last clean, so the cumulative diff IS the fresh diff
                    single_thresh = thresh.clone();
                }

                // #1354: what of that is NEW, and on the board where there is one.
                // #1364: the masked image is then also what the TIP is found in --
                // measured on the rig's own footage, camera 1's MISS marker sat at the
                // top edge of the frame, on the thrower's follow-through, nowhere near
                // either end of the dart: fresh change OFF the board (an arm, a shadow,
                // the shoes) was in the point cloud, and the furthest-hull-point "tip"
                // followed it out of the picture. A tip is on the board by definition,
                // so the hull the tip is picked from is the on-board diff; off-board
                // clutter can no longer be a tip, and the part of a dart's silhouette
                // that projects past the rim (its flight, from a side-on camera) is
                // clipped, which biases the hull toward the end that scored.
                double fresh_share;
                if (decides_on_board)
                {
                    Mat fresh_inside;
                    bitwise_and(single_thresh, region.mask, fresh_inside);
                    const int fresh_pixels = countNonZero(fresh_inside);
                    result.camera_results[i].fresh_board_pixels = fresh_pixels;
                    fresh_share = 100.0 * (double)fresh_pixels / (double)board_pixels;
                    // The tip is searched inside the PHYSICAL board -- wider than the
                    // deciding share's scoring area, see Region::tip_mask -- so a
                    // near-edge dart keeps its shaft and the thrower stays outside.
                    Mat tip_image;
                    bitwise_and(single_thresh, region.tip_mask, tip_image);
                    single_thresh = tip_image;
                }
                else
                {
                    fresh_share = 100.0 * (double)countNonZero(single_thresh) / (double)total_pixels;
                }

                if (fresh_share >= decide_threshold) // and something new arrived on it
                {
                    // CHECK FROM CLEAN AND OR UNKNOW STATES TOO (MAYBE NOT DEFINED YET)
                    if (previous_states[i] == DartBoardState::CLEAN)
                    {
                        candidate_state = DartBoardState::DART_1;
                    }
                    else if (previous_states[i] == DartBoardState::DART_1)
                    {
                        candidate_state = DartBoardState::DART_2;
                    }
                    else if (previous_states[i] == DartBoardState::DART_2)
                    {
                        candidate_state = DartBoardState::DART_3;
                    }
                    else if (previous_states[i] == DartBoardState::DART_3)
                    {
                        candidate_state = DartBoardState::DART_3; // Stay in DART_3
                    }

                    // #1495: the reference moves when a dart was CALLED, and a camera
                    // is not what calls one. Under the falsifier this is the pre-#1495
                    // line -- this camera's own candidate advancing, decided here,
                    // before the vote that can refuse it. Ordinarily the move is made
                    // below, from the reconciled final state, for every camera at once.
                    if (advanceResetIsPerCamera())
                    {
                        working_backgrounds[i] = averaged_frame.clone();
                    }

                    // Use smart tip detection
                    double gap_to_figure = 0;
                    auto tip_and_center = detectTipAndCenter(single_thresh, debug_mode, static_cast<int>(i), dart_tips,
                                                             &gap_to_figure, &axis_pieces[i]);

                    // #1511: the shaft axis, fitted to the SAME linked figure the tip
                    // was just picked from. Always computed -- nothing reads it to
                    // decide anything, so no published byte moves -- and gated unless
                    // the falsification pin turns the gates off.
                    {
                        shaft_axis::AxisParams axis_params;
                        axis_params.gated = !axisGateIsOff();
                        shaft_axis::AxisObservation observed = shaft_axis::observeShaftAxis(
                            shaft_axis::pixelsOfPieces(axis_pieces[i], single_thresh.size()), axis_params);
                        observed.camera = (int)i;
                        observed.windowOrdinal = window_serial;
                        observed.windowOpenedCycle = window_opened_at;
                        observed.windowClosedCycle = cycle_ordinal;
                        result.camera_results[i].axis = std::move(observed);
                    }
                    Point2f tip_pos = tip_and_center.first;
                    Point2f center_pos = tip_and_center.second;

                    // If tip position is valid, update the camera result
                    if (norm(tip_pos) > 0)
                    {
                        // #1535: a "new" tip that is a pixel this camera already
                        // reported for an earlier dart of this visit, with the fresh
                        // figure elsewhere, is a re-report and not a second witness --
                        // this camera abstains from scoring the new dart, honestly.
                        // The vote is untouched: with one false witness silent, two
                        // "agreeing" strings cannot form (rig-20260918 visit 4's
                        // S20@0.9 -- the census on isAReReportOfAnEarlierTip).
                        double nearest_reported = -1;
                        if (i < reported_tips.size())
                        {
                            for (const Point2f &earlier : reported_tips[i])
                            {
                                const double d = norm(tip_pos - earlier);
                                if (nearest_reported < 0 || d < nearest_reported)
                                {
                                    nearest_reported = d;
                                }
                            }
                        }
                        if (!tipIdentityIsOff() && i < reported_tips.size() &&
                            isAReReportOfAnEarlierTip(tip_pos, gap_to_figure, reported_tips[i]))
                        {
                            log_info("TIP IDENTITY: camera " + to_string(i + 1) + " found its \"new\" tip at (" +
                                     to_string((int)tip_pos.x) + "," + to_string((int)tip_pos.y) + "), " +
                                     to_string(nearest_reported >= 0 ? (long)(nearest_reported + 0.5) : -1L) +
                                     " px from a tip it already reported for an earlier dart of this visit, "
                                     "while the fresh change's nearest point is " + to_string((long)(gap_to_figure + 0.5)) +
                                     " px away -- a re-report of an earlier dart, not a second witness, so "
                                     "this camera abstains from scoring this dart (#1535)");
                        }
                        else
                        {
                            result.camera_results[i].tip_position = tip_pos;
                            result.camera_results[i].center_position = tip_and_center.second;
                            result.camera_results[i].tip_found = true;
                        }
                    }
                }
                else
                {
                    // #1354: occupied, and nothing new on it -- the darts already
                    // called are still there and no dart arrived. The board stays what
                    // it was; before this branch existed, this window voted an
                    // advance on the history alone.
                    candidate_state = previous_states[i];
                    char share[64];
                    snprintf(share, sizeof(share), "%.3f%% against the %.3f%% threshold",
                             fresh_share, decide_threshold);
                    result.camera_results[i].axis.refusal =
                        std::string("no fresh figure: this camera's fresh change is ") + share +
                        ", so nothing new arrived to fit";
                }
            }
            else // Threshold for no dart
            {
                if (previous_states[i] == DartBoardState::DART_1 ||
                    previous_states[i] == DartBoardState::DART_2 ||
                    previous_states[i] == DartBoardState::DART_3 ||
                    previous_states[i] == DartBoardState::CLEAN)
                {
                    candidate_state = DartBoardState::CLEAN;
                    result.camera_results[i].axis.refusal =
                        "clean: this camera read the board under its CLEAN ceiling, so there is "
                        "no arriving dart to fit";

                    // #1349: the "safety reset" that lived here wiped working_backgrounds
                    // [0], [1] AND [2] -- every camera, by hard-coded index, from ONE
                    // camera's CLEAN candidate, before the vote had even been counted. A
                    // single camera flickering CLEAN mid-round -- which #1345's log shows
                    // on every throw -- cost the OTHER cameras the reference that isolates
                    // the newest dart, so their next tip was found on the union of every
                    // dart on the board. The reset a clean board really needs is made
                    // after the vote, from the reconciled final state, where the decision
                    // that can make it lives.

                    // clone the diff to working diff
                    // we know the latest is good!
                    single_thresh = thresh.clone();

                    if (debug_mode)
                    {
                        // no tip found or none tip at all just write a black image to overwrite the tip_detection_cam_
                        Mat black_image = Mat::zeros(single_thresh.size(), CV_8UC1);
                        imwrite("debug_frames/dart_processing/tip_detection_cam_" + to_string(i) + ".jpg", black_image);
                    }
                }
            }

            if (debug_mode)
            {
                imwrite("debug_frames/dart_processing/diff_thresh_cam_" + to_string(i) + ".jpg", single_thresh);

                // Store debug frames for visualization
                dart_thresh_diffs.push_back(single_thresh);

                // debug output
                string a = getDartBoardStateName(previous_states[i]);
                string b = getDartBoardStateName(candidate_state);
                log_info("STATE GUESS: From: " + a + " -> " + b);
            }

            // store previous state so we can compare to global variable
            previous_states[i] = candidate_state;
            // add to result
            result.camera_results[i].detected_state = candidate_state;
        }

        // debug via streamers
        if (debug_mode && dart_diff_streamer)
        {
            try
            {
                dart_diff_streamer->push(debug::createCombinedFrame(dart_diffs, "dart_diffs"));
                dart_thresh_streamer->push(debug::createCombinedFrame(dart_threshs, "dart_threshs"));
                dart_thresh_diff_streamer->push(debug::createCombinedFrame(dart_thresh_diffs, "dart_thresh_diffs"));
                dart_tip_streamer->push(debug::createCombinedFrame(dart_tips, "dart_tips"));
            }
            catch (const std::exception &e)
            {
                log_error("Error pushing debug frames: " + string(e.what()));
            }
        }

        // Now we should have the results for all cameras, we need to see what global state we should be at.
        // What can happen is for example:
        // Camera 0: DART_1, Camera 1: DART_1, Camera 2: DART_2
        // OR Camera 0: DART_3, Camera 1: CLEAN, Camera 2: DART_3
        // OR Camera 0: CLEAN, Camera 1: DART_1, Camera 2: DART_2
        // OR any other combination
        // ---
        // We need to determine the best state based on majority rule
        // And also update the ones that are not in the best state to the best state
        int moves_up = 0;
        int goes_clean = 0;
        int stays_same = 0;
        // #1348: the population the two counts below are counts OF, kept as a number
        // because it is what the quorum is measured against. Every `continue` under it is
        // a camera that is not in it.
        int voters = 0;

        // Loop through all cameras once
        for (size_t i = 0; i < result.camera_results.size(); i++)
        {
            if (!result.camera_results[i].frame_available)
                continue; // #798: an abstaining camera is not a vote for anything
            if (result.camera_results[i].abstained_no_board)
                continue; // #1354: nor is one with no board to have measured against
            voters++;

            if (result.camera_results[i].detected_state == DartBoardState::CLEAN)
            {
                goes_clean++;
            }
            else if (result.camera_results[i].detected_state > best_previous_state)
            {
                moves_up++;
            }
            else
            {
                stays_same++;
            }
        }

        // #1348: the quorum against the population that was just counted, rather than the
        // absolute 2 both rules compared against. At three voters and under it IS 2, so
        // nothing either fixture measures moves; what changes is that a board with fewer
        // voters than 2 is now a sentence somebody can read instead of a rule silently
        // out of reach, and a board with more than three -- which #1355 made possible --
        // takes a majority of them rather than any two.
        DartParams voting = params;
        if (stateQuorumIsAbsolute())
        {
            voting.absolute_quorum = true;
        }
        const int quorum = stateVoteQuorum(voters, voting);

        // Pick the winner
        DartBoardState final_state;
        if (goes_clean >= quorum)
        {
            final_state = DartBoardState::CLEAN; // Rule 3: a quorum thinks CLEAN
        }
        else if (moves_up >= quorum)
        {
            final_state = static_cast<DartBoardState>(static_cast<int>(best_previous_state) + 1); // Rule 1: a quorum moves up
        }
        else
        {
            final_state = best_previous_state; // Rule 2: Stay put
        }

        // log
        string a = getDartBoardStateName(best_previous_state);
        string b = getDartBoardStateName(final_state);
        log_debug("FINAL State: From: " + a + " -> " + b);

        // Set ALL cameras to the final state.
        // #1355: bounded by the CAMERA COUNT, not by `previous_states.size()`, which was
        // three whatever the board was running -- so on a four-camera board the fourth
        // camera's state was the one thing the vote never reached.
        for (size_t i = 0; i < result.camera_results.size(); i++)
        {
            previous_states[i] = final_state;
        }

        // #1495: #1349's sentence, one branch over. The board gained a dart when the
        // VOTE says so, and from that moment every camera's reference is one dart out of
        // date -- including the cameras that did not see it. Until this block existed the
        // move was made inside the per-camera loop, for a camera whose OWN candidate
        // advanced, so a camera that missed the dart the board called carried it into the
        // next window's diff and its next tip was picked from a figure holding two darts.
        // Measured in mocks/rig-20260918: camera 1 found no tip at dart 7 and dart 8's
        // figure held both, camera 2 at dart 16 and dart 17's did, camera 3 at dart 5 and
        // dart 6's did.
        //
        // A camera that brought no frame to this window has no average to move to, and is
        // left alone: it abstained, and the frame it last saw is still the best reference
        // it has.
        if (!advanceResetIsPerCamera() && final_state > best_previous_state)
        {
            for (size_t i = 0; i < working_backgrounds.size() && i < window_frames.size(); i++)
            {
                if (!window_frames[i].empty())
                {
                    working_backgrounds[i] = window_frames[i].clone();
                }
            }
        }

        // #1535: the board gained a dart, so what each camera reported FOR IT is now on
        // the record isAReReportOfAnEarlierTip reads. Recorded from the vote's decision
        // and not inside the per-camera loop, for #1495's reason: a camera is not what
        // calls a dart, and a tip from a window the vote refused was never reported to
        // anybody. Recorded whatever the OD_TIP_IDENTITY and OD_ADVANCE_RESET pins say,
        // so a pinned run measures the rule against the same memory the tree rule holds.
        if (final_state > best_previous_state)
        {
            for (size_t i = 0; i < result.camera_results.size() && i < reported_tips.size(); i++)
            {
                if (result.camera_results[i].tip_found)
                {
                    reported_tips[i].push_back(result.camera_results[i].tip_position);
                }
            }
        }

        // #1511: the axis census and its overlay, printed exactly when the VOTE called
        // a dart -- the event the observation belongs to -- and one line per camera
        // slot, abstentions included, because a coverage/refusal census with only the
        // cameras that produced plausible lines has measured nothing (the acceptance
        // says so in as many words). Both are pins; an ordinary run prints neither.
        if ((shaftCensusOn() || !shaftProbeDir().empty()) && final_state > best_previous_state)
        {
            for (size_t i = 0; i < result.camera_results.size(); i++)
            {
                const CameraDetectionResult &r = result.camera_results[i];
                // How far the published tip sits off the fitted line, where both exist:
                // the diagnostic that ties the two observations without coupling them.
                double tip_gap = -1.0;
                if (r.axis.valid && r.tip_found)
                {
                    const double dx = r.tip_position.x - r.axis.point.x;
                    const double dy = r.tip_position.y - r.axis.point.y;
                    tip_gap = std::fabs(dx * r.axis.direction.y - dy * r.axis.direction.x);
                }
                if (shaftCensusOn())
                {
                    log_info(shaft_axis::censusLine(r.axis, tip_gap));
                }
                if (!shaftProbeDir().empty() && i < window_frames.size() && !window_frames[i].empty())
                {
                    Mat canvas;
                    cvtColor(window_frames[i], canvas, COLOR_GRAY2BGR);
                    shaft_axis::drawAxisOverlay(canvas, r.axis, i < axis_pieces.size() ? axis_pieces[i]
                                                                                       : vector<vector<Point>>());
                    if (r.tip_found)
                    {
                        circle(canvas, r.tip_position, 7, Scalar(255, 0, 0), 2);
                    }
                    imwrite(shaftProbeDir() + "/axis_w" + to_string(r.axis.windowOrdinal) +
                                "_cam" + to_string(i + 1) + ".jpg",
                            canvas);
                }
            }
        }

        // #1349: the reset the mid-loop wipe was reaching for, made from the decision
        // that can make it: the board is CLEAN when the VOTE says so, and only then is
        // every camera's working background stale. Sized by the vector rather than
        // written as [0] [1] [2], which was out of bounds the day this ran with fewer
        // than three cameras. A camera whose own candidate flickered CLEAN while the
        // board held its darts now KEEPS its working background, so its next dart is
        // still read against the board as it was at the last dart.
        if (final_state == DartBoardState::CLEAN)
        {
            for (size_t i = 0; i < working_backgrounds.size(); i++)
            {
                working_backgrounds[i] = Mat();
            }

            // #1535: the visit is over, so its reported tips are nobody's earlier dart
            // any more. Reset here, beside the working backgrounds and OUTSIDE the
            // OD_CLEAN_REFERENCE guard below, so that pin cannot change what the
            // re-report rule remembers.
            for (size_t i = 0; i < reported_tips.size(); i++)
            {
                reported_tips[i].clear();
            }

            // #1518: and the CLEAN reference adopts the scene, from the same decision.
            // A reconciled CLEAN is the one moment the board is known to hold nothing,
            // so this window's settled frames ARE what clean looks like -- including
            // whatever changed since calibration: a sticker, a moved shadow line, the
            // hole where mocks/rig-20260922's parked dart stood. On a scene that never
            // changed this adopts frames that match the calibration background and moves
            // nothing, which is why both rig censuses hold (see the pull request); on
            // one that did, it is the difference between the next takeout reconciling
            // and the board wedging at DART_3 for the evening (#1514).
            //
            // Said at INFO because a board that re-based its own idea of clean must say
            // so (ADR-0055's posture: a change of reference must not be able to happen
            // silently). A camera that brought no frame keeps its old reference -- the
            // last scene it saw is still the best reference it has (#1495's rule).
            if (!cleanReferenceIsCalibration())
            {
                string adopted;
                for (size_t i = 0; i < clean_references.size() && i < window_frames.size(); i++)
                {
                    if (!window_frames[i].empty())
                    {
                        clean_references[i] = window_frames[i].clone();
                        // The adopted scene is what zero change now looks like; a fall
                        // measured from a pre-adoption figure would read the NEXT dart
                        // as a departure.
                        if (i < previous_board_change.size())
                        {
                            previous_board_change[i] = 0;
                        }
                        adopted += (adopted.empty() ? "" : ", ") + to_string(i + 1);
                    }
                }
                if (!adopted.empty())
                {
                    log_info("CLEAN REFERENCE ADOPTED: the board reconciled CLEAN, so camera(s) " +
                             adopted + " re-based their clean reference to this window's settled "
                             "frames -- the scene as it is now, not the calibration picture, is "
                             "what CLEAN is measured against from here (#1518)");
                }
            }
        }

        // set new best previous state
        result.current_state = final_state;
        result.previous_state = best_previous_state;
        best_previous_state = final_state;

        if (windowCensus())
        {
            static int window_ordinal = 0;
            string line = "WINDOW CENSUS: #" + to_string(++window_ordinal) +
                          " opened=" + to_string(window_opened_at) + " closed=" + to_string(cycle_ordinal) + " " +
                          getDartBoardStateName(result.previous_state) + " -> " +
                          getDartBoardStateName(final_state) + " (" + to_string(moves_up) + " up, " +
                          to_string(goes_clean) + " clean" +
                          // #1348: and out of how many, against what. A census that names
                          // the counts without the population cannot be read for the
                          // failure this issue is about.
                          ", " + to_string(voters) + " voted, quorum " + to_string(quorum) + ")";
            for (size_t i = 0; i < result.camera_results.size(); i++)
            {
                const CameraDetectionResult &r = result.camera_results[i];
                line += " | cam" + to_string(i + 1) + " " + getDartBoardStateName(r.detected_state) +
                        " board=" + to_string(r.board_changed_pixels) + "/" + to_string(r.board_pixels) +
                        " fresh=" + to_string(r.fresh_board_pixels) +
                        " frame=" + to_string(r.total_changed_pixels);
                if (!r.frame_available)
                    line += " NOFRAME";
                if (r.abstained_no_board)
                    line += " NOBOARD";
            }
            log_info(line);
        }

        // #1350: a window whose vote changed nothing used to leave one empty INFO line
        // here, so tonight's failure -- one camera voting a dart and the vote refusing it
        // (#1345) -- was invisible at normal level. The refusal now accounts for itself
        // where the blank line was. A window that scored keeps the blank line instead,
        // byte for byte, because the research chain's controls were extracted from that
        // output and this issue promises not to move it.
        // #1348: `voting`, not `params` -- the sentence must name the quorum the vote just
        // used, and under the falsification switch that is the absolute count.
        const string refusal = refusedWindowAccount(result.camera_results, result.previous_state,
                                                    result.current_state, moves_up, goes_clean, voting);
        if (!refusal.empty())
        {
            log_info(refusal);
        }
        else
        {
            log_info(""); // empty line for readability
        }

        // #1358: the window that settled while this one was averaging, opened now that
        // the vote it would otherwise have raced has been reconciled and every camera's
        // working background is what the vote made it.
        if (window_pending)
        {
            window_pending = false;
            openWindow();
        }

        return result;
    }
}

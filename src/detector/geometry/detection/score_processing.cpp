#include "score_processing.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "utils/streamer.hpp"
#include "../calibration/geometry_calibration.hpp"
#include "../calibration/perspective_processing.hpp"

using namespace cv;
using namespace std;

namespace score_processing
{

    static bool initialized = false;
    static unique_ptr<streamer> point_on_screen_streamer;

    // Helper: Check if point is inside ellipse (pure math)
    bool isPointInEllipse(Point2f point, const RotatedRect &ellipse)
    {
        Point2f center = ellipse.center;
        Point2f relative = point - center;

        // Rotate point to ellipse coordinate system
        float angle_rad = -ellipse.angle * CV_PI / 180.0f;
        float cos_a = cos(angle_rad);
        float sin_a = sin(angle_rad);

        Point2f rotated(
            relative.x * cos_a - relative.y * sin_a,
            relative.x * sin_a + relative.y * cos_a);

        // Ellipse equation: (x/a)² + (y/b)² <= 1
        float a = ellipse.size.width / 2.0f;
        float b = ellipse.size.height / 2.0f;

        return (rotated.x * rotated.x) / (a * a) + (rotated.y * rotated.y) / (b * b) <= 1.0f;
    }

    // #1186: distance along a ray from `origin` in direction `direction` (unit) to the
    // boundary of `ellipse`, or a negative number where the ray never crosses it. The
    // helper in utils/math.hpp answers a made-up radius in that case; a ruler mark that
    // was not found has to be absent, not invented.
    static float rayDistanceToEllipse(Point2f origin, Point2f direction, const RotatedRect &ellipse)
    {
        float a = ellipse.size.width / 2.0f;
        float b = ellipse.size.height / 2.0f;
        if (!(a > 0.0f) || !(b > 0.0f))
            return -1.0f;
        float theta = -ellipse.angle * CV_PI / 180.0f;
        float c = cos(theta), sn = sin(theta);
        Point2f rel = origin - ellipse.center;
        float x0 = rel.x * c - rel.y * sn;
        float y0 = rel.x * sn + rel.y * c;
        float dx = direction.x * c - direction.y * sn;
        float dy = direction.x * sn + direction.y * c;
        float A = (dx * dx) / (a * a) + (dy * dy) / (b * b);
        float B = 2.0f * ((x0 * dx) / (a * a) + (y0 * dy) / (b * b));
        float C = (x0 * x0) / (a * a) + (y0 * y0) / (b * b) - 1.0f;
        float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f || !(A > 0.0f))
            return -1.0f;
        float t = (-B + sqrt(disc)) / (2.0f * A); // the crossing ahead of an origin inside the ellipse
        return t > 0.0f ? t : -1.0f;
    }

    // #1186: the radial ruler. The six ring ellipses cross the ray from the bull through
    // the tip at known distances, and the board's own radii for those rings are known in
    // millimetres, so the tip's pixel distance is read off between the two marks it lies
    // between. Returns the radius normalised to the outer edge of the double ring, or a
    // negative number where the ruler has no outer mark. Membership in a ring by the
    // ellipse test above and position on this ruler agree by construction: a point on the
    // ray is inside a convex ellipse that contains the bull exactly when it is short of
    // the crossing.
    static float boardRadius(Point2f pixel, const DartboardCalibration &calib)
    {
        Point2f center = Point2f(calib.bullCenter);
        Point2f rel = pixel - center;
        float d = sqrt(rel.x * rel.x + rel.y * rel.y);
        if (d <= 0.0f)
            return 0.0f;
        Point2f u = rel * (1.0f / d);
        const perspective_processing::DartboardSpec spec;
        struct Mark
        {
            const RotatedRect *ellipse;
            float mm;
        };
        const Mark marks[6] = {
            {&calib.ellipses.innerBullEllipse, spec.bullRadius},
            {&calib.ellipses.outerBullEllipse, spec.bull25Radius},
            {&calib.ellipses.innerTripleEllipse, spec.innerTripleRadius},
            {&calib.ellipses.outerTripleEllipse, spec.outerTripleRadius},
            {&calib.ellipses.innerDoubleEllipse, spec.innerDoubleRadius},
            {&calib.ellipses.outerDoubleEllipse, spec.outerDoubleRadius},
        };
        // Usable marks, in increasing pixel distance; a mark that was not crossed or that
        // sits inside the previous one is dropped rather than bent into place.
        vector<pair<float, float>> ruler; // (pixels, mm)
        ruler.push_back({0.0f, 0.0f});
        for (const Mark &m : marks)
        {
            float t = rayDistanceToEllipse(center, u, *m.ellipse);
            if (t > ruler.back().first)
                ruler.push_back({t, m.mm});
        }
        if (ruler.back().second != spec.outerDoubleRadius)
            return -1.0f; // no outer mark, no ruler
        size_t k = 1;
        while (k + 1 < ruler.size() && d > ruler[k].first)
            k++;
        float p0 = ruler[k - 1].first, p1 = ruler[k].first;
        float m0 = ruler[k - 1].second, m1 = ruler[k].second;
        float mm = m0 + (d - p0) * (m1 - m0) / (p1 - p0); // extrapolates past the last mark
        return mm / spec.outerDoubleRadius;
    }

    // #1186: the angular ruler. Upstream's wedge loop, kept as it was - the first wire
    // pair whose span contains the tip's image angle, scanning from `start` - and made to
    // say where between the two wires the tip is. Returns the slot (0..19 from `start`)
    // or -1, and writes the fraction across the wedge from its first wire.
    static int findWedgeSlot(Point2f pixel, const DartboardCalibration &calib, int start, float &fraction)
    {
        Point2f center = Point2f(calib.bullCenter);
        Point2f direction = pixel - center;
        float point_angle = atan2(direction.y, direction.x);
        if (point_angle < 0)
            point_angle += 2 * CV_PI; // Normalize to 0-2π

        log_debug("SCORE: Point angle = " + log_string(point_angle * 180.0f / CV_PI) + " degrees");

        const int wires = (int)calib.wires.wireEndpoints.size();
        for (int i = 0; i < 20; i++)
        {
            int wire1_index = (start + i) % wires;
            int wire2_index = (start + i + 1) % wires;

            Point2f wire1 = calib.wires.wireEndpoints[wire1_index];
            Point2f wire2 = calib.wires.wireEndpoints[wire2_index];

            // Calculate angles for both wire boundaries
            Point2f dir1 = wire1 - center;
            Point2f dir2 = wire2 - center;
            float angle1 = atan2(dir1.y, dir1.x);
            float angle2 = atan2(dir2.y, dir2.x);

            if (angle1 < 0)
                angle1 += 2 * CV_PI;
            if (angle2 < 0)
                angle2 += 2 * CV_PI;

            // Ensure angle1 < angle2 (handle wraparound)
            if (angle2 < angle1)
                angle2 += 2 * CV_PI;

            // Check if point angle is between the two wire angles
            float test_angle = point_angle;
            if (test_angle < angle1)
                test_angle += 2 * CV_PI;

            if (test_angle >= angle1 && test_angle <= angle2)
            {
                float span = angle2 - angle1;
                fraction = span > 0.0f ? (test_angle - angle1) / span : 0.0f;
                log_debug("SCORE: Found wedge slot " + log_string(i) +
                          " (angle1=" + log_string(angle1 * 180.0f / CV_PI) +
                          ", angle2=" + log_string(angle2 * 180.0f / CV_PI) +
                          ", fraction=" + log_string(fraction) + ")");
                return i;
            }
        }
        return -1;
    }

    // Standard dartboard sequence starting from 20, clockwise
    static const vector<int> dartboard_numbers = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

    // #1451: `scorePoint`'s guard, with one copy of it. See the header for why a camera
    // can reach this function seeing the board and still be refused by it, and for why
    // the refusal was invisible. NOT a behaviour change: this is the expression that was
    // written inline below, character for character.
    bool canScoreAPoint(const DartboardCalibration &calib)
    {
        return calib.ellipses.hasValidDoubles && calib.wires.wholeRing();
    }

    int camerasThatCanScoreAPoint(const vector<DartboardCalibration> &calibrations)
    {
        int scorable = 0;
        for (const DartboardCalibration &calibration : calibrations)
        {
            if (canScoreAPoint(calibration))
            {
                scorable++;
            }
        }
        return scorable;
    }

    string howItScores(const DartboardCalibration &calib)
    {
        if (canScoreAPoint(calib))
        {
            return "has a fitted doubles ring and all " + to_string(wire_processing::kWiresRequired) +
                   " wire boundaries, so a dart is scored from it";
        }
        if (!calib.ellipses.hasValidDoubles)
        {
            // The same camera camera_quorum already abstains from both dart quorums for
            // (#1339, #1354), said here in the scorer's own words: no fitted ring is no
            // radial ruler, so there is no ring to put the dart in either.
            return "has no fitted doubles ring, so it has no radial ruler and no dart is "
                   "scored from it";
        }
        // A ring that is not whole. #1442's two shapes, and the count tells them apart,
        // because they send the reader to different places: short is a wire stage that
        // found too little, long is one that found too much and had the surplus dropped.
        const string count = to_string(calib.wires.wiresDetected) + " of the " +
                             to_string(wire_processing::kWiresRequired) + " wire boundaries a board has";
        if (!calib.sees_board)
        {
            return "is not looking at the dartboard, and found " + count;
        }
        // Seeing the board with a ring that is not whole is the state `calibrateSingleCamera`
        // refuses, so this camera did not calibrate on this start: it came off the cache,
        // written by a binary whose wire guard asked the other field (#1442). The remedy is
        // the cache and not the rig, and saying so is the whole point of naming it here.
        return "calibrated with " + count +
               ", which the wire guard refuses, so no dart is scored from it -- it came off "
               "the calibration cache, written by a binary that measured a whole ring "
               "differently; delete cache/ to measure this camera again";
    }

    string namingEachCamera(const vector<DartboardCalibration> &calibrations)
    {
        string out;
        for (size_t i = 0; i < calibrations.size(); i++)
        {
            out += out.empty() ? "" : "; ";
            out += "camera " + to_string(i + 1) + ": " + howItScores(calibrations[i]);
        }
        return out;
    }

    // Clean, angle-based scoring function. #1186: the same decision upstream made, stated
    // as fields; `score` is composed from them and is byte-for-byte what it was.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib)
    {
        PointScore out;

        // Validation check. #1317: the second half of this could not fire -- it asked
        // std::array<Point2f, 20>::size(), which is 20 on every calibration ever made,
        // including a blank one. It is the guard on the SCORING path, and it is the last
        // thing between a partial detection and a dart being given a number: findWedgeSlot
        // below walks `(start + i) % wires` around the ring, so with nineteen wires every
        // wedge past the gap is the wrong segment, and with none it divides by zero.
        // A camera with no frame is exactly that case -- calibrateMultipleCameras keeps a
        // blank slot for it on purpose (#1318) and scoreDarts reads calibrations[i] for
        // every camera -- so this now refuses on a real calibration in a real run.
        //
        // #1442: and the half of it that STILL could not fire is repaired the same way
        // perspective_processing's twin is. `wireEndpoints.size() < kWiresRequired` reads
        // a store bounded at kWiresRequired, so it catches a short ring and can never
        // catch a long one; a camera proposing twenty-two filled that store to twenty and
        // arrived here indistinguishable from a clean board. And it is exactly this
        // function that the difference matters in: findWedgeSlot below walks the ring by
        // ORDINAL and indexes dartboard_numbers with it, so the twenty kept out of
        // twenty-two -- the twenty smallest angles, the last two dropped, a double-width
        // gap left behind -- give every wedge past that gap a different number, reported
        // with the confidence of a whole ring. That is the same fault the sentence above
        // describes for nineteen wires, reached from the other side.
        //
        // #1451: the same expression, with one copy of it, so that the startup census can
        // ask THIS question rather than a weaker one that happened to agree. It asked
        // `sees_board && hasValidDoubles` and never the ring, so a camera refused here was
        // counted a full voter at start and then abstained from every dart in silence --
        // this refusal is `log_debug`, below the default level.
        if (!canScoreAPoint(calib))
        {
            log_debug("SCORE: Invalid calibration data: camera " + log_string(calib.camera_index + 1) +
                      " has " + log_string(calib.wires.wiresDetected) + " wire boundaries where scoring needs the " +
                      log_string(wire_processing::kWiresRequired) + " a board has" +
                      string(calib.ellipses.hasValidDoubles ? "" : ", and no fitted doubles ring"));
            return out;
        }

        // 1. RING DETECTION - Check from inside out
        bool in_inner_bull = isPointInEllipse(pixel, calib.ellipses.innerBullEllipse);
        bool in_outer_bull = !in_inner_bull && isPointInEllipse(pixel, calib.ellipses.outerBullEllipse);
        bool on_bull = in_inner_bull || in_outer_bull;

        string ring_prefix;
        if (in_inner_bull)
        {
            log_debug("SCORE: Point in INNER BULL");
            out.score = "BULL";
            out.ring = "bull";
        }
        else if (in_outer_bull)
        {
            log_debug("SCORE: Point in OUTER BULL");
            out.score = "OUTER";
            out.ring = "outer";
        }
        else
        {
            bool in_inner_triple = isPointInEllipse(pixel, calib.ellipses.innerTripleEllipse);
            bool in_outer_triple = isPointInEllipse(pixel, calib.ellipses.outerTripleEllipse);
            bool in_inner_double = isPointInEllipse(pixel, calib.ellipses.innerDoubleEllipse);
            bool in_outer_double = isPointInEllipse(pixel, calib.ellipses.outerDoubleEllipse);

            log_debug("SCORE: Ellipse tests - Inner_T:" + log_string(in_inner_triple) +
                      " Outer_T:" + log_string(in_outer_triple) +
                      " Inner_D:" + log_string(in_inner_double) +
                      " Outer_D:" + log_string(in_outer_double));

            // Determine ring type with clear logic
            if (in_outer_double && !in_inner_double)
            {
                ring_prefix = "D"; // In the double ring (narrow band)
                out.ring = "double";
                log_debug("SCORE: Ring type = DOUBLE");
            }
            else if (in_outer_triple && !in_inner_triple)
            {
                ring_prefix = "T"; // In the triple ring (narrow band)
                out.ring = "triple";
                log_debug("SCORE: Ring type = TRIPLE");
            }
            else if (in_outer_double)
            {
                ring_prefix = "S"; // Anywhere else inside the dartboard
                out.ring = "single";
                log_debug("SCORE: Ring type = SINGLE");
            }
            else
            {
                log_debug("SCORE: Point outside dartboard");
                return out; // MISS, and nothing on the board to place
            }
        }

        // The radial ruler answers for every ring, the bull included.
        float radius = boardRadius(pixel, calib);
        if (radius >= 0.0f)
        {
            out.board.has_radius = true;
            out.board.radius = radius;
        }

        // 2. WEDGE DETECTION using angles
        //
        // #1346 made this gate a DECISION where it read as an accident, and #1363 gave
        // the decision its own word: `anchored` is true for the star camera's
        // MEASUREMENT and for an operator-CONFIGURED anchor (OD_CAMERA_WEDGES, the
        // fixed-rig statement a Blade 6 over a black surround needs, because its clip
        // finder sees one clip where the heuristic demands four). It stays false for
        // the TOP/BOTTOM clip-wire guesses: #797 measured them agreeing with the star
        // camera on four wedges of seven, one wedge loose either way, and measured that
        // widening the voter set makes the published score WORSE while disagreements
        // fall back to the lowest index. Until that vote question is settled, an
        // unanchored camera's wedge is an assertion the vote keeps aside (chooseScore),
        // never a second voter.
        // #1449: the same expression this line always was, with one copy of it. The
        // startup census asks `wedgeCanBeRead` too, so a camera reported readable at
        // start is by construction a camera read here -- the two cannot drift, which is
        // the defect #1449 was filed about one field earlier. NOT a behaviour change:
        // `wedgeCanBeRead` is `anchored && wedge20WireIndex >= 0` and nothing else.
        out.wedge_measured = orientation_processing::wedgeCanBeRead(calib.orientation);
        if (!out.wedge_measured && !on_bull)
        {
            log_debug("SCORE: No orientation data, defaulting to 20");
        }

        // On a bull there is no wedge to decide, but the angle is still known where the
        // orientation is; where it is not, nothing implies one, and the angle stays absent.
        if (on_bull && !out.wedge_measured)
        {
            return out;
        }

        float fraction = 0.0f;
        int start = out.wedge_measured ? calib.orientation.wedge20WireIndex : 0;
        int slot = findWedgeSlot(pixel, calib, start, fraction);
        if (slot < 0)
        {
            log_debug("SCORE: No wedge found - this shouldn't happen");
            if (on_bull)
            {
                return out;
            }
            if (!out.wedge_measured)
            {
                // Upstream asserted the 20 without looking at a wire; so does this, and
                // with no wedge to place the tip in there is no angle to state.
                out.wedge_asserted = true; // #1346: marked at the site of the assertion
                out.segment = 20;
                out.score = ring_prefix + "20";
                return out;
            }
            out.score = "MISS";
            out.ring.clear();
            out.board = BoardPosition();
            return out;
        }

        // With no orientation the wedge the tip is in is asserted to be the 20 - slot 0 of
        // the sequence - and the fraction says where across that wedge the tip is.
        // #1346: a bull cannot reach this as an assertion -- on_bull without a measured
        // wedge returned above -- so the flag marks exactly the asserted 20s.
        out.wedge_asserted = !out.wedge_measured;
        int sequence_slot = out.wedge_measured ? slot : 0;
        float angle = 18.0f * sequence_slot - 9.0f + 18.0f * fraction;
        while (angle < 0.0f)
            angle += 360.0f;
        while (angle >= 360.0f)
            angle -= 360.0f;
        out.board.has_angle = true;
        out.board.angle = angle;

        if (!on_bull)
        {
            int number = dartboard_numbers[sequence_slot];
            log_debug("SCORE: Found wedge " + log_string(number));
            out.segment = number;
            out.score = ring_prefix + to_string(number);
        }
        return out;
    }

    ScoreResult processScore(const vector<Mat> &background_frames, const dart_processing::DartStateResult &dart_result, const vector<DartboardCalibration> &calibrations, bool debug_mode)
    {

        if (!initialized)
        {
            log_debug("SCORE: Initializing score processing");
#ifdef DEBUG_VIA_VIDEO_INPUT
            // #812: this listener is an unauthenticated MJPEG view of the board on
            // 0.0.0.0:8088. It used to be opened here on the first scored cycle of
            // every run — behind no define and no debug test — while the only push
            // to it is already behind debug_mode. It is now behind both, which is
            // the same gate the raw camera feed on 8081 has always had, so a
            // release build (make build, OD_DEFS empty) opens neither.
            if (debug_mode)
            {
                point_on_screen_streamer = make_unique<streamer>(8088, 1);
            }
#endif
            initialized = true;
        }

        ScoreResult result;

        if (dart_result.previous_state == dart_result.current_state)
        {
            // No state change, return invalid result
            result.valid = false;
            return result;
        }

        // State changed - now process the scoring
        switch (dart_result.current_state)
        {
        case dart_processing::DartBoardState::CLEAN:
            result.score = "END";
            result.confidence = 1.0f;
            result.camera_index = -1;
            result.valid = true;
            break;

        case dart_processing::DartBoardState::DART_1:
        case dart_processing::DartBoardState::DART_2:
        case dart_processing::DartBoardState::DART_3:
            // Collect scores from all cameras with detected tips
            vector<Mat> points_on_screen;                                       // For debug images
            vector<PointScore> point_scores(dart_result.camera_results.size()); // #1186: each camera's decision, as fields
            vector<bool> may_vote(dart_result.camera_results.size(), false);    // #1346: a tip and not a MISS, as it always was

            for (size_t i = 0; i < dart_result.camera_results.size(); i++)
            {
                // #798: no frame from this camera in the window, so calibrations[i] has
                // nothing to score. It abstains rather than contributing a MISS.
                if (!dart_result.camera_results[i].frame_available)
                {
                    log_warning("Camera " + to_string(i) + " score: ABSTAIN (no frame captured)");
                    if (debug_mode)
                        points_on_screen.push_back(Mat());
                    continue;
                }

                // #1318: and a camera that was refused at calibration abstains for the
                // life of the run. Its slot is still here -- so that every other camera
                // keeps its own calibration -- but there is no dartboard in its picture
                // to score a tip against, and a MISS from it is not an observation.
                if (i >= calibrations.size() || !calibrations[i].sees_board)
                {
                    log_warning("Camera " + to_string(i) + " score: ABSTAIN (this camera is not looking at the dartboard)");
                    if (debug_mode)
                        points_on_screen.push_back(Mat());
                    continue;
                }

                log_debug("-------");
                PointScore point = scorePoint(dart_result.camera_results[i].tip_position, calibrations[i]);
                string score_test = point.score;
                point_scores[i] = point;
                log_debug("-------");

                // print image
                if (debug_mode)
                {
                    log_warning("Camera " + to_string(i) + " score: " + score_test);

                    // just draw the point on the screen
                    Mat some_mat = background_frames[i].clone();
                    circle(some_mat, dart_result.camera_results[i].tip_position, 5, Scalar(0, 255, 0), -1);
                    putText(some_mat, score_test, dart_result.camera_results[i].tip_position + Point2f(10, 10),
                            FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 0, 0), 1);
                    odfs::ensureDirectory("debug_frames/score_processing");
                    imwrite("debug_frames/score_processing/point_on_screen" + to_string(i) + ".jpg", some_mat);

                    points_on_screen.push_back(some_mat);
                }

                // #1346: whether this camera may vote is unchanged -- a found tip and
                // not a MISS. How its vote COUNTS is chooseScore's decision now.
                may_vote[i] = dart_result.camera_results[i].tip_found && score_test != "MISS";
            }

            if (debug_mode && point_on_screen_streamer)
            {
                try
                {
                    point_on_screen_streamer->push(debug::createCombinedFrame(points_on_screen, "points_on_screen"));
                }
                catch (const std::exception &e)
                {
                    log_error("Failed to push points_on_screen frame: " + string(e.what()));
                }
            }

            // #1346: the vote itself is chooseScore, pure and in the header, where a
            // tester can hold it. A camera whose wedge was asserted -- the default-to-20
            // -- no longer counts toward a consensus and no longer earns the 0.9 by
            // agreeing with another assertion; it is published only when nothing
            // measured, at 0.5. #796 measured the failure this removes: S20 S20 S20
            // published over a hand-verified 36, two constants outvoting the camera
            // that measured.
            const ScoreChoice choice = chooseScore(point_scores, may_vote);
            if (choice.camera >= 0)
            {
                const int best_camera = choice.camera;
                const string final_score = point_scores[best_camera].score;

                if (choice.agreeing >= 2)
                {
                    log_info("Consensus score: " + final_score + " from " + to_string(choice.agreeing) + " cameras");
                }
                else if (!choice.by_default)
                {
                    log_info("No consensus, using single camera score: " + final_score + " from camera " + to_string(best_camera));
                }
                else
                {
                    log_info("No measured wedge: publishing camera " + to_string(best_camera) +
                             "'s wedge-by-default " + final_score + " at low confidence");
                }

                result.score = final_score;
                result.pixel_position = dart_result.camera_results[best_camera].tip_position;
                result.center_position = dart_result.camera_results[best_camera].center_position;
                result.dartboard_position = dart_result.camera_results[best_camera].tip_position; // TODO: Convert to dartboard coordinates
                result.confidence = choice.confidence;
                result.camera_index = best_camera;
                result.valid = true;
                // #1186: the board-frame fields come from the same camera and the same
                // decision the score string came from, never from the string.
                result.ring = point_scores[best_camera].ring;
                result.segment = point_scores[best_camera].segment;
                result.board = point_scores[best_camera].board;
                log_info(string("BOARD: ") + (point_scores[best_camera].wedge_measured ? "wedge measured" : "wedge by default") +
                         " | ring=" + result.ring +
                         " | segment=" + to_string(result.segment) +
                         " | radius=" + (result.board.has_radius ? to_string(result.board.radius) : string("none")) +
                         " | angle=" + (result.board.has_angle ? to_string(result.board.angle) : string("none")));
            }
            else
            {
                // State changed but no valid scores found
                result.score = "MISS";
                result.confidence = 0.5f;
                result.camera_index = -1;
                result.valid = true;

                if (debug_mode)
                {
                    log_warning("State changed but no valid scores found!");
                }
            }
            break;
        }

        return result;
    }

} // namespace score_processing

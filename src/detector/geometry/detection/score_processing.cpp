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

    // Clean, angle-based scoring function. #1186: the same decision upstream made, stated
    // as fields; `score` is composed from them and is byte-for-byte what it was.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib)
    {
        PointScore out;

        // Validation check
        if (!calib.ellipses.hasValidDoubles || calib.wires.wireEndpoints.size() < 20)
        {
            log_debug("SCORE: Invalid calibration data");
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
        out.wedge_measured = calib.orientation.isStarCamera && calib.orientation.wedge20WireIndex >= 0;
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
            vector<pair<string, int>> camera_scores; // (score, camera_index)
            vector<Mat> points_on_screen;            // For debug images
            vector<PointScore> point_scores(dart_result.camera_results.size()); // #1186: each camera's decision, as fields

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

                if (dart_result.camera_results[i].tip_found && score_test != "MISS")
                {
                    camera_scores.push_back({score_test, static_cast<int>(i)});
                }
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

            if (!camera_scores.empty())
            {
                // Consensus scoring logic
                string final_score;
                int best_camera = -1;

                // Count occurrences of each score
                map<string, vector<int>> score_cameras;
                for (const auto &[score, camera_idx] : camera_scores)
                {
                    score_cameras[score].push_back(camera_idx);
                }

                // Look for consensus (2+ cameras agreeing)
                string consensus_score;
                int max_consensus = 0;
                for (const auto &[score, cameras] : score_cameras)
                {
                    if (cameras.size() >= 2 && cameras.size() > max_consensus)
                    {
                        consensus_score = score;
                        max_consensus = cameras.size();
                    }
                }

                if (!consensus_score.empty())
                {
                    // Use consensus score, pick first camera from the group
                    final_score = consensus_score;
                    best_camera = score_cameras[consensus_score][0];
                    log_info("Consensus score: " + final_score + " from " + to_string(max_consensus) + " cameras");
                }
                else
                {
                    // No consensus, use first available score
                    final_score = camera_scores[0].first;
                    best_camera = camera_scores[0].second;
                    log_info("No consensus, using single camera score: " + final_score + " from camera " + to_string(best_camera));
                }

                result.score = final_score;
                result.pixel_position = dart_result.camera_results[best_camera].tip_position;
                result.center_position = dart_result.camera_results[best_camera].center_position;
                result.dartboard_position = dart_result.camera_results[best_camera].tip_position; // TODO: Convert to dartboard coordinates
                result.confidence = consensus_score.empty() ? 0.7f : 0.9f;
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

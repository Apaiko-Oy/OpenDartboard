#include "dart_processing.hpp"
#include <cstdlib>
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

    // Streamers for debugging
    static unique_ptr<streamer> dart_diff_streamer;
    static unique_ptr<streamer> dart_thresh_streamer;
    static unique_ptr<streamer> dart_thresh_diff_streamer;
    static unique_ptr<streamer> dart_tip_streamer;

    // getDartBoardStateName lives inline in the header since #1350.

    pair<Point2f, Point2f> detectTipAndCenter(const Mat &binary_thresh, bool debug_mode, int camera_id, vector<Mat> &dart_tips)
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

        vector<vector<Point>> dart_pieces;
        for (const auto &contour : contours)
        {
            double area = contourArea(contour);
            if (area > 400 && area < 20000) // More inclusive range for dart pieces
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
            working_backgrounds.size() != current_frames.size())
        {
            previous_states.resize(current_frames.size(), DartBoardState::CLEAN);
            accumulated_frames.resize(current_frames.size());
            frames_accumulated.resize(current_frames.size(), 0);
            working_backgrounds.resize(current_frames.size());
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

        // Process all cameras - use pre-computed averages
        vector<DartBoardState> camera_states;

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
                log_warning("DART: camera " + to_string(i) + " contributed no frames to this window - abstaining");
                if (debug_mode)
                {
                    dart_diffs.push_back(Mat());
                    dart_threshs.push_back(Mat());
                    dart_thresh_diffs.push_back(Mat());
                }
                continue;
            }

            Mat background_gray;
            cvtColor(background_frames[i], background_gray, COLOR_BGR2GRAY);

            Mat averaged_frame;
            accumulated_frames[i].convertTo(averaged_frame, CV_8U, 1.0 / frames_accumulated[i]);

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

            if (any_board_fitted && !decides_on_board)
            {
                // #1354: another camera brought a board and this one did not, so it has
                // no denominator to decide with. It abstains -- the frame figure it
                // would otherwise answer with is how the rig's camera 1 voted DART_1 on
                // the thrower's shoes, six windows of six (#1345).
                result.camera_results[i].abstained_no_board = true;
                candidate_state = previous_states[i];
                single_thresh = thresh.clone();
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

                    // The reference moves to this dart exactly when a dart was called;
                    // an occupied-but-still window keeps the reference where it was.
                    working_backgrounds[i] = averaged_frame.clone();

                    // Use smart tip detection
                    auto tip_and_center = detectTipAndCenter(single_thresh, debug_mode, static_cast<int>(i), dart_tips);
                    Point2f tip_pos = tip_and_center.first;
                    Point2f center_pos = tip_and_center.second;

                    // If tip position is valid, update the camera result
                    if (norm(tip_pos) > 0)
                    {
                        result.camera_results[i].tip_position = tip_pos;
                        result.camera_results[i].center_position = tip_and_center.second;
                        result.camera_results[i].tip_found = true;
                    }
                }
                else
                {
                    // #1354: occupied, and nothing new on it -- the darts already
                    // called are still there and no dart arrived. The board stays what
                    // it was; before this branch existed, this window voted an
                    // advance on the history alone.
                    candidate_state = previous_states[i];
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

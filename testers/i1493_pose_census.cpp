// #1493: compose the three board planes into camera poses, and report the
// ray-intersection residual.
//
//   i1493_pose_census <clip1> <clip2> <clip3>
//
// A MEASUREMENT AND NOT A CHANGE. Nothing under src/ moves for this file, and it asserts
// no threshold and concludes no architecture (#1488 is the decision and it is the
// maintainer's).
//
// THIS IS #1490'S HARNESS WITH ONE EXTRA COLUMN. The replay is i1490_spread_census.cpp's,
// line for line -- calibrate all three cameras on capture::readAveraged(30) at the frames
// DEBUG_SEEK_VIDEO seeks to, then motion_processing::processMotion and
// dart_processing::processDartState once per cycle, no cycle cap, stopping wherever the
// board's state ADVANCED because that is where score_processing::processScore scores a
// dart. What is added is that each camera's board plane is printed AS A MATRIX rather than
// only as its two scalars, because a ray needs the whole of it. All the pose arithmetic is
// in i1493_poses.py, where it can be re-read and re-run without a rebuild.
//
// WHAT A PLANE IS AND IS NOT, said here because the issue's premise turns on it.
// wire_model::planeOf() returns a HOMOGRAPHY, board plane -> image. Under a pinhole camera
// H is proportional to K [r1 r2 t]: the pose is in there, but it is multiplied by the
// intrinsics, and #1467 built the plane precisely so that no intrinsics would be needed --
// "no solvePnP here, no intrinsics, no focal length". Unprojecting a pixel onto the board
// (which is what #1490 measured) needs only H^-1 and is therefore genuinely free. A RAY
// needs a camera CENTRE, and a camera centre is not in H unless K is supplied or assumed.
// So the poses this census feeds are recovered under an assumed pinhole model, and the
// python states every assumption it makes and reports how well each one held.
#include <opencv2/opencv.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "wire_model.hpp"
#include "wire_processing.hpp"
#include "detector/geometry/detection/dart_processing.hpp"
#include "detector/geometry/detection/motion_processing.hpp"
#include "detector/geometry/detection/score_processing.hpp"

namespace
{
    /** DEBUG_SEEK_VIDEO's own arithmetic (utils/capture_opencv.hpp). */
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0) return 0;
        return (int)(fps * seconds);
    }

    /** capture::readAveraged(30), which is what calibration is handed at start-up. */
    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out)
    {
        cv::Mat sum;
        int consumed = 0;
        for (int i = 0; i < numFrames; i++)
        {
            cv::Mat f;
            if (!cap.read(f) || f.empty()) break;
            cv::Mat ff;
            f.convertTo(ff, CV_32F);
            if (sum.empty()) sum = ff; else sum += ff;
            consumed++;
        }
        if (consumed == 0) return false;
        cv::Mat mean = sum / consumed;
        mean.convertTo(out, CV_8U);
        return true;
    }

    std::string fmt(double v)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%.6f", v);
        return buf;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1493_pose_census <clip> [<clip> ...]\n";
        return 2;
    }

    const int cameras = argc - 1;
    std::vector<cv::VideoCapture> caps(cameras);
    std::vector<cv::Mat> initial_frames(cameras);
    for (int i = 0; i < cameras; i++)
    {
        caps[i].open(argv[i + 1]);
        if (!caps[i].isOpened())
        {
            std::cerr << "cannot open " << argv[i + 1] << "\n";
            return 2;
        }
        const double fps = caps[i].get(cv::CAP_PROP_FPS);
        caps[i].set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(i, fps));
        if (!averageOf(caps[i], 30, initial_frames[i]))
        {
            std::cerr << "no frames from " << argv[i + 1] << "\n";
            return 2;
        }
    }

    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    std::vector<motion_processing::BoardExtent> board_extents(calibrations.size());
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        board_extents[i].known = calibrations[i].sees_board && calibrations[i].ellipses.hasValidDoubles;
        board_extents[i].edge = calibrations[i].ellipses.outerDoubleEllipse;
    }

    // The board plane per camera, recomputed exactly as wire_processing builds it. It is
    // stored nowhere -- DartboardCalibration is fwritten raw and keeps only the two
    // scalars -- so a probe recomputes it rather than reading it (#1490 found the same).
    std::vector<wire_model::Plane> planes(calibrations.size());
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const double conicOfDoubles = wire_processing::conicOfDoublesFor(calib);
        planes[i] = wire_model::planeOf(calib.ellipses.outerDoubleEllipse,
                                        cv::Point2f(calib.bullCenter), conicOfDoubles);

        // The wedge grid this camera is already scoring against: its twenty generated
        // boundaries, asked of the same fitter that placed them. #1490's census reads it
        // the same way. It is HERE because planeOf() leaves the rotation about the board's
        // axis unfitted -- see i1493_poses.py, which is where that costs something.
        double phase = 0.0;
        bool phaseKnown = false;
        if (planes[i].built)
        {
            std::vector<cv::Point2f> endpoints;
            for (size_t w = 0; w < calib.wires.wireEndpoints.size(); w++)
            {
                endpoints.push_back(calib.wires.wireEndpoints[w]);
            }
            const wire_model::Fit fit = wire_model::fitTwentyFold(planes[i], endpoints);
            if (fit.built)
            {
                phase = fit.offset * 180.0 / CV_PI;
                phaseKnown = true;
            }
        }

        const double semiMajor = 0.5 * std::max(calib.ellipses.outerDoubleEllipse.size.width,
                                                calib.ellipses.outerDoubleEllipse.size.height);
        const double semiMinor = 0.5 * std::min(calib.ellipses.outerDoubleEllipse.size.width,
                                                calib.ellipses.outerDoubleEllipse.size.height);

        std::cout << "I1493CAM cam=" << (i + 1)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " scorable=" << (score_processing::aDartIsScoredFrom(calib) ? 1 : 0)
                  << " frameW=" << initial_frames[i].cols
                  << " frameH=" << initial_frames[i].rows
                  << " doublesPx=" << fmt(semiMajor)
                  << " doublesMinorPx=" << fmt(semiMinor)
                  << " bull=" << fmt(calib.bullCenter.x) << "," << fmt(calib.bullCenter.y)
                  << " conicOfDoubles=" << fmt(conicOfDoubles)
                  << " planeBuilt=" << (planes[i].built ? 1 : 0)
                  << " tilt=" << fmt(planes[i].tilt)
                  << " tiltAngle=" << fmt(planes[i].tiltAngle)
                  << " conicRadius=" << fmt(planes[i].conicRadius)
                  << " anchored=" << (orientation_processing::wedgeCanBeRead(calib.orientation) ? 1 : 0)
                  << " phaseDeg=" << (phaseKnown ? fmt(phase) : std::string("none"))
                  << " H=";
        for (int r = 0; r < 3; r++)
        {
            for (int c = 0; c < 3; c++)
            {
                std::cout << (r || c ? "," : "") << fmt(planes[i].H(r, c));
            }
        }
        std::cout << std::endl;
    }

    // ---- the replay --------------------------------------------------------------------
    std::vector<cv::Mat> background_frames;
    for (const cv::Mat &f : initial_frames) background_frames.push_back(f.clone());

    long cycle = 0;
    int dart = 0;
    while (true)
    {
        std::vector<cv::Mat> images(cameras);
        int got = 0;
        for (int i = 0; i < cameras; i++)
        {
            if (caps[i].read(images[i]) && !images[i].empty()) got++;
            else images[i] = cv::Mat();
        }
        if (got == 0) break;
        cycle++;

        const motion_processing::MotionResult motion =
            motion_processing::processMotion(images, background_frames, board_extents, false);
        const dart_processing::DartStateResult dart_result =
            dart_processing::processDartState(images, background_frames, board_extents,
                                              motion.motion_finished, false);

        if (dart_result.previous_state == dart_result.current_state) continue;
        if (dart_result.current_state == dart_processing::DartBoardState::CLEAN) continue;

        dart++;
        for (size_t i = 0; i < dart_result.camera_results.size() && i < calibrations.size(); i++)
        {
            const dart_processing::CameraDetectionResult &cam = dart_result.camera_results[i];
            const DartboardCalibration &calib = calibrations[i];

            // processScore's own abstentions, in its own order.
            const bool abstained = !cam.frame_available || !calib.sees_board;
            const bool usable = !abstained && cam.tip_found;

            std::cout << "I1493DART dart=" << dart << " cycle=" << cycle
                      << " state=" << dart_processing::getDartBoardStateName(dart_result.current_state)
                      << " cam=" << (i + 1)
                      << " frame=" << (cam.frame_available ? 1 : 0)
                      << " tipFound=" << (cam.tip_found ? 1 : 0)
                      << " tip=" << fmt(cam.tip_position.x) << "," << fmt(cam.tip_position.y);

            if (!usable)
            {
                std::cout << " score=- onBoard=0 bpR=none" << std::endl;
                continue;
            }

            // The shipped decision, called unmodified. `has_radius` is the tip-plausibility
            // split #1490 used: it is set only where scorePoint put the tip INSIDE the outer
            // double ellipse, so a camera that found the thrower's arm or a previous dart
            // says nothing here.
            const score_processing::PointScore ps =
                score_processing::scorePoint(cam.tip_position, calib);

            std::cout << " score=" << ps.score
                      << " onBoard=" << (ps.board.has_radius ? 1 : 0)
                      << " bpR=" << (ps.board.has_radius ? fmt(ps.board.radius) : std::string("none"))
                      << std::endl;
        }
    }

    std::cout << "I1493END cycles=" << cycle << " darts=" << dart << std::endl;
    return 0;
}

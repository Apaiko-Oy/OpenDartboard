// #1490: how far apart do two cameras place the same dart, once each is mapped through
// its own board plane?
//
//   i1490_spread_census <clip1> <clip2> <clip3>
//
// A MEASUREMENT AND NOT A CHANGE. Nothing under src/ moves for this file; every number
// below comes out of the shipped stages, called in the order the detector calls them.
//
// It replays the rig footage through the detector's own pipeline -- calibrate all three
// cameras on an average of thirty frames apiece (capture::readAveraged(30) at start-up),
// then motion_processing::processMotion and dart_processing::processDartState once per
// cycle -- and stops at every window where the board's state ADVANCED, which is exactly
// where score_processing::processScore scores a dart. At that moment every camera's own
// tip pixel is in hand, and each is mapped into board coordinates TWICE:
//
//   1. THROUGH #1467's planeOf(). wire_model::planeOf(the fitted doubles ellipse, the
//      detected bull, #1423's conicOfDoubles) is the board plane for this camera -- no
//      solvePnP, no intrinsics. Hinv carries the tip into that plane, where |q| = 1 is
//      the outer edge of the double ring by construction (planeOf scales the conic by
//      conicOfDoubles), so the radius in millimetres is |q| * 170. This is the
//      RECONSTRUCTION candidate ADR-0084 left open.
//
//   2. THROUGH THE EXISTING BoardPosition. score_processing::scorePoint(tip, calib) is
//      called unmodified and its BoardPosition read: radius normalised 0..1 on the ring
//      ruler, angle in degrees. This is the RECTIFICATION that is in the tree today --
//      "a wire-to-wire fraction in image angle is a rectification, not a reconstruction."
//
// WHAT AN ANGLE CAN AND CANNOT SAY ACROSS CAMERAS, because it decides how this reads.
// Neither mapping gives a board angle in a frame two cameras share. planeOf's zero is
// wherever the fitted ellipse's own axis fell, and BoardPosition's angle is measured from
// the wedge the ORIENTATION stage anchored -- and on this rig only one camera of three can
// be read for a wedge. What both DO give is a position relative to that camera's own
// twenty-fold wedge grid, and the two grids differ by a whole number of sectors: a
// camera's wire ring is generated at wire_model::Fit::offset + k*18 degrees. So this
// census prints each tip's position WITHIN its own sector, in degrees, for both mappings.
// Two cameras reading one dart correctly agree on that number exactly; a disagreement of
// d degrees is real, and only a disagreement of a whole sector or more is invisible.
// Nothing here resolves which sector, and nothing here pretends to (#1486 is that work).
//
// The wedge phase per camera is recovered by asking wire_model::fitTwentyFold the twenty
// boundaries the calibration already holds -- the ring it generated -- rather than by
// refitting anything.
//
// One line per camera and one per (dart, camera), prefixed so they survive the pipeline's
// own logging on the same stream.
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
    // The board's own outer double radius, in millimetres. Same number
    // perspective_processing::DartboardSpec carries; stated here because every figure this
    // census prints is in it.
    constexpr double kOuterDoubleMm = 170.0;

    // DEBUG_SEEK_VIDEO's own arithmetic (utils/capture_opencv.hpp), so this census reads
    // the frames a dev-build detector run reads.
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0) return 0;
        return (int)(fps * seconds);
    }

    // capture::readAveraged(30), which is what calibration is handed at start-up.
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
        char buf[40];
        snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    }

    /** An angle in degrees folded into [0, 18): where in its own sector a point sits. */
    double intoSectorDeg(double deg)
    {
        double d = std::fmod(deg, 18.0);
        if (d < 0.0) d += 18.0;
        return d;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1490_spread_census <clip> [<clip> ...]\n";
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

    // The detector's own calibration, on the detector's own frames.
    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    // GeometryDetector::process builds these once, out of the same two questions.
    std::vector<motion_processing::BoardExtent> board_extents(calibrations.size());
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        board_extents[i].known = calibrations[i].sees_board && calibrations[i].ellipses.hasValidDoubles;
        board_extents[i].edge = calibrations[i].ellipses.outerDoubleEllipse;
    }

    // The board plane per camera, recomputed exactly as wire_processing builds it. It is
    // stored nowhere -- DartboardCalibration is fwritten raw and keeps only the two
    // scalars -- so a probe recomputes it rather than reading it.
    std::vector<wire_model::Plane> planes(calibrations.size());
    std::vector<double> wedgePhaseDeg(calibrations.size(), 0.0);
    std::vector<bool> phaseKnown(calibrations.size(), false);

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const double conicOfDoubles = wire_processing::conicOfDoublesFor(calib);
        planes[i] = wire_model::planeOf(calib.ellipses.outerDoubleEllipse,
                                        cv::Point2f(calib.bullCenter), conicOfDoubles);

        // The wedge grid this camera is already scoring against: its twenty generated
        // boundaries, asked of the same fitter that placed them.
        double phase = 0.0;
        bool known = false;
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
                known = true;
            }
        }
        wedgePhaseDeg[i] = phase;
        phaseKnown[i] = known;

        const double semiMajor = 0.5 * std::max(calib.ellipses.outerDoubleEllipse.size.width,
                                                calib.ellipses.outerDoubleEllipse.size.height);
        std::cout << "I1490CAM cam=" << (i + 1)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " scorable=" << (score_processing::aDartIsScoredFrom(calib) ? 1 : 0)
                  << " anchored=" << (orientation_processing::wedgeCanBeRead(calib.orientation) ? 1 : 0)
                  << " wires=" << calib.wires.wiresDetected
                  << " doublesPx=" << fmt(semiMajor)
                  << " bull=" << calib.bullCenter.x << "," << calib.bullCenter.y
                  << " conicOfDoubles=" << fmt(conicOfDoubles)
                  << " planeBuilt=" << (planes[i].built ? 1 : 0)
                  << " tilt=" << fmt(planes[i].tilt)
                  << " conicRadius=" << fmt(planes[i].conicRadius)
                  << " phaseDeg=" << (known ? fmt(phase) : std::string("none"))
                  << std::endl;
    }

    // ---- the replay --------------------------------------------------------------------
    // background_frames are the calibration frames, which is what GeometryDetector keeps
    // (`background_frames.push_back(frame.clone())` over initial_frames).
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

        // processScore's own condition for scoring a dart: the state changed, and it did
        // not change to CLEAN (which is a takeout, published as END).
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

            std::cout << "I1490DART dart=" << dart << " cycle=" << cycle
                      << " state=" << dart_processing::getDartBoardStateName(dart_result.current_state)
                      << " cam=" << (i + 1)
                      << " frame=" << (cam.frame_available ? 1 : 0)
                      << " tipFound=" << (cam.tip_found ? 1 : 0)
                      << " tip=" << fmt(cam.tip_position.x) << "," << fmt(cam.tip_position.y);

            if (!usable)
            {
                std::cout << " score=- bpR=none bpPhase=none plR=none plPhase=none" << std::endl;
                continue;
            }

            // 2. THE EXISTING BoardPosition -- the shipped decision, called unmodified.
            const score_processing::PointScore ps =
                score_processing::scorePoint(cam.tip_position, calib);

            // 1. THROUGH planeOf() -- the reconstruction candidate.
            std::string plR = "none", plPhase = "none", plTheta = "none";
            if (planes[i].built)
            {
                const cv::Vec3d q = planes[i].Hinv * cv::Vec3d(cam.tip_position.x, cam.tip_position.y, 1.0);
                if (q[2] != 0.0)
                {
                    const double bx = q[0] / q[2], by = q[1] / q[2];
                    plR = fmt(std::sqrt(bx * bx + by * by)); // |q| = 1 is the double ring's outer edge
                    const double theta = std::atan2(by, bx) * 180.0 / CV_PI;
                    plTheta = fmt(theta);
                    if (phaseKnown[i])
                    {
                        plPhase = fmt(intoSectorDeg(theta - wedgePhaseDeg[i]));
                    }
                }
            }

            std::cout << " score=" << ps.score
                      << " wedgeMeasured=" << (ps.wedge_measured ? 1 : 0)
                      << " bpR=" << (ps.board.has_radius ? fmt(ps.board.radius) : std::string("none"))
                      << " bpAngle=" << (ps.board.has_angle ? fmt(ps.board.angle) : std::string("none"))
                      // Where in its own wedge the rectification puts the tip: the angle is
                      // 18*slot - 9 + 18*fraction, so this is 18*fraction and nothing else.
                      << " bpPhase=" << (ps.board.has_angle ? fmt(intoSectorDeg(ps.board.angle + 9.0)) : std::string("none"))
                      << " plR=" << plR
                      << " plTheta=" << plTheta
                      << " plPhase=" << plPhase
                      << " mmPerUnit=" << fmt(kOuterDoubleMm)
                      << std::endl;
        }
    }

    std::cout << "I1490END cycles=" << cycle << " darts=" << dart << std::endl;
    return 0;
}

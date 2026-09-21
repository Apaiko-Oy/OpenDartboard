// #1492: what did each camera actually find, on the darts where they disagree most?
//
//   i1492_tip_probe <outdir> <clip1> <clip2> <clip3>
//
// A MEASUREMENT AND NOT A CHANGE of any decision. It is i1490_spread_census's replay --
// the detector's own calibration, motion and dart stages, over the whole of
// mocks/rig-20260918 with no cycle cap -- with one thing added: at every window where the
// board's state ADVANCED it writes each camera's RAW FRAME with the tip the detector chose
// marked on it, beside the board's fitted double ring, the physical rim the tip search is
// masked to, and the bull. dart_processing's own OD_TIP_CENSUS prints the figure that tip
// was picked out of; this is the picture of the same moment, in the camera's own pixels,
// which is the only thing that can say whether the point is on the dart at all.
//
// It prints I1492DART rows in i1490's shape so the two are read by the same arithmetic.
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
    constexpr double kOuterDoubleMm = 170.0;

    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0) return 0;
        return (int)(fps * seconds);
    }

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

    double intoSectorDeg(double deg)
    {
        double d = std::fmod(deg, 18.0);
        if (d < 0.0) d += 18.0;
        return d;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1492_tip_probe <outdir> <clip> [<clip> ...]\n";
        return 2;
    }
    const std::string outdir = argv[1];

    const int cameras = argc - 2;
    std::vector<cv::VideoCapture> caps(cameras);
    std::vector<cv::Mat> initial_frames(cameras);
    for (int i = 0; i < cameras; i++)
    {
        caps[i].open(argv[i + 2]);
        if (!caps[i].isOpened()) { std::cerr << "cannot open " << argv[i + 2] << "\n"; return 2; }
        const double fps = caps[i].get(cv::CAP_PROP_FPS);
        caps[i].set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(i, fps));
        if (!averageOf(caps[i], 30, initial_frames[i])) { std::cerr << "no frames\n"; return 2; }
    }

    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    std::vector<motion_processing::BoardExtent> board_extents(calibrations.size());
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        board_extents[i].known = calibrations[i].sees_board && calibrations[i].ellipses.hasValidDoubles;
        board_extents[i].edge = calibrations[i].ellipses.outerDoubleEllipse;
    }

    std::vector<wire_model::Plane> planes(calibrations.size());
    std::vector<double> wedgePhaseDeg(calibrations.size(), 0.0);
    std::vector<bool> phaseKnown(calibrations.size(), false);
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const double conicOfDoubles = wire_processing::conicOfDoublesFor(calib);
        planes[i] = wire_model::planeOf(calib.ellipses.outerDoubleEllipse,
                                        cv::Point2f(calib.bullCenter), conicOfDoubles);
        if (planes[i].built)
        {
            std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
            const wire_model::Fit fit = wire_model::fitTwentyFold(planes[i], endpoints);
            if (fit.built) { wedgePhaseDeg[i] = fit.offset * 180.0 / CV_PI; phaseKnown[i] = true; }
        }
        std::cout << "I1492CAM cam=" << (i + 1)
                  << " bull=" << calib.bullCenter.x << "," << calib.bullCenter.y
                  << " doubles=" << fmt(calib.ellipses.outerDoubleEllipse.center.x) << ","
                  << fmt(calib.ellipses.outerDoubleEllipse.center.y)
                  << " size=" << fmt(calib.ellipses.outerDoubleEllipse.size.width) << "x"
                  << fmt(calib.ellipses.outerDoubleEllipse.size.height)
                  << " angle=" << fmt(calib.ellipses.outerDoubleEllipse.angle)
                  << " planeBuilt=" << (planes[i].built ? 1 : 0)
                  << " phaseDeg=" << (phaseKnown[i] ? fmt(wedgePhaseDeg[i]) : std::string("none"))
                  << std::endl;
    }

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
            const bool usable = cam.frame_available && calib.sees_board && cam.tip_found;

            std::cout << "I1492DART dart=" << dart << " cycle=" << cycle
                      << " state=" << dart_processing::getDartBoardStateName(dart_result.current_state)
                      << " cam=" << (i + 1)
                      << " tipFound=" << (cam.tip_found ? 1 : 0)
                      << " tip=" << fmt(cam.tip_position.x) << "," << fmt(cam.tip_position.y)
                      << " centre=" << fmt(cam.center_position.x) << "," << fmt(cam.center_position.y);

            if (!usable) { std::cout << " score=- plR=none plPhase=none" << std::endl; }
            else
            {
                const score_processing::PointScore ps = score_processing::scorePoint(cam.tip_position, calib);
                std::string plR = "none", plPhase = "none";
                if (planes[i].built)
                {
                    const cv::Vec3d q = planes[i].Hinv * cv::Vec3d(cam.tip_position.x, cam.tip_position.y, 1.0);
                    if (q[2] != 0.0)
                    {
                        const double bx = q[0] / q[2], by = q[1] / q[2];
                        plR = fmt(std::sqrt(bx * bx + by * by));
                        const double theta = std::atan2(by, bx) * 180.0 / CV_PI;
                        if (phaseKnown[i]) plPhase = fmt(intoSectorDeg(theta - wedgePhaseDeg[i]));
                    }
                }
                std::cout << " score=" << ps.score << " plR=" << plR << " plPhase=" << plPhase
                          << " mmPerUnit=" << fmt(kOuterDoubleMm) << std::endl;
            }

            // the picture: this camera's own pixels at this moment, with the tip on them
            if (!images[i].empty() && !outdir.empty())
            {
                cv::Mat pic = images[i].clone();
                if (board_extents[i].known)
                {
                    cv::ellipse(pic, board_extents[i].edge, cv::Scalar(0, 255, 0), 1);
                    cv::RotatedRect physical = board_extents[i].edge;
                    physical.size.width *= 225.5f / 170.0f;
                    physical.size.height *= 225.5f / 170.0f;
                    cv::ellipse(pic, physical, cv::Scalar(0, 255, 255), 1);
                }
                cv::circle(pic, calib.bullCenter, 4, cv::Scalar(255, 0, 0), -1);
                if (cam.tip_found)
                {
                    cv::circle(pic, cam.tip_position, 10, cv::Scalar(0, 0, 255), 2);
                    cv::line(pic, cv::Point2f(cam.tip_position.x - 18, cam.tip_position.y),
                             cv::Point2f(cam.tip_position.x + 18, cam.tip_position.y), cv::Scalar(0, 0, 255), 1);
                    cv::circle(pic, cam.center_position, 5, cv::Scalar(255, 0, 255), -1);
                }
                char name[256];
                snprintf(name, sizeof(name), "%s/dart%02d_cam%zu.jpg", outdir.c_str(), dart, i + 1);
                cv::imwrite(name, pic);
            }
        }
    }

    std::cout << "I1492END cycles=" << cycle << " darts=" << dart << std::endl;
    return 0;
}

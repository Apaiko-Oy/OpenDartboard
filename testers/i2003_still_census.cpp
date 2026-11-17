// turnaus#2003: what one camera's calibration reads on one STILL picture.
//
//   i2003_still_census <picture> <camera-index> [debug|middle]
//
//     debug    the calibration's debug pictures, under debug_frames/ in the working directory
//     middle   everything but the middle of the board painted black before it is read: an
//              ellipse of 120x70 px about (698,297), camera 1's bull. What is left is the
//              bull and the beds round it, so the ring cannot be traced with the dark red
//              or without it -- a picture BOTH readings refuse, which no camera gave us.
//
// The club's board was changed from the Winmau Blade 6 to a Harrows board, and on
// 2026-11-17 at 21:09 camera 1 was refused on the averaged frame and on all 31 further
// looks. The board wrote what it was looking at: `cache/backgrounds/camera_<i>_background.jpg`
// is the mean of the thirty frames after the last look (#1605), in colour, and it is the
// only colour picture of that board there is. testers/fixtures2003/ holds the three.
//
// This program hands one of them to geometry_calibration::calibrateSingleCamera -- the
// function the detector calls on its averaged frame and on every further look -- and
// prints what came back, in one row a harness can read:
//
//   I2003 picture=<name> cam=<n> sees=<0|1> bull=(x,y) span=<px> identity=<..>
//         doubles=<0|1> conic=<px> wires=<n> whole=<0|1> asked=<0|1> trusted=<0|1>
//         R=<f> candidates=<n> planeBuilt=<0|1> geometryAccepted=<0|1>
//         worstFittedMm=<f> worstHeldOutMm=<f>
//   I2003STORY picture=<name> cam=<n> <the board fit's whole story>
//
// The pipeline's own log lines are on stdout between them, at DEBUG, because the stage
// that first goes wrong says so in its own words and nowhere else.
//
// A STILL IS NOT A LOOK, and the difference is stated rather than hidden: the picture is a
// thirty-frame mean that has been through one JPEG encode, where a look is one frame
// straight off the camera. What the still can say is what the calibration reads on the
// board's own picture of the board; it cannot say what the next frame would have read.
//
// Measurement only: the assertions are in i2003_inside.sh.

#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "geometry_calibration.hpp"
#include "board_model.hpp"
#include "ring_identity.hpp"
#include "wire_processing.hpp"
#include "utils.hpp"

namespace
{
    std::string baseNameOf(const std::string &path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    const char *identityName(int ring)
    {
        switch (static_cast<ring_identity::Ring>(ring))
        {
        case ring_identity::Ring::Doubles:
            return "doubles";
        case ring_identity::Ring::Trebles:
            return "trebles";
        default:
            return "unknown";
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i2003_still_census <picture> <camera-index> [debug|middle]" << std::endl;
        return 2;
    }
    const std::string picture = argv[1];
    const int cameraIndex = atoi(argv[2]);
    const bool debugImages = argc > 3 && std::string(argv[3]) == "debug";

    logging::setLogLevel(logging::LogLevel::DEBUG);

    cv::Mat frame = cv::imread(picture, cv::IMREAD_COLOR);
    if (frame.empty())
    {
        std::cerr << "cannot read " << picture << std::endl;
        return 2;
    }
    if (argc > 3 && std::string(argv[3]) == "middle")
    {
        cv::Mat keep = cv::Mat::zeros(frame.size(), CV_8U);
        cv::ellipse(keep, cv::Point(698, 297), cv::Size(120, 70), 0, 0, 360, cv::Scalar(255), cv::FILLED);
        cv::Mat middle = cv::Mat::zeros(frame.size(), frame.type());
        frame.copyTo(middle, keep);
        frame = middle;
    }

    const std::string tag = "picture=" + baseNameOf(picture) + " cam=" + std::to_string(cameraIndex + 1);

    DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, cameraIndex, debugImages);

    const double conicOfDoubles = calib.sees_board ? wire_processing::conicOfDoublesFor(calib) : 0.0;
    const board_model::BoardProfile profile =
        board_model::profileFromSpec(perspective_processing::DartboardSpec());
    const board_model::BoardFit fit = board_model::fitBoardToCamera(profile, calib, conicOfDoubles);

    const double conicPx = ellipse_processing::ringReach(calib.ellipses.outerDoubleEllipse);
    double worstFitted = 0.0, worstHeldOut = 0.0;
    for (int i = 0; i <= ellipse_processing::kRingCount; i++)
    {
        const board_model::RingResidual &r = fit.rings[i];
        if (!r.observed)
        {
            continue;
        }
        double &worst = r.heldOut ? worstHeldOut : worstFitted;
        worst = std::max(worst, std::fabs(r.signedMedianMm));
    }

    printf("I2003 %s sees=%d bull=(%d,%d) span=%.1f identity=%s doubles=%d conic=%.1f wires=%d whole=%d "
           "asked=%d trusted=%d R=%.6f candidates=%d planeBuilt=%d geometryAccepted=%d "
           "worstFittedMm=%.1f worstHeldOutMm=%.1f\n",
           tag.c_str(), calib.sees_board ? 1 : 0, calib.bullCenter.x, calib.bullCenter.y,
           calib.look.board_span_px, identityName(calib.look.ring_measured),
           calib.ellipses.hasValidDoubles ? 1 : 0, conicPx, calib.wires.wiresDetected,
           calib.wires.wholeRing() ? 1 : 0, calib.wires.fit_asked ? 1 : 0, calib.wires.fit_trusted ? 1 : 0,
           calib.wires.fit_coherence, calib.wires.fit_candidates, fit.planeBuilt ? 1 : 0,
           fit.geometryAccepted ? 1 : 0, worstFitted, worstHeldOut);
    std::cout << "I2003STORY " << tag << " " << fit.story << std::endl;
    return 0;
}

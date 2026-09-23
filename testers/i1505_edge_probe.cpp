// #1505: what each camera actually found for the two OFF-BOARD throws, shown not
// inferred.
//
//   i1505_edge_probe <outdir> <clip1> <clip2> <clip3>
//
// A MEASUREMENT AND NOT A CHANGE of any decision. It is i1492_tip_probe's replay -- the
// detector's own calibration, motion and dart stages over the whole of
// mocks/rig-20260918 with no cycle cap -- asked a different question: for every dart
// event, where does each camera's tip sit RELATIVE TO THE BOARD'S EDGE? The issue's two
// candidate mechanisms separate on exactly that number. A tip a few millimetres past the
// true 170 mm wire but inside the TRACED outer-double ellipse (bloom pushes traced outer
// edges out by +2.8..+3.9 mm on this rig -- #1510's held-out residuals) is the radial
// ruler being generous at the edge; a tip tens of millimetres inside the board on a dart
// that landed OFF it is a tip found on the wrong object (#1494/#1495's family).
//
// So every camera's reading prints, per dart event:
//
//   I1505CAM dart=N cam=K tipFound=.. tip=x,y score=.. ring=.. rulerR=..   the scorer's
//       own verdict on this tip: score/ring from scorePoint, rulerR its radial-ruler
//       radius (1.0 = the outer edge of the double ring, as the traced marks read it)
//   ... d=<px> tOut=<px> tIn=<px> innerInBand=0|1 tEdgeDebiased=<px> mmTraced=..
//       mmDebiased=..
//       d is the tip's pixel distance from the bull along its own ray; tOut/tIn are the
//       ray's crossings of the traced outer/inner doubles ellipses; tEdgeDebiased is the
//       170 mm mark #1499's two-marks rule puts on that ray (average of the outer mark
//       and the inner mark scaled by 170/162, when the inner mark sits in its own #1485
//       band); mmTraced reads the tip against the traced outer mark as 170 mm and
//       mmDebiased against the de-biased mark. The difference between those two numbers
//       IS the bloom, per camera, on this ray.
//
// and the vote's outcome prints as I1505VOTE, chooseScore over the same readings with
// processScore's own may_vote rule, so the probe's dart sequence can be aligned with the
// real run's SCORE lines. I1505VISITEND marks the board going CLEAN, which is how visits
// are separated.
//
// It writes one annotated JPEG per camera per dart event into <outdir>: the raw frame,
// the traced doubles ellipse (green), the physical rim the tip search is masked to
// (yellow, x 225.5/170 -- #1364), the de-biased edge crossing on the tip's own ray
// (cyan tick) and the tip (red).
#include <opencv2/opencv.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "ellipse_processing.hpp"
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
        snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }

    // The instrument's own copy of the ray crossing (score_processing's is static in the
    // .cpp on purpose). Distance along the ray from `origin` in unit direction `u` to the
    // boundary of `ellipse`, or a negative number where the ray never crosses it.
    double rayCrossing(cv::Point2f origin, cv::Point2f u, const cv::RotatedRect &ellipse)
    {
        const double a = ellipse.size.width / 2.0, b = ellipse.size.height / 2.0;
        if (!(a > 0.0) || !(b > 0.0)) return -1.0;
        const double theta = -ellipse.angle * CV_PI / 180.0;
        const double c = std::cos(theta), sn = std::sin(theta);
        const cv::Point2f rel = origin - ellipse.center;
        const double x0 = rel.x * c - rel.y * sn, y0 = rel.x * sn + rel.y * c;
        const double dx = u.x * c - u.y * sn, dy = u.x * sn + u.y * c;
        const double A = (dx * dx) / (a * a) + (dy * dy) / (b * b);
        const double B = 2.0 * ((x0 * dx) / (a * a) + (y0 * dy) / (b * b));
        const double C = (x0 * x0) / (a * a) + (y0 * y0) / (b * b) - 1.0;
        const double disc = B * B - 4.0 * A * C;
        if (disc < 0.0 || !(A > 0.0)) return -1.0;
        const double t = (-B + std::sqrt(disc)) / (2.0 * A);
        return t > 0.0 ? t : -1.0;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1505_edge_probe <outdir> <clip> [<clip> ...]\n";
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

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        std::cout << "I1505CALIB cam=" << (i + 1)
                  << " scorable=" << (score_processing::aDartIsScoredFrom(calib) ? 1 : 0)
                  << " bull=" << fmt(calib.bullCenter.x) << "," << fmt(calib.bullCenter.y)
                  << " outerDouble=" << fmt(ellipse_processing::ringReach(calib.ellipses.outerDoubleEllipse))
                  << " innerDouble=" << fmt(ellipse_processing::ringReach(calib.ellipses.innerDoubleEllipse))
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
        if (dart_result.current_state == dart_processing::DartBoardState::CLEAN)
        {
            std::cout << "I1505VISITEND cycle=" << cycle << std::endl;
            continue;
        }

        dart++;
        std::vector<score_processing::PointScore> point_scores(dart_result.camera_results.size());
        std::vector<bool> may_vote(dart_result.camera_results.size(), false);
        for (size_t i = 0; i < dart_result.camera_results.size() && i < calibrations.size(); i++)
        {
            const dart_processing::CameraDetectionResult &cam = dart_result.camera_results[i];
            const DartboardCalibration &calib = calibrations[i];
            const bool usable = cam.frame_available && calib.sees_board && cam.tip_found;

            std::cout << "I1505CAM dart=" << dart << " cycle=" << cycle
                      << " state=" << dart_processing::getDartBoardStateName(dart_result.current_state)
                      << " cam=" << (i + 1)
                      << " tipFound=" << (cam.tip_found ? 1 : 0)
                      << " tip=" << fmt(cam.tip_position.x) << "," << fmt(cam.tip_position.y);

            if (!usable)
            {
                std::cout << " score=- ring=- rulerR=none" << std::endl;
                continue;
            }

            const score_processing::PointScore ps = score_processing::scorePoint(cam.tip_position, calib);
            point_scores[i] = ps;
            may_vote[i] = cam.tip_found && score_processing::aVoteIsCast(ps);

            const cv::Point2f bull = cv::Point2f(calib.bullCenter);
            const cv::Point2f rel = cam.tip_position - bull;
            const double d = std::sqrt(rel.x * rel.x + rel.y * rel.y);
            std::string edgeSaid = " d=" + fmt(d) + " tOut=none tIn=none innerInBand=0 tEdgeDebiased=none mmTraced=none mmDebiased=none";
            double tEdge = -1.0;
            if (d > 0.0)
            {
                const cv::Point2f u = rel * (1.0f / (float)d);
                const double tOut = rayCrossing(bull, u, calib.ellipses.outerDoubleEllipse);
                const double tIn = rayCrossing(bull, u, calib.ellipses.innerDoubleEllipse);
                if (tOut > 0.0)
                {
                    // #1499's two-marks rule, applied to THIS ray: the inner mark is a
                    // second estimate of the 170 mm edge (over 162/170), trusted only
                    // where it sits inside its own #1485 band of the outer mark.
                    const double innerTruth = ellipse_processing::ringTruth(ellipse_processing::kInnerDouble);
                    const bool innerInBand = tIn > 0.0 &&
                        (tIn / tOut) >= ellipse_processing::ringBandLow(ellipse_processing::kInnerDouble) &&
                        (tIn / tOut) <= ellipse_processing::ringBandHigh(ellipse_processing::kInnerDouble);
                    tEdge = innerInBand ? 0.5 * (tOut + tIn / innerTruth) : tOut;
                    edgeSaid = " d=" + fmt(d) + " tOut=" + fmt(tOut) +
                               " tIn=" + (tIn > 0.0 ? fmt(tIn) : std::string("none")) +
                               " innerInBand=" + (innerInBand ? "1" : "0") +
                               " tEdgeDebiased=" + fmt(tEdge) +
                               " mmTraced=" + fmt(d / tOut * kOuterDoubleMm) +
                               " mmDebiased=" + fmt(d / tEdge * kOuterDoubleMm);
                }
            }
            std::cout << " score=" << ps.score << " ring=" << (ps.ring.empty() ? "-" : ps.ring)
                      << " rulerR=" << (ps.board.has_radius ? fmt(ps.board.radius) : std::string("none"))
                      << edgeSaid << std::endl;

            if (!images[i].empty() && !outdir.empty())
            {
                cv::Mat pic = images[i].clone();
                cv::ellipse(pic, calib.ellipses.outerDoubleEllipse, cv::Scalar(0, 255, 0), 1);
                cv::RotatedRect physical = calib.ellipses.outerDoubleEllipse;
                physical.size.width *= 225.5f / 170.0f;
                physical.size.height *= 225.5f / 170.0f;
                cv::ellipse(pic, physical, cv::Scalar(0, 255, 255), 1);
                cv::circle(pic, calib.bullCenter, 4, cv::Scalar(255, 0, 0), -1);
                if (cam.tip_found)
                {
                    cv::circle(pic, cam.tip_position, 10, cv::Scalar(0, 0, 255), 2);
                    if (tEdge > 0.0 && d > 0.0)
                    {
                        const cv::Point2f u = (cam.tip_position - bull) * (1.0f / (float)d);
                        const cv::Point2f mark = bull + u * (float)tEdge;
                        cv::line(pic, mark + cv::Point2f(-u.y, u.x) * 12.0f,
                                 mark - cv::Point2f(-u.y, u.x) * 12.0f, cv::Scalar(255, 255, 0), 2);
                    }
                }
                char name[256];
                snprintf(name, sizeof(name), "%s/dart%02d_cam%zu.jpg", outdir.c_str(), dart, i + 1);
                cv::imwrite(name, pic);
            }
        }

        // The published verdict comes from processScore itself -- the same call the
        // detector makes -- so the vote path this probe reports is the one that ships,
        // the `may_vote` line included (#1505 moved the MISS half of that line into
        // `aVoteIsCast`; a probe voting by its own copy of the old rule would measure a
        // rule that no longer runs). The per-camera chooseScore above is kept only as a
        // cross-check that the two agree on the same readings.
        const score_processing::ScoreResult published =
            score_processing::processScore(background_frames, dart_result, calibrations, false);
        const score_processing::ScoreChoice choice = score_processing::chooseScore(point_scores, may_vote);
        std::cout << "I1505VOTE dart=" << dart
                  << " published=" << (published.valid ? published.score : std::string("(invalid)"))
                  << " confidence=" << fmt(published.confidence)
                  << " camera=" << published.camera_index
                  << " replayAgrees=" << ((choice.camera >= 0 ? point_scores[choice.camera].score : std::string("MISS")) == published.score ? 1 : 0)
                  << " agreeing=" << choice.agreeing << std::endl;
    }

    std::cout << "I1505END cycles=" << cycle << " darts=" << dart << std::endl;
    return 0;
}

// turnaus#1906: OD_FOCAL_PX, held pure -- the pin's parse, the focal each camera is given, the
// intrinsic matrix built from it, and a pose solved through that matrix.
//
// Each assertion's label is what i1906_check.sh's mutations predict red:
//
//   parse:   unset, empty, a word, zero, a negative, an empty slot, over 100000 and a
//            trailing junk character are all NO pin (an empty list), never half a pin;
//            "690" is one value and "690,700.5,712" three, in order
//   default: with OD_FOCAL_PX unset the process reads no pin, and every camera is solved
//            at the 120-degree guess
//   guess:   the guess is today's formula: 1280 wide, doubles 390 px across -> 423.9 px
//   camera:  a single value reaches every camera; a list reaches camera i with slot i, and
//            a camera past the list's end keeps the guess, marked as not pinned
//   matrix:  cameraMatrixFor(690, 1280, 720) is [690 0 640; 0 690 360; 0 0 1]
//   pose:    board points imaged by a planted f = 690 camera at 388 mm: solvePnP through
//            the 690 matrix recovers the distance to 0.5 mm and reprojects under 0.01 px;
//            through the 424 matrix the distance is off by over 50 mm
//   clause:  the calibration line's words name f and where it came from
//
//   compiled by i1906_check.sh against the tree and against mutated scratch copies.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>

#include "detector/geometry/calibration/focal_pin.hpp"

using namespace focal_pin;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string f3(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

// Solve the planted board through a matrix of focal `f`; returns the recovered distance (mm)
// and writes the mean reprojection error (px).
static double solvedDistance(double f, double &meanReprojPx)
{
    const int cols = 1280, rows = 720;
    const double trueF = 690.0;
    // The board: wire-ring crossings on the doubles and trebles, 20 wires, z = 0 (mm).
    std::vector<cv::Point3f> board;
    const double radii[] = {99.0, 107.0, 162.0, 170.0};
    for (int w = 0; w < 20; w++)
    {
        const double a = (w * 18.0 + 9.0) * CV_PI / 180.0;
        for (double r : radii)
        {
            board.emplace_back(static_cast<float>(r * std::cos(a)), static_cast<float>(r * std::sin(a)), 0.0f);
        }
    }
    // A camera 388 mm off the bull, looking at it from 35 degrees above the face's normal.
    const cv::Mat rvec = (cv::Mat_<double>(3, 1) << 35.0 * CV_PI / 180.0, 0.0, 0.0);
    const cv::Mat tvec = (cv::Mat_<double>(3, 1) << 0.0, 0.0, 388.0);
    std::vector<cv::Point2f> image;
    cv::projectPoints(board, rvec, tvec, cameraMatrixFor(trueF, cols, rows), cv::Mat(), image);

    cv::Mat r, t;
    cv::solvePnP(board, image, cameraMatrixFor(f, cols, rows), cv::Mat(), r, t);
    std::vector<cv::Point2f> back;
    cv::projectPoints(board, r, t, cameraMatrixFor(f, cols, rows), cv::Mat(), back);
    double sum = 0.0;
    for (size_t i = 0; i < back.size(); i++)
    {
        sum += cv::norm(back[i] - image[i]);
    }
    meanReprojPx = sum / back.size();
    return cv::norm(t);
}

int main()
{
    // ---- parse ----------------------------------------------------------------------------
    const char *refused[] = {"", "abc", "0", "-5", "690,", ",690", "690,,700", "1e9", "690x", "690,abc", "nan"};
    for (const char *t : refused)
    {
        const std::vector<double> v = focalPinFrom(t);
        say(v.empty(), std::string("parse: \"") + t + "\" is no pin (got " + std::to_string(v.size()) + " value(s))");
    }
    say(focalPinFrom(nullptr).empty(), "parse: unset is no pin");
    {
        const std::vector<double> v = focalPinFrom("690");
        say(v.size() == 1 && v[0] == 690.0, "parse: \"690\" is one value, 690");
    }
    {
        const std::vector<double> v = focalPinFrom("690,700.5,712");
        say(v.size() == 3 && v[0] == 690.0 && v[1] == 700.5 && v[2] == 712.0,
            "parse: \"690,700.5,712\" is three values in order");
    }

    // ---- default --------------------------------------------------------------------------
    unsetenv("OD_FOCAL_PX");
    say(focalPin().empty(), "default: OD_FOCAL_PX unset reads no pin");
    say(!focalPinRefused(), "default: an unset pin is not a refused one");
    {
        const Focal f = focalFor(focalPin(), 1, 1280, 390.0);
        say(!f.pinned && std::fabs(f.px - guessedFocalPx(1280, 390.0)) < 1e-9,
            "default: an unset pin solves at the guess (" + f3(f.px) + " px)");
    }

    // ---- guess ----------------------------------------------------------------------------
    {
        const double g = guessedFocalPx(1280, 390.0);
        const double expected = 640.0 / std::tan(CV_PI / 3.0) * 195.0 / 170.0;
        say(std::fabs(g - expected) < 1e-9 && std::fabs(g - 423.9) < 0.1,
            "guess: 1280 wide, doubles 390 px across -> " + f3(g) + " px (today's formula, about 424)");
    }

    // ---- camera ---------------------------------------------------------------------------
    {
        const std::vector<double> one = focalPinFrom("690");
        bool all = true;
        for (int cam = 0; cam < 4; cam++)
        {
            const Focal f = focalFor(one, cam, 1280, 390.0);
            all = all && f.pinned && f.px == 690.0;
        }
        say(all, "camera: a single value reaches every camera");

        const std::vector<double> list = focalPinFrom("690,700.5,712");
        const Focal c0 = focalFor(list, 0, 1280, 390.0);
        const Focal c1 = focalFor(list, 1, 1280, 390.0);
        const Focal c2 = focalFor(list, 2, 1280, 390.0);
        const Focal c3 = focalFor(list, 3, 1280, 390.0);
        say(c0.pinned && c0.px == 690.0, "camera: camera 1 takes slot 1 (" + f3(c0.px) + ")");
        say(c1.pinned && c1.px == 700.5, "camera: camera 2 takes slot 2 (" + f3(c1.px) + ")");
        say(c2.pinned && c2.px == 712.0, "camera: camera 3 takes slot 3 (" + f3(c2.px) + ")");
        say(!c3.pinned && std::fabs(c3.px - guessedFocalPx(1280, 390.0)) < 1e-9,
            "camera: camera 4, past the list, keeps the guess (" + f3(c3.px) + ")");
    }

    // ---- matrix ---------------------------------------------------------------------------
    {
        const cv::Mat k = cameraMatrixFor(690.0, 1280, 720);
        const double want[9] = {690, 0, 640, 0, 690, 360, 0, 0, 1};
        bool same = k.rows == 3 && k.cols == 3 && k.type() == CV_64F;
        for (int i = 0; same && i < 9; i++)
        {
            same = k.at<double>(i / 3, i % 3) == want[i];
        }
        say(same, "matrix: cameraMatrixFor(690, 1280, 720) is [690 0 640; 0 690 360; 0 0 1]");
    }

    // ---- pose -----------------------------------------------------------------------------
    {
        double reprojAt690 = 0.0, reprojAt424 = 0.0;
        const double d690 = solvedDistance(690.0, reprojAt690);
        const double d424 = solvedDistance(424.0, reprojAt424);
        say(std::fabs(d690 - 388.0) < 0.5 && reprojAt690 < 0.01,
            "pose: at the planted f the distance is " + f3(d690) + " mm (388) and the reprojection " +
                f3(reprojAt690) + " px");
        say(std::fabs(d424 - 388.0) > 50.0,
            "pose: at 424 px the distance is " + f3(d424) + " mm, reprojection " + f3(reprojAt424) + " px");
    }

    // ---- clause ---------------------------------------------------------------------------
    {
        Focal pinned;
        pinned.px = 690.0;
        pinned.pinned = true;
        Focal guessed;
        guessed.px = 423.86;
        say(focalClause(pinned) == "solved at f=690.0 px (OD_FOCAL_PX)", "clause: " + focalClause(pinned));
        say(focalClause(guessed) == "solved at f=423.9 px (the 120-degree guess)", "clause: " + focalClause(guessed));
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

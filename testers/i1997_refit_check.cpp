// turnaus#1997: OD_RING_REFIT, held pure -- ring_refit.hpp and the place board_model.hpp
// applies it, against a board drawn through a known map, with OpenCV and wire_model.cpp.
//
// The board is DRAWN through one projective map (the truth: bands at the spec's 99..107 and
// 162..170 mm, blurred). The calibration handed to `fitBoardToCamera` is that truth seen
// WRONG the way #1992's census found the rig's cameras wrong: the doubles ellipses 2% large
// (the mask's bloom) and the bull three pixels off (the whole tilt signal), so today's map
// reads the rings inward and turning. Its treble ellipses are the mask's too, 3 mm wide of
// the band each side, so #1553's bloom adjustment has something to adjust. Each label is
// one assertion, and i1997_refit_check.sh's mutations predict them by name:
//
//   parse:    unset, empty, `off`, `1`, `ON` are off; `on` is on; `bloom` is bloom
//   default:  with OD_RING_REFIT unset the process reads off
//   record:   the calibration is its record from before #1997 plus the map, nothing between
//   wrong:    CONTROL -- through today's map the census reads a ring more than 1 mm off
//   refit:    the refit is applied, stamped `on`, and its sentence says refitted
//   rings:    through fitBoardToCamera's plane of the calibration CARRYING the refit, the
//             census reads all four rings within 0.3 mm all round and turning
//   darts:    48 points at 103 and 166 mm of the true board read within 0.4 mm of their
//             radius through that fit -- and more than 1 mm off through today's
//   wedge:    40 mid-wedge points score the same wedge through both fits (the comb keeps
//             the rotation), and the refitted one is the true one
//   bloom:    `on` scores the treble at the spec's edges (no adjustment; a point at 97.5 mm
//             is a single), `bloom` keeps #1553's (it is a treble)
//   off:      a calibration carrying no refit builds today's plane, bit for bit
//   cache:    a cache written under another setting is refused in words naming both; under
//             the same setting, not
//   blank:    a black frame keeps today's map and says a ring was not read
//   clipped:  a frame whose right side is cut off keeps today's map and names the arc
//
//   compiled by i1997_refit_check.sh against the tree and against mutated scratch copies.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/calibration/board_model.hpp"

using namespace board_model;

static int failures = 0;

static void say(bool ok, const std::string &label, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << label << " " << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string num(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%+.2f", v);
    return buf;
}

// ---- the truth (i1510_board_check.cpp's construction) ----------------------------------

static const double kComb = 0.13;

static cv::Matx33d makeTruth(double cx, double cy, double semiA, double semiB, double phiDeg,
                             double tiltR, double tiltArg)
{
    const double phi = phiDeg * CV_PI / 180.0;
    const double c = std::cos(phi), s = std::sin(phi);
    const cv::Matx33d A(semiA * c, -semiB * s, cx, semiA * s, semiB * c, cy, 0.0, 0.0, 1.0);
    const double t = std::atanh(tiltR);
    const double ch = std::cosh(t), sh = std::sinh(t);
    const double cp = std::cos(tiltArg), sp = std::sin(tiltArg);
    const cv::Matx33d rot(cp, -sp, 0, sp, cp, 0, 0, 0, 1);
    const cv::Matx33d rotT(cp, sp, 0, -sp, cp, 0, 0, 0, 1);
    const cv::Matx33d boost(ch, 0, sh, 0, 1, 0, sh, 0, ch);
    return A * (rot * boost * rotT);
}

/** A point of the true board, millimetres and board angle, in the image. */
static cv::Point2f imageOf(const cv::Matx33d &H, double mm, double theta)
{
    const double u = mm / 170.0;
    const cv::Vec3d q = H * cv::Vec3d(u * std::cos(theta), u * std::sin(theta), 1.0);
    return cv::Point2f((float)(q[0] / q[2]), (float)(q[1] / q[2]));
}

static cv::RotatedRect ringSeenAt(const cv::Matx33d &H, double radiusMm)
{
    std::vector<cv::Point2f> points;
    for (int i = 0; i < 90; i++)
    {
        points.push_back(imageOf(H, radiusMm, 2.0 * CV_PI * i / 90.0));
    }
    return cv::fitEllipse(points);
}

/** The truth, seen wrong: doubles 2% large, the bull three pixels off, the treble's mask 3 mm wide a side. */
static DartboardCalibration calibrationSeenWrong(const cv::Matx33d &H)
{
    DartboardCalibration calib;
    const cv::Point2f bull = imageOf(H, 0.0, 0.0);
    calib.bullCenter = cv::Point((int)std::lround(bull.x) + 2, (int)std::lround(bull.y) - 3);
    calib.camera_index = 0;
    calib.sees_board = true;
    calib.ellipses.outerDoubleEllipse = ringSeenAt(H, 170.0 * 1.02);
    calib.ellipses.innerDoubleEllipse = ringSeenAt(H, 162.0 * 1.02);
    calib.ellipses.outerTripleEllipse = ringSeenAt(H, 110.0);
    calib.ellipses.innerTripleEllipse = ringSeenAt(H, 96.0);
    calib.ellipses.outerBullEllipse = ringSeenAt(H, 15.9);
    calib.ellipses.innerBullEllipse = ringSeenAt(H, 6.35);
    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.validOuterPoints = 108;
    calib.ellipses.validInnerPoints = 100;
    for (int k = 0; k < 20; k++)
    {
        calib.wires.wireEndpoints.add(imageOf(H, 170.0, kComb + k * wire_model::kSector));
    }
    calib.wires.wiresDetected = 20;
    calib.wires.isValid = true;
    calib.orientation.anchored = true;
    calib.orientation.wedge20WireIndex = 3;
    calib.orientation.wedgeNumber = 6;
    calib.orientation.cameraPosition = orientation_processing::CameraPosition::CONFIGURED;
    return calib;
}

static cv::Mat drawBoard(const cv::Matx33d &H)
{
    const cv::Matx33d Hinv = H.inv();
    cv::Mat img(720, 1280, CV_8UC3);
    for (int y = 0; y < img.rows; y++)
    {
        for (int x = 0; x < img.cols; x++)
        {
            const cv::Vec3d q = Hinv * cv::Vec3d(x, y, 1.0);
            const double X = q[0] / q[2], Y = q[1] / q[2];
            const double mm = 170.0 * std::sqrt(X * X + Y * Y), th = std::atan2(Y, X);
            const bool even = ((int)std::floor((th - kComb + 4.0 * CV_PI) / wire_model::kSector) % 2) == 0;
            const bool band = (mm >= 99.0 && mm < 107.0) || (mm >= 162.0 && mm < 170.0);
            cv::Vec3d c = (mm < 170.0 && !even) ? cv::Vec3d(200, 220, 230) : cv::Vec3d(25, 25, 25);
            if (band)
            {
                c = even ? cv::Vec3d(30, 30, 200) : cv::Vec3d(30, 160, 30);
            }
            img.at<cv::Vec3b>(y, x) = cv::Vec3b((uchar)c[0], (uchar)c[1], (uchar)c[2]);
        }
    }
    cv::GaussianBlur(img, img, cv::Size(0, 0), 1.5);
    return img;
}

static double worstOf(const ring_census::Census &c, bool &allRead)
{
    double worst = 0.0;
    allRead = true;
    for (int i = 0; i < 4; i++)
    {
        allRead = allRead && c.ring[i].read;
        worst = std::max(worst, std::max(std::fabs(c.ring[i].allRoundMm), c.ring[i].turningMm));
    }
    return worst;
}

/** The worst radial error, mm, of 48 true points at 103 and 166 mm read through a fit. */
static double worstDart(const cv::Matx33d &truth, const BoardFit &fit)
{
    double worst = 0.0;
    for (int i = 0; i < 24; i++)
    {
        for (double mm : {103.0, 166.0})
        {
            const cv::Point2f b = boardPointOf(fit, imageOf(truth, mm, 2.0 * CV_PI * i / 24.0 + 0.05));
            worst = std::max(worst, std::fabs(std::sqrt((double)b.x * b.x + (double)b.y * b.y) - mm));
        }
    }
    return worst;
}

int main()
{
    using namespace ring_refit;
    say(settingFrom(nullptr) == Off && settingFrom("") == Off && settingFrom("off") == Off &&
            settingFrom("1") == Off && settingFrom("ON") == Off && settingFrom("on") == On &&
            settingFrom("bloom") == Bloom,
        "parse:", "unset, empty, off, 1 and ON are off; on is on; bloom is bloom");
    say(asked() == Off, "default:", "OD_RING_REFIT unset reads off");
    say(kCalibrationRecordBytesBefore1997 + sizeof(Map) == sizeof(DartboardCalibration),
        "record:", "the record before #1997 is " + std::to_string(kCalibrationRecordBytesBefore1997) +
                       " bytes, the map " + std::to_string(sizeof(Map)) + ", the calibration " +
                       std::to_string(sizeof(DartboardCalibration)));

    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const double ringMm[4] = {profile.innerTripleRadiusMm, profile.outerTripleRadiusMm,
                              profile.innerDoubleRadiusMm, profile.outerDoubleRadiusMm};
    const cv::Matx33d truth = makeTruth(640.0, 360.0, 300.0, 240.0, 25.0, 0.18, 0.7);
    const cv::Mat frame = drawBoard(truth);
    DartboardCalibration calib = calibrationSeenWrong(truth);
    const cv::Point2f bull = geometry_calibration::planeBullOf(calib);

    const BoardFit today = fitBoardToCamera(profile, calib);
    bool allRead = false;
    const ring_census::Census before = ring_census::read(frame, today.plane.Hinv, today.unitPerMm, bull, ringMm);
    const double worstBefore = worstOf(before, allRead);
    say(today.planeBuilt && allRead && worstBefore > 1.0 && !today.ringRefitted,
        "wrong:", "CONTROL through today's map: " + ring_census::story(before).substr(0, 150));

    const Result result = fit(frame, today.plane.Hinv, today.unitPerMm, bull, ringMm, On);
    say(result.map.state == On && result.story.find("the map refitted through") != std::string::npos &&
            result.map.rays > 1500,
        "refit:", result.story);

    calib.ringRefit = result.map;
    const BoardFit refitted = fitBoardToCamera(profile, calib);
    const ring_census::Census after =
        ring_census::read(frame, refitted.plane.Hinv, refitted.unitPerMm, bull, ringMm);
    const double worstAfter = worstOf(after, allRead);
    say(allRead && worstAfter < 0.3 && refitted.ringRefitted,
        "rings:", "through the carried map: " + ring_census::story(after).substr(0, 150));

    const double dartsToday = worstDart(truth, today), dartsRefit = worstDart(truth, refitted);
    say(dartsRefit < 0.4 && dartsToday > 1.0,
        "darts:", "48 true points read " + num(dartsRefit) + " mm off at worst through the refit, " +
                      num(dartsToday) + " through today's map");

    {
        const std::vector<cv::Point2f> ends(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
        const ModelAnchor a0 = anchorOnBoard(today, ends, calib.orientation.wedge20WireIndex);
        const ModelAnchor a1 = anchorOnBoard(refitted, ends, calib.orientation.wedge20WireIndex);
        int same = 0, total = 0;
        double worstInto = 1e9;
        for (int k = 0; k < 20; k++)
        {
            for (double mm : {60.0, 130.0})
            {
                const cv::Point2f p = imageOf(truth, mm, kComb + (k + 0.5) * wire_model::kSector);
                const ModelScore s0 = scoreFromModel(profile, today, a0, p);
                const ModelScore s1 = scoreFromModel(profile, refitted, a1, p);
                total++;
                same += (s0.wedgeResolved && s1.wedgeResolved && s0.segment == s1.segment) ? 1 : 0;
                // mid-wedge at this radius is half a sector of arc from either wire
                worstInto = std::min(worstInto, s1.wedgeBoundaryMm / (mm * wire_model::kSector / 2.0));
            }
        }
        say(same == total && worstInto > 0.85,
            "wedge:", std::to_string(same) + " of " + std::to_string(total) +
                          " mid-wedge points score the same wedge through both fits; through the refit the "
                          "nearest wire is never under " + num(worstInto) + " of half a wedge away");
    }

    {
        DartboardCalibration bloomCalib = calib;
        bloomCalib.ringRefit.state = Bloom;
        const BoardFit kept = fitBoardToCamera(profile, bloomCalib);
        const double onIn = trebleBloomAdjustMm(profile, refitted, ellipse_processing::kInnerTriple);
        const double onOut = trebleBloomAdjustMm(profile, refitted, ellipse_processing::kOuterTriple);
        const double keptIn = trebleBloomAdjustMm(profile, kept, ellipse_processing::kInnerTriple);
        const double keptOut = trebleBloomAdjustMm(profile, kept, ellipse_processing::kOuterTriple);
        const std::vector<cv::Point2f> ends(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
        const ModelAnchor a = anchorOnBoard(refitted, ends, calib.orientation.wedge20WireIndex);
        const cv::Point2f p = imageOf(truth, 97.5, kComb + 2.5 * wire_model::kSector);
        const std::string onWord = scoreFromModel(profile, refitted, a, p).ringWord;
        const std::string keptWord = scoreFromModel(profile, kept, a, p).ringWord;
        say(refitted.specTreble && !kept.specTreble && onIn == 0.0 && onOut == 0.0 &&
                std::fabs(keptIn + 3.0) < 0.5 && std::fabs(keptOut - 3.0) < 0.5 &&
                onWord == "single" && keptWord == "triple",
            "bloom:", "on adjusts the treble by " + num(onIn) + " / " + num(onOut) + " mm and reads 97.5 mm as a " +
                          onWord + "; bloom by " + num(keptIn) + " / " + num(keptOut) + " and reads it as a " + keptWord);
    }

    {
        DartboardCalibration plain = calibrationSeenWrong(truth);
        DartboardCalibration refused = plain;
        refused.ringRefit = result.map;
        refused.ringRefit.state = Off; // the numbers are there; the camera was refused
        const BoardFit a = fitBoardToCamera(profile, plain), b = fitBoardToCamera(profile, refused);
        say(std::memcmp(a.plane.H.val, today.plane.H.val, sizeof(a.plane.H.val)) == 0 &&
                std::memcmp(b.plane.Hinv.val, today.plane.Hinv.val, sizeof(b.plane.Hinv.val)) == 0 &&
                a.story == today.story && b.story == today.story && !b.ringRefitted,
            "off:", "a calibration carrying no applied refit builds today's plane and says today's sentence");
    }

    {
        const std::string a = cacheRefusal(Off, On), b = cacheRefusal(On, Off), c = cacheRefusal(On, Bloom);
        say(cacheRefusal(Off, Off).empty() && cacheRefusal(On, On).empty() && cacheRefusal(Bloom, Bloom).empty() &&
                a.find("written with OD_RING_REFIT unset") != std::string::npos &&
                a.find("runs with OD_RING_REFIT on") != std::string::npos &&
                b.find("written with OD_RING_REFIT on") != std::string::npos &&
                b.find("runs with OD_RING_REFIT unset") != std::string::npos &&
                c.find("written with OD_RING_REFIT on") != std::string::npos &&
                c.find("runs with OD_RING_REFIT bloom") != std::string::npos,
            "cache:", a.empty() ? "a cache written unset is ACCEPTED under on" : a);
    }

    {
        const Result none = fit(cv::Mat(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0)), today.plane.Hinv,
                                today.unitPerMm, bull, ringMm, On);
        say(none.map.state == Off && none.story.find("not read on enough rays") != std::string::npos &&
                none.story.find("this camera keeps today's map") != std::string::npos,
            "blank:", none.story);
    }

    {
        cv::Mat cut = frame.clone();
        cut(cv::Rect(840, 0, cut.cols - 840, cut.rows)).setTo(cv::Scalar(0, 0, 0));
        const Result clipped = fit(cut, today.plane.Hinv, today.unitPerMm, bull, ringMm, On);
        say(clipped.map.state == Off && clipped.story.find("degrees is missing from") != std::string::npos &&
                clipped.story.find("this camera keeps today's map") != std::string::npos,
            "clipped:", clipped.story);
    }

    std::cout << "I1997 refit check: " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

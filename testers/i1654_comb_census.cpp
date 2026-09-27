// #1654: how far the wire comb's theta20 moves from one look of the SAME camera to the next.
//
// #1653 found the same unmoved camera's theta20 differing by up to 0.65 degrees between
// the dev and the opening calibration, and put the comb's statistical error at 0.3-0.4
// degrees without measuring it. This program measures it: one clip, both windows, the
// averaged frame and every look a board would take after it (#1445's spacing, #1605's
// budget), each calibrated with geometry_calibration::calibrateSingleCamera -- the
// function the detector calls -- and each fitted to the board model with
// board_model::fitBoardToCamera, which is where the geometric entry's theta20 comes from
// (anchorOnBoard snaps it to fit.wireOffset, the comb over the twenty endpoints).
//
// A COMMON FRAME. A look's own theta20 is an angle in its own plane, and its plane moves
// with its own bull and conic. Two looks that put the 12/9 wire on the same image pixels
// can report different theta20 and vice versa, so the number that decides a dart is not
// theta20 but WHERE ON THE IMAGE the look puts the wire. So every look's boundaries are
// carried into ONE reference plane per camera -- the opening window's averaged frame --
// and read there as board angles:
//
//   rot    the circular mean, over the twenty boundaries at r = 100 mm, of (look's boundary
//          read in the reference plane) - (the reference's own boundary): how far the
//          look's whole comb is rotated against the reference, in degrees
//   w129   the same for the 12/9 boundary alone, at r = 88.6 mm (rig-20260922 dev v2.1's
//          radius, #1653): what decides that dart
//   own    the look's comb offset in its own plane, (-9, 9] degrees
//
// and the same three through the doubles band's centre-line plane (#1653's bloom-free
// plane: unit circle = 166 mm, fitted to the midpoints of the two traced doubles edges
// along rays from the bull), prefixed `m`.
//
//   i1654_comb_census <clip> <camera-index> [looks] [spacing] [averaged-frames]
//
// One line per measurement on stdout, prefixed I1654:
//
//   I1654 clip=<name> cam=<n> win=<open|dev> look=<avg|k> at=<frame> sees=<0|1> R=..
//         cand=.. inl=.. rms=.. snapped=.. bull=x,y tilt=.. conic=wxh gray=.. own=..
//         rot=.. w129=.. mown=.. mrot=.. mw129=.. read=<0|1>
//
// The dev window is the dev binary's DEBUG_SEEK_VIDEO seek (capture_opencv.hpp, copied as
// i1445_look_census.cpp copies it); the opening window is frame 0 (OD_SEEK_VIDEO=off).
#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "board_model.hpp"
#include "wire_processing.hpp"
#include "orientation_processing.hpp"

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    double deg(double r) { return r * 180.0 / kPi; }
    double wrapPi(double a)
    {
        while (a > kPi)
            a -= 2 * kPi;
        while (a <= -kPi)
            a += 2 * kPi;
        return a;
    }

    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0)
            return 0;
        return (int)(fps * seconds);
    }

    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out, int &consumed)
    {
        cv::Mat sum;
        int counted = 0;
        consumed = 0;
        for (int i = 0; i < numFrames; i++)
        {
            cv::Mat f;
            if (!cap.read(f) || f.empty())
                continue;
            consumed++;
            cv::Mat as_float;
            f.convertTo(as_float, CV_32F);
            if (counted == 0)
                sum = as_float;
            else
                sum += as_float;
            counted++;
        }
        if (counted == 0)
            return false;
        cv::Mat averaged = sum / (float)counted;
        averaged.convertTo(out, CV_8U);
        return true;
    }

    // Where a traced conic meets the ray from `o` along `d` (the far root).
    double reach(const cv::Point2d &o, const cv::Point2d &d, const cv::RotatedRect &e)
    {
        const double a = e.size.width * 0.5, b = e.size.height * 0.5;
        const double th = e.angle * kPi / 180.0, c = std::cos(th), s = std::sin(th);
        const double rx = o.x - e.center.x, ry = o.y - e.center.y;
        const double px = rx * c + ry * s, py = -rx * s + ry * c;
        const double dx = d.x * c + d.y * s, dy = -d.x * s + d.y * c;
        const double A = dx * dx / (a * a) + dy * dy / (b * b);
        const double B = 2 * (px * dx / (a * a) + py * dy / (b * b));
        const double C = px * px / (a * a) + py * py / (b * b) - 1;
        const double disc = B * B - 4 * A * C;
        if (!(A > 0) || disc < 0)
            return -1;
        return (-B + std::sqrt(disc)) / (2 * A);
    }

    // #1653's bloom-free plane: the doubles band's centre line, fitted as a conic, is the
    // image of the 166 mm circle whatever the (pixel-constant) bloom. Unit = 166 mm.
    board_model::BoardFit midbandFit(const board_model::BoardProfile &profile,
                                     const DartboardCalibration &calib, const board_model::BoardFit &asBuilt)
    {
        board_model::BoardFit fit = asBuilt;
        fit.planeBuilt = false;
        const cv::RotatedRect &eo = calib.ellipses.outerDoubleEllipse;
        const cv::RotatedRect &ei = calib.ellipses.innerDoubleEllipse;
        if (wire_processing::conicOfDoublesFor(calib) != 1.0 || !(ei.size.width > 0) || !(eo.size.width > 0))
            return fit;
        const cv::Point2d bull(calib.bullCenter.x + (calib.wires.centre_from_wires ? calib.wires.centre_dx : 0.0f),
                               calib.bullCenter.y + (calib.wires.centre_from_wires ? calib.wires.centre_dy : 0.0f));
        std::vector<cv::Point2f> mid;
        for (int i = 0; i < 360; i++)
        {
            const double a = 2 * kPi * i / 360.0;
            const cv::Point2d d(std::cos(a), std::sin(a));
            const double ro = reach(bull, d, eo), ri = reach(bull, d, ei);
            if (ro <= 0 || ri <= 0)
                continue;
            const double r = 0.5 * (ro + ri);
            mid.push_back(cv::Point2f((float)(bull.x + r * d.x), (float)(bull.y + r * d.y)));
        }
        if (mid.size() < 60)
            return fit;
        const cv::RotatedRect m = cv::fitEllipse(mid);
        fit.plane = wire_model::planeOf(m, cv::Point2f((float)bull.x, (float)bull.y), 1.0);
        if (!fit.plane.built)
            return fit;
        fit.planeBuilt = true;
        fit.unitPerMm = 1.0 / 166.0;
        (void)profile;
        std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
        const wire_model::Fit comb = wire_model::fitTwentyFold(fit.plane, endpoints);
        fit.wireOffset = comb.offset;
        return fit;
    }

    struct Ref
    {
        bool ok = false;
        board_model::BoardFit fit, mfit;
        double w129 = 0.0; // the reference's 12/9 boundary, board angle in its own plane
        bool have129 = false;
    };

    // The 12/9 boundary: the 20 starts at theta20 and the sequence advances by `advance`;
    // 20 1 18 4 13 6 10 15 2 17 3 19 7 16 8 11 14 9 12 5, so the 12 starts at boundary 18.
    bool boundary129(const board_model::BoardFit &fit, const DartboardCalibration &calib, double &out)
    {
        if (!orientation_processing::wedgeCanBeRead(calib.orientation))
            return false;
        std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
        const board_model::ModelAnchor anchor =
            board_model::anchorOnBoard(fit, endpoints, calib.orientation.wedge20WireIndex);
        if (!anchor.resolved)
            return false;
        out = anchor.theta20 + anchor.advance * 18.0 * wire_model::kSector;
        return true;
    }

    // The look's boundary nearest `target` (a reference-plane angle), read in the
    // reference plane at radius `mm`, minus target -- in degrees.
    double lookBoundaryAgainst(const board_model::BoardFit &look, const board_model::BoardFit &ref,
                               double target, double mm)
    {
        double best = 1e9;
        for (int k = 0; k < wire_model::kFold; k++)
        {
            const double th = look.wireOffset + k * wire_model::kSector;
            const cv::Point2f img = board_model::imageOfBoard(look, mm, th);
            const cv::Point2f b = board_model::boardPointOf(ref, img);
            const double d = wrapPi(std::atan2(b.y, b.x) - target);
            if (std::fabs(d) < std::fabs(best))
                best = d;
        }
        return deg(best);
    }

    // The whole comb's rotation against the reference: circular mean at fold 20 of the
    // look's twenty boundaries read in the reference plane, against the reference offset.
    double combRotation(const board_model::BoardFit &look, const board_model::BoardFit &ref)
    {
        double sr = 0, si = 0;
        for (int k = 0; k < wire_model::kFold; k++)
        {
            const double th = look.wireOffset + k * wire_model::kSector;
            const cv::Point2f img = board_model::imageOfBoard(look, 100.0, th);
            const cv::Point2f b = board_model::boardPointOf(ref, img);
            const double a = std::atan2(b.y, b.x) - ref.wireOffset;
            sr += std::cos(wire_model::kFold * a);
            si += std::sin(wire_model::kFold * a);
        }
        return deg(std::atan2(si, sr) / wire_model::kFold);
    }

    double grayOf(const cv::Mat &frame, const DartboardCalibration &calib)
    {
        if (!calib.ellipses.hasValidDoubles)
            return cv::mean(frame)[0];
        cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8U);
        cv::ellipse(mask, calib.ellipses.outerDoubleEllipse, cv::Scalar(255), -1);
        cv::Mat g;
        cv::cvtColor(frame, g, cv::COLOR_BGR2GRAY);
        return cv::mean(g, mask)[0];
    }

    void say(const std::string &clip, int cam, const std::string &win, const std::string &look, int at,
             const cv::Mat &frame, const DartboardCalibration &calib, Ref &ref)
    {
        const board_model::BoardProfile profile =
            board_model::profileFromSpec(perspective_processing::DartboardSpec());
        char head[400];
        snprintf(head, sizeof(head), "I1654 clip=%s cam=%d win=%s look=%s at=%d sees=%d R=%.4f cand=%d inl=%.2f snapped=%d",
                 clip.c_str(), cam + 1, win.c_str(), look.c_str(), at, calib.sees_board ? 1 : 0,
                 calib.wires.fit_coherence, calib.wires.fit_candidates, calib.wires.fit_inlier_fraction,
                 calib.wires.fit_snapped);
        std::cout << head;
        if (!calib.sees_board)
        {
            std::cout << std::endl;
            return;
        }
        const board_model::BoardFit fit =
            board_model::fitBoardToCamera(profile, calib, wire_processing::conicOfDoublesFor(calib));
        const board_model::BoardFit mfit = midbandFit(profile, calib, fit);
        if (!ref.ok && fit.planeBuilt && fit.wireBoundaries == wire_model::kFold)
        {
            ref.ok = true;
            ref.fit = fit;
            ref.mfit = mfit;
            ref.have129 = boundary129(fit, calib, ref.w129);
        }
        char line[600];
        snprintf(line, sizeof(line),
                 " rms=%.2f bull=%d,%d tilt=%.4f conic=%.1fx%.1f@%.1f gray=%.1f own=%.3f read=%d",
                 fit.wireRmsDeg, calib.bullCenter.x, calib.bullCenter.y, fit.plane.tilt,
                 calib.ellipses.outerDoubleEllipse.size.width, calib.ellipses.outerDoubleEllipse.size.height,
                 calib.ellipses.outerDoubleEllipse.angle, grayOf(frame, calib), deg(fit.wireOffset),
                 orientation_processing::wedgeCanBeRead(calib.orientation) ? 1 : 0);
        std::cout << line;
        if (calib.wires.centre_from_wires)
        {
            std::cout << cv::format(" ctr=%.2f,%.2f", calib.bullCenter.x + calib.wires.centre_dx,
                                    calib.bullCenter.y + calib.wires.centre_dy);
        }
        if (ref.ok && fit.planeBuilt && fit.wireBoundaries == wire_model::kFold)
        {
            std::cout << " rot=" << cv::format("%.3f", combRotation(fit, ref.fit));
            if (ref.have129)
            {
                std::cout << " w129=" << cv::format("%.3f", lookBoundaryAgainst(fit, ref.fit, ref.w129, 88.6));
            }
            if (mfit.planeBuilt && ref.mfit.planeBuilt)
            {
                // The 12/9 boundary of the reference, carried into ITS midband plane.
                std::cout << " mown=" << cv::format("%.3f", deg(mfit.wireOffset))
                          << " mrot=" << cv::format("%.3f", combRotation(mfit, ref.mfit));
                if (ref.have129)
                {
                    const cv::Point2f img = board_model::imageOfBoard(ref.fit, 88.6, ref.w129);
                    const cv::Point2f b = board_model::boardPointOf(ref.mfit, img);
                    const double w129m = std::atan2(b.y, b.x);
                    std::cout << " mw129=" << cv::format("%.3f", lookBoundaryAgainst(mfit, ref.mfit, w129m, 88.6));
                }
            }
        }
        std::cout << std::endl;
    }

    void window(const std::string &clipPath, const std::string &name, int cam, bool dev, int looks, int spacing,
                int averaged_frames, Ref &ref)
    {
        cv::VideoCapture cap(clipPath);
        if (!cap.isOpened())
        {
            std::cerr << "I1654 cannot open " << clipPath << std::endl;
            return;
        }
        const double fps = cap.get(cv::CAP_PROP_FPS);
        const int seek = dev ? seekFrameFor(cam, fps) : 0;
        if (seek > 0)
            cap.set(cv::CAP_PROP_POS_FRAMES, seek);
        const std::string win = dev ? "dev" : "open";
        cv::Mat averaged;
        int consumed = 0;
        if (!averageOf(cap, averaged_frames, averaged, consumed))
            return;
        int position = seek + consumed;
        say(name, cam, win, "avg", seek, averaged, geometry_calibration::calibrateSingleCamera(averaged, cam, false), ref);
        for (int k = 1; k <= looks; k++)
        {
            cv::Mat frame;
            bool have = false;
            for (int s = 0; s < spacing; s++)
            {
                have = cap.read(frame) && !frame.empty();
                if (!have)
                    break;
                position++;
            }
            if (!have)
                break;
            say(name, cam, win, std::to_string(k), position - 1, frame,
                geometry_calibration::calibrateSingleCamera(frame, cam, false), ref);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1654_comb_census <clip> <camera-index> [looks] [spacing] [averaged-frames]" << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    const int cam = atoi(argv[2]);
    const int looks = argc > 3 ? atoi(argv[3]) : 31;
    const int spacing = argc > 4 ? std::max(1, atoi(argv[4])) : 5;
    const int averaged_frames = argc > 5 ? atoi(argv[5]) : 30;

    std::string name = clip;
    const size_t cut = name.find_last_of('/');
    if (cut != std::string::npos && cut > 0)
    {
        const size_t prev = name.find_last_of('/', cut - 1);
        name = name.substr(prev == std::string::npos ? 0 : prev + 1);
    }
    Ref ref; // the opening window's averaged frame, the first calibration that builds a plane
    window(clip, name, cam, false, looks, spacing, averaged_frames, ref);
    window(clip, name, cam, true, looks, spacing, averaged_frames, ref);
    return 0;
}

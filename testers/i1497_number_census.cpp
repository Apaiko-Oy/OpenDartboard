// #1497: how big is a printed number on the board's number ring, in pixels, on the frame
// the detector really calibrates on -- and what does one LOOK like.
//
//   i1497_number_census <outdir> <clip1> [<clip2> ...]
//
// A MEASUREMENT AND NOT A CHANGE. Nothing under src/ moves for this file. It builds no
// reader -- no OCR, no template matching, no classifier -- asserts no threshold and tunes
// no constant (#1322). #1498 is the decision and it is the maintainer's.
//
// THIS IS #1493'S HARNESS WITH THE REPLAY REMOVED. The calibration half is line for line
// the same -- seek where DEBUG_SEEK_VIDEO seeks, capture::readAveraged(30),
// geometry_calibration::calibrateMultipleCameras, wire_model::planeOf recomputed exactly
// as wire_processing builds it -- because the averaged calibration frame is the frame a
// reader would be handed: anchoring happens once. Nothing after calibration is replayed,
// because a number ring does not move.
//
// WHAT A "CELL" IS HERE. The board plane maps the unit circle to the DOUBLES ring, so a
// board radius is in units of 170 mm (wire_model::planeOf: A carries the unit circle onto
// the conic scaled by conicOfDoubles, and conicRadius records where the FITTED conic sat).
// A board is 225.5 mm to its rim, so the number ring is the annulus r in [1.0, 1.326],
// cut into twenty by the wire model's own fitted phase. That annulus is what
// roi_processing.hpp means by "the number ring and the wire ends live between the doubles
// and the rim". This census crops each of the twenty and measures it.
//
// THE THREE NUMBERS, AND WHERE EACH COMES FROM.
//   radialPx      how deep the annulus is in pixels at the wedge's centre angle -- the
//                 room a glyph's HEIGHT has, since a board's numbers read outward.
//   tangentialPx  the chord across the wedge at the annulus's mid-radius -- the room a
//                 glyph's WIDTH has.
//   pxPerMm       the singular values of the Jacobian of the board->image map at that
//                 point, divided by 170. Two of them: a plane seen obliquely is
//                 compressed along one direction and not the other.
//   obliquityDeg  acos(pxPerMmMin / pxPerMmMax). Under the pinhole-plus-plane model the
//                 homography ALREADY assumes, the local foreshortening ratio is the
//                 cosine of the angle between the view ray and the board's normal. No
//                 intrinsics are introduced to get it -- that is why it is read from the
//                 map rather than from a pose (#1493 needed a pose and had to assume a
//                 focal length; this does not).
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "wire_model.hpp"
#include "wire_processing.hpp"
#include "orientation_processing.hpp"

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    /** The board's own millimetres, stated here because this file is a probe. */
    constexpr double kDoublesMm = 170.0;   // outer double wire
    constexpr double kRimMm = 225.5;       // the board's rim
    constexpr double kRingInner = 1.0;              // 170/170
    constexpr double kRingOuter = kRimMm / kDoublesMm; // 225.5/170 = 1.3265

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

    std::string fmt(double v, int prec = 2)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", prec, v);
        return buf;
    }

    cv::Point2f mapBoard(const wire_model::Plane &plane, double theta, double radius)
    {
        return wire_model::imageOfBoardAngle(plane, theta, radius);
    }

    /** The two singular values of the board->image Jacobian, in pixels per BOARD UNIT. */
    void jacobianScales(const wire_model::Plane &plane, double theta, double radius,
                        double &sMax, double &sMin)
    {
        const double eps = 1e-3;
        const double x = radius * std::cos(theta), y = radius * std::sin(theta);
        auto at = [&](double bx, double by)
        {
            const cv::Vec3d q = plane.H * cv::Vec3d(bx, by, 1.0);
            return cv::Point2d(q[0] / q[2], q[1] / q[2]);
        };
        const cv::Point2d p0 = at(x, y);
        const cv::Point2d px = at(x + eps, y);
        const cv::Point2d py = at(x, y + eps);
        cv::Matx22d J((px.x - p0.x) / eps, (py.x - p0.x) / eps,
                      (px.y - p0.y) / eps, (py.y - p0.y) / eps);
        cv::Mat w;
        cv::SVD::compute(cv::Mat(J), w);
        sMax = w.at<double>(0, 0);
        sMin = w.at<double>(1, 0);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1497_number_census <outdir> <clip> [<clip> ...]\n";
        return 2;
    }

    const std::string outDir = argv[1];
    const int cameras = argc - 2;

    std::vector<cv::VideoCapture> caps(cameras);
    std::vector<cv::Mat> initial_frames(cameras);
    for (int i = 0; i < cameras; i++)
    {
        caps[i].open(argv[i + 2]);
        if (!caps[i].isOpened())
        {
            std::cerr << "cannot open " << argv[i + 2] << "\n";
            return 2;
        }
        const double fps = caps[i].get(cv::CAP_PROP_FPS);
        caps[i].set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(i, fps));
        if (!averageOf(caps[i], 30, initial_frames[i]))
        {
            std::cerr << "no frames from " << argv[i + 2] << "\n";
            return 2;
        }
    }

    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    // The rectified cell is sampled at a FIXED millimetres-per-pixel so that every cell of
    // every camera is the same size on the contact sheet and a reader is comparing like
    // with like. 2 px/mm is above every scale measured here, so this is an upsample and
    // invents no detail -- the raw crop beside it is the unresampled truth.
    const double kRectPxPerMm = 2.0;
    const int rectH = (int)std::lround((kRimMm - kDoublesMm) * kRectPxPerMm);         // radial
    const int rectW = (int)std::lround(2.0 * std::sin(wire_model::kSector * 0.5) *
                                       ((kDoublesMm + kRimMm) * 0.5) * kRectPxPerMm); // tangential

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const cv::Mat &frame = initial_frames[i];
        const double conicOfDoubles = wire_processing::conicOfDoublesFor(calib);
        const wire_model::Plane plane =
            wire_model::planeOf(calib.ellipses.outerDoubleEllipse,
                                cv::Point2f(calib.bullCenter), conicOfDoubles);

        // The wedge phase, asked of the same fitter that placed the boundaries this
        // pipeline already scores against (#1493 reads it the same way).
        double offset = 0.0;
        bool phaseKnown = false;
        if (plane.built)
        {
            std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(),
                                               calib.wires.wireEndpoints.end());
            const wire_model::Fit fit = wire_model::fitTwentyFold(plane, endpoints);
            if (fit.built)
            {
                offset = fit.offset;
                phaseKnown = true;
            }
        }

        const double semiMajor = 0.5 * std::max(calib.ellipses.outerDoubleEllipse.size.width,
                                                calib.ellipses.outerDoubleEllipse.size.height);

        std::cout << "I1497CAM cam=" << (i + 1)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " frameW=" << frame.cols << " frameH=" << frame.rows
                  << " doublesPx=" << fmt(semiMajor)
                  << " conicOfDoubles=" << fmt(conicOfDoubles, 4)
                  << " planeBuilt=" << (plane.built ? 1 : 0)
                  << " tilt=" << fmt(plane.tilt, 4)
                  << " anchored=" << (orientation_processing::wedgeCanBeRead(calib.orientation) ? 1 : 0)
                  << " phaseKnown=" << (phaseKnown ? 1 : 0)
                  << " phaseDeg=" << fmt(offset * 180.0 / kPi)
                  << std::endl;

        if (!plane.built)
        {
            std::cout << "I1497NOTE cam=" << (i + 1)
                      << " no board plane, so no cell of the number ring can be placed"
                      << std::endl;
            continue;
        }

        // The annotated whole frame: the twenty cells drawn where this census read them,
        // so a reader can see that the crops came from the ring and not from the wall.
        cv::Mat annotated = frame.clone();

        // The contact sheet: twenty rectified cells in one picture, 5 across.
        const int pad = 6, labelH = 16;
        cv::Mat sheet(4 * (rectH + labelH + pad) + pad, 5 * (rectW + pad) + pad,
                      CV_8UC3, cv::Scalar(30, 30, 30));

        for (int k = 0; k < wire_model::kFold; k++)
        {
            const double centre = offset + (k + 0.5) * wire_model::kSector;
            const double halfSector = wire_model::kSector * 0.5;
            const double rMid = 0.5 * (kRingInner + kRingOuter);

            const cv::Point2f pIn = mapBoard(plane, centre, kRingInner);
            const cv::Point2f pOut = mapBoard(plane, centre, kRingOuter);
            const double radialPx = cv::norm(pOut - pIn);

            const cv::Point2f pL = mapBoard(plane, centre - halfSector, rMid);
            const cv::Point2f pR = mapBoard(plane, centre + halfSector, rMid);
            const double tangentialPx = cv::norm(pR - pL);

            double sMax = 0, sMin = 0;
            jacobianScales(plane, centre, rMid, sMax, sMin);
            const double pxPerMmMax = sMax / kDoublesMm;
            const double pxPerMmMin = sMin / kDoublesMm;
            const double ratio = (pxPerMmMax > 0) ? (pxPerMmMin / pxPerMmMax) : 0.0;
            const double obliquityDeg = std::acos(std::min(1.0, std::max(0.0, ratio))) * 180.0 / kPi;

            // The four corners, for the occlusion question: a cell that runs off the
            // frame's own edge is a number no reader on this camera can ever see.
            const cv::Point2f c00 = mapBoard(plane, centre - halfSector, kRingInner);
            const cv::Point2f c01 = mapBoard(plane, centre - halfSector, kRingOuter);
            const cv::Point2f c10 = mapBoard(plane, centre + halfSector, kRingInner);
            const cv::Point2f c11 = mapBoard(plane, centre + halfSector, kRingOuter);
            const cv::Point2f corners[4] = {c00, c01, c11, c10};
            int inside = 0;
            for (const cv::Point2f &c : corners)
            {
                if (c.x >= 0 && c.x < frame.cols && c.y >= 0 && c.y < frame.rows) inside++;
            }

            // --- the rectified cell ---------------------------------------------------
            // Sampled straight through the plane map: output row v is a board radius,
            // output column u is a board angle. Outer radius at the TOP, because that is
            // the way a board's numbers read.
            cv::Mat mapX(rectH, rectW, CV_32F), mapY(rectH, rectW, CV_32F);
            for (int v = 0; v < rectH; v++)
            {
                const double t = (rectH == 1) ? 0.0 : (double)v / (rectH - 1);
                const double r = kRingOuter + t * (kRingInner - kRingOuter);
                for (int u = 0; u < rectW; u++)
                {
                    const double s = (rectW == 1) ? 0.5 : (double)u / (rectW - 1);
                    const double th = centre - halfSector + s * wire_model::kSector;
                    const cv::Point2f p = mapBoard(plane, th, r);
                    mapX.at<float>(v, u) = p.x;
                    mapY.at<float>(v, u) = p.y;
                }
            }
            cv::Mat rect;
            cv::remap(frame, rect, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                      cv::Scalar(255, 0, 255)); // magenta: outside the frame entirely

            // --- the raw crop, unresampled -------------------------------------------
            float minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
            for (const cv::Point2f &c : corners)
            {
                minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
                minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
            }
            const int x0 = std::max(0, (int)std::floor(minX) - 4);
            const int y0 = std::max(0, (int)std::floor(minY) - 4);
            const int x1 = std::min(frame.cols, (int)std::ceil(maxX) + 4);
            const int y1 = std::min(frame.rows, (int)std::ceil(maxY) + 4);

            char stem[128];
            snprintf(stem, sizeof(stem), "cam%zu_wedge%02d", i + 1, k);
            if (x1 > x0 + 1 && y1 > y0 + 1)
            {
                cv::Mat raw = frame(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();
                cv::Mat big;
                cv::resize(raw, big, cv::Size(), 4, 4, cv::INTER_NEAREST);
                cv::imwrite(outDir + "/raw_" + stem + ".png", big);
            }
            cv::imwrite(outDir + "/rect_" + stem + ".png", rect);

            // --- what the picture says about ink -------------------------------------
            // Description, not a verdict and not a reader: the spread of intensity inside
            // the cell. A cell with no ink in it and a cell with a crisp glyph in it do
            // not have the same one, and a human looking at the crop beside this number
            // can see which they are holding.
            cv::Mat grey;
            cv::cvtColor(rect, grey, cv::COLOR_BGR2GRAY);
            std::vector<uchar> vals(grey.begin<uchar>(), grey.end<uchar>());
            std::sort(vals.begin(), vals.end());
            const double p05 = vals[(size_t)(vals.size() * 0.05)];
            const double p95 = vals[(size_t)(vals.size() * 0.95)];
            cv::Scalar mu, sd;
            cv::meanStdDev(grey, mu, sd);
            cv::Mat lap;
            cv::Laplacian(grey, lap, CV_64F);
            cv::Scalar lmu, lsd;
            cv::meanStdDev(lap, lmu, lsd);

            std::cout << "I1497CELL cam=" << (i + 1)
                      << " wedge=" << k
                      << " centreDeg=" << fmt(centre * 180.0 / kPi)
                      << " radialPx=" << fmt(radialPx)
                      << " tangentialPx=" << fmt(tangentialPx)
                      << " pxPerMmMax=" << fmt(pxPerMmMax, 3)
                      << " pxPerMmMin=" << fmt(pxPerMmMin, 3)
                      << " obliquityDeg=" << fmt(obliquityDeg)
                      << " cornersInFrame=" << inside
                      << " greyMean=" << fmt(mu[0], 1)
                      << " greySd=" << fmt(sd[0], 1)
                      << " greyP05=" << fmt(p05, 0)
                      << " greyP95=" << fmt(p95, 0)
                      << " lapSd=" << fmt(lsd[0], 1)
                      << std::endl;

            // The cell on the whole frame, and its index, so the crops can be placed.
            const cv::Point poly[4] = {c00, c01, c11, c10};
            for (int e = 0; e < 4; e++)
            {
                cv::line(annotated, poly[e], poly[(e + 1) % 4], cv::Scalar(0, 255, 255), 1);
            }
            cv::putText(annotated, std::to_string(k), mapBoard(plane, centre, rMid),
                        cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 0, 255), 1);

            // The sheet.
            const int row = k / 5, col = k % 5;
            const int sx = pad + col * (rectW + pad);
            const int sy = pad + row * (rectH + labelH + pad);
            rect.copyTo(sheet(cv::Rect(sx, sy, rectW, rectH)));
            cv::putText(sheet, "w" + std::to_string(k) + " " + fmt(obliquityDeg, 0) + "deg",
                        cv::Point(sx + 2, sy + rectH + 12), cv::FONT_HERSHEY_SIMPLEX, 0.35,
                        cv::Scalar(220, 220, 220), 1);
        }

        char camstem[64];
        snprintf(camstem, sizeof(camstem), "cam%zu", i + 1);
        cv::imwrite(outDir + "/frame_" + camstem + ".png", annotated);
        cv::imwrite(outDir + "/sheet_" + camstem + ".png", sheet);
        cv::imwrite(outDir + "/averaged_" + camstem + ".png", initial_frames[i]);
    }

    std::cout << "I1497END cameras=" << calibrations.size()
              << " rectW=" << rectW << " rectH=" << rectH
              << " rectPxPerMm=" << fmt(kRectPxPerMm) << std::endl;
    return 0;
}

#include "roi_processing.hpp"
#include "logging.hpp"
#include "../../../utils/math.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

using namespace cv;
using namespace std;

namespace roi_processing
{
    namespace
    {
        /**
         * #1331's falsification, in the shape od_fix and #1339 established: one binary,
         * the region chosen at run time, so "different build" is never a confound.
         *
         * OD_ROI=frame restores exactly what this file drew before ADR-0079 -- a
         * frame-centred ellipse at 80% of the frame, 0.95 wide and 1.1 tall, with no
         * margin -- and is how an off-centre board can be made to lose its calibration
         * again on the same binary that calibrates it.
         */
        bool drawnAroundTheFrame()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_ROI");
                return e && std::string(e) == "frame";
            }();
            return v;
        }

        /**
         * #1378's falsification, in the same shape as OD_ROI above: one binary, the
         * margin chosen at run time, so "different build" is never a confound.
         *
         * OD_ROI_MARGIN=1.25 restores the constant this issue moved, and is how the rig's
         * fitted board can be made to collapse to 72171/72374/72531 px again on the very
         * binary that measures it at 197117/200385/194335. It is also how the plateau in
         * ROIParams was swept: 1.25, 1.40, 1.60, 1.80, 2.00, 2.30, 2.60, 3.00 over both
         * fixtures, one build.
         *
         * A value of zero or less, or anything atof cannot read, is ignored rather than
         * obeyed -- a region of no radius is a black frame and every camera would be
         * refused four stages down for a reason naming the ellipse fitter.
         */
        double marginAsked(double stated)
        {
            static double asked = []
            {
                const char *e = std::getenv("OD_ROI_MARGIN");
                return e ? std::atof(e) : 0.0;
            }();
            return asked > 0.0 ? asked : stated;
        }

        /** The four hand-fitted numbers ADR-0079 §1 retired, kept only for OD_ROI=frame. */
        Mat frameCentredMask(const Mat &frame)
        {
            const float roiSizePercent = 0.8f;
            const float horizontalScale = 0.95f;
            const float verticalScale = 1.1f;
            const float perspectiveMargin = 1.0f;

            Mat mask = Mat::zeros(frame.size(), CV_8UC1);
            int ellipseWidth = int(frame.cols * roiSizePercent * horizontalScale * perspectiveMargin);
            int ellipseHeight = int(frame.rows * roiSizePercent * verticalScale * perspectiveMargin);
            ellipse(mask, math::calculateFrameCenter(frame), Size(ellipseWidth / 2, ellipseHeight / 2),
                    0, 0, 360, Scalar(255), -1);
            return mask;
        }
    }

    double regionRadiusFor(double boardRadius, const ROIParams &params)
    {
        return boardRadius * marginAsked(params.roiRadiusOfBoardRadius);
    }

    Mat processROI(const Mat &frame, const Point &boardCenter, double boardRadius,
                   bool debug_mode, int camera_idx, const ROIParams &params)
    {
        Mat mask;
        if (drawnAroundTheFrame())
        {
            mask = frameCentredMask(frame);
            log_debug("Camera " + log_string(camera_idx + 1) +
                      " region: OD_ROI=frame, so it is the frame-centred ellipse this stage drew "
                      "before ADR-0079 and the board that was found is ignored");
        }
        else
        {
            // A circle, not an ellipse, because what was measured is the smallest circle
            // enclosing a contour, so a circle is the shape that answer comes in and no
            // shape constant has to be chosen to say so. What it encloses is the largest
            // red/green CONTOUR and not necessarily the board -- #1378 -- which is what
            // the margin is sized for and what the check after STEP 6 verifies.
            const int regionRadius = cvRound(regionRadiusFor(boardRadius, params));
            mask = Mat::zeros(frame.size(), CV_8UC1);
            circle(mask, boardCenter, regionRadius, Scalar(255), -1);
            log_debug("Camera " + log_string(camera_idx + 1) + " region: " + log_string(regionRadius) +
                      " px around the board found at (" + log_string(boardCenter.x) + "," +
                      log_string(boardCenter.y) + "), which measured " + log_string((int)boardRadius) +
                      " px across its widest");
        }

        // Apply mask and return cropped result
        Mat roiFrame;
        frame.copyTo(roiFrame, mask);

        // Debug output
        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/roi_processing");
            imwrite("debug_frames/roi_processing/roi_frame_" + to_string(camera_idx) + ".jpg", roiFrame);
        }

        return roiFrame;
    }

} // namespace roi_processing

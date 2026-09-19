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
            // A circle, not an ellipse, because the board was measured by the smallest
            // circle enclosing it: a circle of that radius contains the whole of it by
            // construction, and no shape constant has to be chosen to say so.
            const int regionRadius = cvRound(boardRadius * params.roiRadiusOfBoardRadius);
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

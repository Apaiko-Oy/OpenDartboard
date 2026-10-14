#pragma once
// turnaus#1838: the motion clock a probe that replays footage itself must wind.
//
// The detector's own capture seam (src/utils/capture_opencv.hpp, "#811: the acquisition
// clock") stores each cycle's newest presentation time into od_clock::capture_ms() and
// counts the cycle into od_clock::cycles(). A probe that reads the clips with its own
// cv::VideoCapture and calls processMotion/processDartState directly links none of that,
// so on OD_MOTION_CLOCK=capture or =cycle its motion clock read 0 for the whole replay --
// a cooldown that never expires and a spike window that never closes (#1535's
// capture-clock probe "found no darts" on exactly that). Its only working clock was the
// wall clock, and OD_WINDOW_UNIT=cycles takes only the WINDOWS off it: the cooldown, the
// spike window, the safety timeout and the body-window vote stay milliseconds of
// od_clock::now_ms(). A loaded box runs fewer cycles per millisecond, so those timers end
// on different frames and two replays of one binary publish different darts.
//
// advance() is what the seam does, once per cycle, after the frames are read. It changes
// nothing on the default (wall) clock; under OD_MOTION_CLOCK=capture the timers run on the
// footage's own 33.3 ms frames whatever the load, which is what 1838-replay-noise asserts.
#include <opencv2/opencv.hpp>
#include <vector>

#include "od_clock.hpp"

namespace replay_clock
{
    // The rate the cycle clock (OD_MOTION_CLOCK=cycle) converts counts with: the clips'.
    inline void start(std::vector<cv::VideoCapture> &caps)
    {
        for (cv::VideoCapture &cap : caps)
        {
            const double fps = cap.get(cv::CAP_PROP_FPS);
            if (fps > 0)
            {
                od_clock::frame_period_ms() = 1000.0 / fps;
                return;
            }
        }
    }

    // After a cycle's reads: the newest presentation time any camera carried, monotonically,
    // and one more cycle -- capture_opencv.hpp's two lines.
    inline void advance(std::vector<cv::VideoCapture> &caps, const std::vector<cv::Mat> &images)
    {
        for (size_t i = 0; i < caps.size() && i < images.size(); i++)
        {
            if (images[i].empty())
                continue;
            const long long pos = (long long)caps[i].get(cv::CAP_PROP_POS_MSEC);
            if (pos > od_clock::capture_ms().load())
                od_clock::capture_ms().store(pos);
        }
        od_clock::cycles().fetch_add(1);
    }
}

// #1445: what a camera reads on the frame the board calibrates on, and on the frames it
// would read next.
//
// The issue is one sentence: `mocks/rig-20260918/cam_3`'s AVERAGED calibration frame
// proposes twenty-one wire boundaries and is refused, while several of the single frames
// composing that average read exactly twenty. A retry is only worth building if that is
// true, and its budget is only honest if it is sized against something observed -- #1388's
// rule, and the reason that slice's two constants carry a table above them.
//
// So this program prints, for one clip:
//
//   the AVERAGED frame, composed exactly the way camera::Capture::readAveraged does --
//   thirty consecutive reads accumulated in CV_32F, divided by what the camera really
//   contributed and converted back to CV_8U -- and calibrated with
//   geometry_calibration::calibrateSingleCamera, the same function the detector calls;
//
//   then every single frame after it, one line each, calibrated the same way.
//
// WHY THE AVERAGE CAN BE REPRODUCED AT ALL, and why the load caveat #1442 flagged does not
// survive contact with the capture source: for a FILE source `read()` is
// `cv::VideoCapture::read()` on the next frame in the stream. There is no clock in that
// path and no frame is skipped -- capture_opencv.hpp drops a slot only when read() fails,
// which for a file means the clip ended. So readAveraged(30) over a mock is the mean of
// thirty CONSECUTIVE frames from wherever the stream is positioned, and which thirty is
// decided by the seek and by nothing else. A loaded box cannot change it. That is a claim
// this program exists to check rather than to assert: it is handed the same seek the
// detector uses and must reproduce the detector's own number.
//
// THE SEEK IS NOT A DETAIL. The dev build defines DEBUG_SEEK_VIDEO, which puts camera i at
// `fps * (3 - i*0.18)` seconds before the first read -- so camera 3 of a fixture
// calibrates on a different stretch of its clip than camera 1 does, and a census taken
// from frame 0 is about a picture the board never saw. run_all.sh builds the dev binary
// and every detector tester measures it; this program takes the camera's INDEX and
// reproduces the same arithmetic.
//
//   i1445_look_census <clip> <camera-index> [looks] [spacing] [averaged-frames]
//
//     camera-index      0-based, the slot this clip occupies -- it decides the seek
//     looks             how many single frames to read after the average (default 60)
//     spacing           capture cycles between one look and the next (default 1)
//     averaged-frames   what readAveraged is asked for (default 30)
//
// One line per measurement on stdout, prefixed so it survives the calibration's own
// logging:
//
//   I1445 clip=<name> cam=<n> look=<avg|k> at=<frame> wires=<n> kept=<n> whole=<0|1> sees=<0|1> board=<px>
//
// `look=avg` is the frame the board really calibrates on. `whole` is wire_processing's
// own verdict as this tree spells it, so a tree with #1442's two-sided count in it prints
// a two-sided answer and one without prints a one-sided one -- the program states the
// reading, it does not own the rule.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"

namespace
{
    // capture_opencv.hpp's DEBUG_SEEK_VIDEO arithmetic, copied rather than included
    // because that header drags the whole capture stack in and this program has no
    // cameras. It is one line there and one line here, and the tester asserts the
    // AVERAGED reading against what a real detector run prints, which is what stops the
    // two drifting apart.
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0)
        {
            return 0;
        }
        return (int)(fps * seconds);
    }

    // readAveraged's arithmetic, on one camera. Accumulated in CV_32F and divided by what
    // was really contributed, not by what was asked for.
    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out, int &consumed)
    {
        cv::Mat sum;
        int counted = 0;
        consumed = 0;
        for (int n = 0; n < numFrames; n++)
        {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty())
            {
                break;
            }
            consumed++;
            cv::Mat as_float;
            frame.convertTo(as_float, CV_32F);
            if (counted == 0)
            {
                sum = as_float;
            }
            else
            {
                sum += as_float;
            }
            counted++;
        }
        if (counted == 0)
        {
            return false;
        }
        cv::Mat averaged = sum / (float)counted;
        averaged.convertTo(out, CV_8U);
        return true;
    }

    void say(const std::string &clip, int cam, const std::string &look, int at, const DartboardCalibration &calib)
    {
        std::cout << "I1445 clip=" << clip
                  << " cam=" << cam
                  << " look=" << look
                  << " at=" << at
                  << " wires=" << calib.wires.wiresDetected
                  << " kept=" << (int)calib.wires.wireEndpoints.size()
                  << " whole=" << (calib.wires.isValid ? 1 : 0)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " board=" << (int)(calib.ellipses.hasValidDoubles
                                            ? calib.ellipses.outerDoubleEllipse.size.area()
                                            : 0)
                  << std::endl;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1445_look_census <clip> <camera-index> [looks] [spacing] [averaged-frames]" << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    const int cam = atoi(argv[2]);
    const int looks = argc > 3 ? atoi(argv[3]) : 60;
    const int spacing = argc > 4 ? std::max(1, atoi(argv[4])) : 1;
    const int averaged_frames = argc > 5 ? atoi(argv[5]) : 30;

    std::string name = clip;
    const size_t cut = name.find_last_of('/');
    if (cut != std::string::npos && cut > 0)
    {
        const size_t prev = name.find_last_of('/', cut - 1);
        name = name.substr(prev == std::string::npos ? 0 : prev + 1);
    }

    cv::VideoCapture cap(clip);
    if (!cap.isOpened())
    {
        std::cerr << "I1445 cannot open " << clip << std::endl;
        return 1;
    }

    const double fps = cap.get(cv::CAP_PROP_FPS);
    const int seek = seekFrameFor(cam, fps);
    if (seek > 0)
    {
        cap.set(cv::CAP_PROP_POS_FRAMES, seek);
    }
    std::cout << "I1445 clip=" << name << " cam=" << cam << " fps=" << fps
              << " seek=" << seek << " averaged=" << averaged_frames
              << " looks=" << looks << " spacing=" << spacing << std::endl;

    cv::Mat averaged;
    int consumed = 0;
    if (!averageOf(cap, averaged_frames, averaged, consumed))
    {
        std::cerr << "I1445 no frames to average in " << clip << std::endl;
        return 1;
    }
    int position = seek + consumed;
    say(name, cam, "avg", seek, geometry_calibration::calibrateSingleCamera(averaged, cam, false));

    // The looks, from where the average left the stream -- which is where a board's own
    // next read() would be, because readAveraged leaves the capture wherever it stopped.
    for (int k = 1; k <= looks; k++)
    {
        cv::Mat frame;
        bool have = false;
        for (int s = 0; s < spacing; s++)
        {
            have = cap.read(frame) && !frame.empty();
            if (!have)
            {
                break;
            }
            position++;
        }
        if (!have)
        {
            std::cout << "I1445 clip=" << name << " cam=" << cam << " look=" << k
                      << " at=" << position << " NO-FRAME" << std::endl;
            break;
        }
        say(name, cam, std::to_string(k), position, geometry_calibration::calibrateSingleCamera(frame, cam, false));
    }

    return 0;
}

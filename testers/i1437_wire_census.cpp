// #1437: what the wire stage finds on one frame of one clip, said as a number.
//
// The question this issue is about -- why mocks/rig-20260918/cam_2.mp4 stopped
// calibrating -- is answered by one integer per clip: how many of the twenty wire
// boundaries a board has the ensemble detector returned. `wire_processing` says it
// only at DEBUG, inside a whole detector run that must be started, waited on and
// killed, on a board that never exits when it cannot calibrate (#895). That is three
// minutes and a recorded pid per measurement, and a bisect over nineteen merges wants
// a measurement that costs a second.
//
// So this calls the real calibration -- geometry_calibration::calibrateSingleCamera,
// the same function the detector calls, not a cheaper imitation of it -- on one frame
// read from one clip, and prints what came back. It reads ONLY the fields that have
// been in DartboardCalibration since before this integration branch was cut, because
// it is compiled against each of that branch's merge points in turn and a field one of
// them introduces would make the bisect fail to build rather than fail to calibrate.
//
//   i1437_wire_census <clip> [frame ...]      default frame: 300
//
// One line per frame, on stdout, prefixed so it can be grepped out of the calibration's
// own logging:
//
//   I1437 clip=<name> frame=<n> doubles=<0|1> wires=<n> kept=<n> wires_ok=<0|1> sees=<0|1> board=<px>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include "geometry_calibration.hpp"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1437_wire_census <clip> [frame ...]" << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    std::vector<int> frames;
    for (int i = 2; i < argc; i++)
    {
        frames.push_back(atoi(argv[i]));
    }
    if (frames.empty())
    {
        frames.push_back(300);
    }

    std::string name = clip;
    const size_t cut = name.find_last_of('/');
    if (cut != std::string::npos && cut > 0)
    {
        const size_t prev = name.find_last_of('/', cut - 1);
        name = name.substr(prev == std::string::npos ? 0 : prev + 1);
    }

    std::sort(frames.begin(), frames.end());

    // One pass, decoded forward. CAP_PROP_POS_FRAMES on an h264 file lands on the
    // nearest keyframe, so a seek to frame 300 hands two callers different pictures;
    // reading forward is what the detector does and is the only way two commits'
    // answers are about the same frame.
    cv::VideoCapture cap(clip);
    if (!cap.isOpened())
    {
        std::cerr << "I1437 cannot open " << clip << std::endl;
        return 1;
    }

    cv::Mat frame;
    int index = -1;
    for (size_t f = 0; f < frames.size(); f++)
    {
        while (index < frames[f])
        {
            if (!cap.read(frame) || frame.empty())
            {
                break;
            }
            index++;
        }
        if (index < frames[f] || frame.empty())
        {
            std::cout << "I1437 clip=" << name << " frame=" << frames[f] << " NO-FRAME" << std::endl;
            continue;
        }

        DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, 0, false);

        std::cout << "I1437 clip=" << name
                  << " frame=" << frames[f]
                  << " doubles=" << (calib.ellipses.hasValidDoubles ? 1 : 0)
                  << " wires=" << calib.wires.wiresDetected
                  << " kept=" << (int)calib.wires.wireEndpoints.size()
                  << " wires_ok=" << (calib.wires.isValid ? 1 : 0)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  // #1378 moved the region the wire stage reads inside, and it moved it
                  // to stop the fitted board collapsing. So the fitted board is printed
                  // beside the wire count: the two move in opposite directions under
                  // OD_ROI_MARGIN, and a reader who sees only the wires would read the
                  // old margin as a repair.
                  << " board=" << (int)(calib.ellipses.hasValidDoubles
                                            ? calib.ellipses.outerDoubleEllipse.size.area()
                                            : 0)
                  << std::endl;
    }

    return 0;
}

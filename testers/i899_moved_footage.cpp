// #899: the input that makes a recovered board refuse, built from the footage that makes
// it resume.
//
// The thing that has to be produced is "somebody moved the camera while it was not
// answering", and a rig with three video files for cameras cannot be nudged. What CAN be
// produced is the only thing the detector could ever have noticed about a nudge: the same
// dartboard, in the same room, arriving at the sensor shifted and turned. So this writes
// the mock footage through one affine warp -- a translation in pixels and a rotation in
// degrees about the frame centre -- and the tester puts the result behind the path the
// board will reopen.
//
//   i899_moved_footage <in.mp4> <out.avi> <dx_px> <dy_px> <rotate_deg> <frames> [skip]
//
// `skip` is how many frames to drop before writing, and it is what makes the control
// worth running. A board that reopens a video file rewinds it, so a recovery that reads
// the same file reads the same pictures and every figure in the comparison is 0.00 --
// which proves the plumbing and measures nothing. The tester therefore puts a LATER
// stretch of the same clip behind the path before the recovery: the same rig, the same
// room, a different moment with different darts in the board, which is the closest a file
// gets to "the camera was reopened and nothing had been touched". The numbers that come
// out of that arm are the jitter the tolerances in geometry_agreement.hpp are set above.
//
// A translation of (0,0) and a rotation of 0 is deliberately allowed and is what the
// CONTROL arm of the tester uses: the two arms then differ in exactly one thing, the
// three numbers on this command line, and every other byte of the run -- the same warp,
// the same MJPG re-encode, the same generation loss -- is identical. A control that used
// the original mp4 instead would be comparing a re-encoded picture against a pristine one
// and calling the difference movement.
//
// MJPG into .avi, for #1321's reason: it is the one writer a minimal OpenCV build is
// always configured with, and the detector reads it through the same VideoCapture path as
// the mocks.
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::cerr << "usage: i899_moved_footage <in> <out> <dx_px> <dy_px> <rotate_deg> <frames> [skip]" << std::endl;
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    const double dx = atof(argv[3]);
    const double dy = atof(argv[4]);
    const double rotate_deg = atof(argv[5]);
    const int wanted = atoi(argv[6]);
    const int skip = argc > 7 ? atoi(argv[7]) : 0;

    cv::VideoCapture cap(in);
    if (!cap.isOpened())
    {
        std::cerr << "cannot open " << in << std::endl;
        return 1;
    }
    const int w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0 || fps > 240.0)
    {
        fps = 15.0;
    }

    // Rotation about the frame centre, then the translation, in one matrix.
    cv::Mat warp = cv::getRotationMatrix2D(cv::Point2f(w / 2.0f, h / 2.0f), rotate_deg, 1.0);
    warp.at<double>(0, 2) += dx;
    warp.at<double>(1, 2) += dy;

    cv::VideoWriter writer(out, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, cv::Size(w, h));
    if (!writer.isOpened())
    {
        std::cerr << "cannot write " << out << std::endl;
        return 1;
    }

    cv::Mat frame, moved;
    for (int n = 0; n < skip; n++)
    {
        if (!cap.read(frame) || frame.empty())
        {
            std::cerr << "ran out of frames while skipping " << skip << std::endl;
            return 1;
        }
    }

    int written = 0;
    while (written < wanted && cap.read(frame) && !frame.empty())
    {
        // BORDER_REPLICATE rather than a black border: a camera that has been nudged
        // does not grow a black edge, it sees a little more of the wall. A black band is
        // a contour the calibration would have to argue with, and it would be arguing
        // with the tester rather than with the move.
        cv::warpAffine(frame, moved, warp, cv::Size(w, h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        writer.write(moved);
        written++;
    }
    writer.release();
    std::cout << "wrote " << written << " frames of " << w << "x" << h
              << " from frame " << skip << " shifted (" << dx << "," << dy
              << ") px and turned " << rotate_deg << " deg to " << out << std::endl;
    return written > 0 ? 0 : 1;
}

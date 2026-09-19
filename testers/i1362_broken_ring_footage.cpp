// #1362: the input whose doubles ring is broken while its bull is still measurable.
//
// #1321 built its doubles-stage fixture by DIMMING the mocks, and #1324 measured the
// scale that reached that stage. #1331 reordered the pipeline -- the board is found on
// the full frame and the region drawn around what was found -- and a dim camera now gets
// FURTHER than it did: it traces its ring and falls out at the wire stage instead. The
// ladder was swept again on the new ordering and the doubles stage is not on it any
// more: camera 1 is refused at the bull stage at 0.42 and at the wire stage at 0.45,
// with nothing in between.
//
// That is not a gap in the sweep, it is what dimming does. Taking light away takes it
// from the bull, the triples and the doubles together, so a frame dark enough to break
// the doubles ring has already lost the bull -- and the bull stage is above the doubles
// stage, so it refuses first. What fails AT the doubles stage is the other fault: a
// board whose ring is broken or covered over part of its circumference while the middle
// of the board is lit and whole. That is also the fault a real rig has -- #1340 measured
// one whose ring stops closing wherever the light falls off it -- so the fixture is the
// failure the stage exists for rather than a contrivance that happens to trip it.
//
//   i1362_broken_ring_footage <in.mp4> <out.avi> <frames> <cx> <cy> <rin> <rout> <a0,sweep>...
//
// Each <a0,sweep> is an annular sector in degrees, clockwise from the +x axis as OpenCV
// measures angles, painted over in a flat neutral grey between radius <rin> and <rout>
// of the point (<cx>,<cy>). Grey rather than black on purpose: this is not a dark frame,
// which is #1321's clip and a different failure. It is something in front of the board,
// or a strip of it the light does not reach -- an unsaturated colour, which is what
// color_processing keeps nothing of, on a frame whose exposure is untouched everywhere
// else.
//
// <rin> is the argument that decides WHICH doubles-stage sentence comes out, and it is
// worth knowing which is which. The ray tracer casts 120 rays out of the bull and keeps
// the ones that find a first white pixel and then a last one: an occlusion that starts
// outside the inner rings leaves those rays finding the triples instead and measuring a
// ring of the wrong width, where one that starts close to the bull leaves them finding
// nothing at all. Only the second kind takes a ray out of the count, and the count is
// what the stage refuses on.
//
// Built from mocks/cam_*.mp4 rather than committed, for i1320_speck_footage's and
// i1321_dark_footage's reason: a clip is build output, and the same sectors painted on
// the same source give the same refusal on any checkout. MJPG into .avi for the same
// reason as both of them -- it is the one writer a minimal OpenCV build always has.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 9)
    {
        std::cerr << "usage: i1362_broken_ring_footage <in> <out> <frames> <cx> <cy> <rin> <rout> "
                     "<a0,sweep> [a0,sweep]..."
                  << std::endl;
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    const int wanted = atoi(argv[3]);
    const cv::Point centre(atoi(argv[4]), atoi(argv[5]));
    const int rin = atoi(argv[6]), rout = atoi(argv[7]);

    if (rout <= rin || rin < 0)
    {
        std::cerr << "rin " << rin << " and rout " << rout << " do not make an annulus" << std::endl;
        return 2;
    }

    std::vector<double> starts, sweeps;
    for (int a = 8; a < argc; a++)
    {
        double a0 = 0, sweep = 0;
        if (sscanf(argv[a], "%lf,%lf", &a0, &sweep) != 2)
        {
            std::cerr << "bad sector '" << argv[a] << "', want a0,sweep" << std::endl;
            return 2;
        }
        starts.push_back(a0);
        sweeps.push_back(sweep);
    }

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

    // The occlusion is the same on every frame, so it is drawn once as a mask and
    // stamped: a sector whose edge moved from frame to frame would be a fixture that
    // measures the encoder as well as the detector.
    cv::Mat occluded = cv::Mat::zeros(cv::Size(w, h), CV_8UC1);
    for (size_t s = 0; s < starts.size(); s++)
    {
        cv::ellipse(occluded, centre, cv::Size(rout, rout), 0.0, starts[s], starts[s] + sweeps[s],
                    cv::Scalar(255), -1);
    }
    if (rin > 0)
    {
        cv::circle(occluded, centre, rin, cv::Scalar(0), -1);
    }
    const int covered = cv::countNonZero(occluded);

    cv::VideoWriter writer(out, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, cv::Size(w, h));
    if (!writer.isOpened())
    {
        std::cerr << "cannot write " << out << std::endl;
        return 1;
    }

    cv::Mat frame;
    int written = 0;
    while (written < wanted && cap.read(frame) && !frame.empty())
    {
        frame.setTo(cv::Scalar(96, 96, 96), occluded);
        writer.write(frame);
        written++;
    }
    writer.release();
    std::cout << "wrote " << written << " frames of " << w << "x" << h << ", " << starts.size()
              << " sector(s) of " << rin << "-" << rout << " px around (" << centre.x << ","
              << centre.y << ") greyed out, " << covered << " px covered, to " << out << std::endl;
    return written > 0 ? 0 : 1;
}

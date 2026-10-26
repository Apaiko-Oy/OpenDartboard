// turnaus#1904: open captures the way the detector does -- camera::OpenCvCaptureSource, the one
// class that reads OD_EXPOSURE / OD_GAIN -- and say what came back. Nothing else.
//
//   i1904_capture_probe <source> [<source> ...]
//
// A source is what --cams takes: a device index or path (the pin is set and read back, and the
// per-start line says what each camera reports), or a video file (the pin is a no-op, said
// once). i1904_run.sh drives it; the log lines are the output.

#include <iostream>
#include <string>
#include <vector>

#include "capture_opencv.hpp"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1904_capture_probe <source> [<source> ...]" << std::endl;
        return 2;
    }
    std::vector<std::string> sources(argv + 1, argv + argc);
    camera::OpenCvCaptureSource capture;
    const bool opened = capture.open(sources, 1280, 720, 30);
    std::cout << "I1904 opened=" << (opened ? 1 : 0) << std::endl;
    return opened ? 0 : 1;
}

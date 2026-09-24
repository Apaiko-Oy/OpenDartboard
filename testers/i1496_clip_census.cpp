// #1496: WHY does the clip-wire finder return one on the maintainer's rig?
//
//   i1496_clip_census <outdir> <clip1> [<clip2> ...]
//
// MEASUREMENT ONLY. It changes nothing and asserts nothing; testers/i1496_run.sh holds
// the assertions and the control.
//
// THE CALIBRATION HALF IS #1498'S, WHICH IS #1497'S, WHICH IS #1493'S. Seek where
// DEBUG_SEEK_VIDEO seeks, capture::readAveraged(30), calibrateMultipleCameras. Nothing
// after calibration is replayed: clips do not move, and anchoring happens once.
//
// What is new is that the CLIP-WIRE VERDICT PER WIRE ENDPOINT is captured, out of the
// `static` function that reaches it, through the instrumentation sink #1496 added to
// orientation_processing. #1363 measured "camera 0 found 1 clip wire" and stopped there,
// which cannot tell the issue's four candidates apart. The four are:
//
//   outside the frame   -- the extension leaves the image, so no sample can refuse it.
//                          `offFrame` counts exactly that, and it is #797's fault seen
//                          from the other side: an unrefusable wire is ADMITTED, so this
//                          candidate makes the count too HIGH, never too low.
//   colour or contrast  -- the collision surface is the NUMBER-RING mask thresholded at
//                          brightnessThreshold=100. A dark surround leaves it empty, so
//                          nothing refuses anything and every wire reads as a clip.
//   the wire stage cuts -- `wires` is how many endpoints existed to judge at all. Four
//                          clips cannot be found among three endpoints.
//   a size/shape filter -- there is none in findClipWires. Nothing here can be rejected
//                          for its size or its shape; a wire is refused if and only if
//                          its 200 px extension crosses a bright pixel of that mask.
//
// So the row per wire says which it was, and the frames say it in a picture.
//
// WHAT IT PRINTS
//   I1496CAM   per camera: wires, clips, the orientation verdict, whether it anchored,
//              and how bright the collision mask was.
//   I1496WIRE  per wire endpoint: angle from south, radius, the sample tallies and the
//              verdict findClipWires reached.
//
// WHAT IT WRITES, per camera, into <outdir>/cam<N>/
//   averaged.png    the frame calibration really ran on
//   collision.png   the number-ring mask the extensions are tested against
//   clips.png       every wire endpoint drawn with its extension and its verdict
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "orientation_processing.hpp"

namespace
{
    /** DEBUG_SEEK_VIDEO's own arithmetic (utils/capture_opencv.hpp). */
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0)
        {
            return 0;
        }
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
            if (!cap.read(f) || f.empty())
            {
                break;
            }
            cv::Mat ff;
            f.convertTo(ff, CV_32F);
            if (sum.empty())
            {
                sum = ff;
            }
            else
            {
                sum += ff;
            }
            consumed++;
        }
        if (consumed == 0)
        {
            return false;
        }
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
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1496_clip_census <outdir> <clip> [<clip> ...]\n";
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

    // Arm the sink BEFORE calibration, because calibration is what calls the orientation
    // stage. Nothing is replayed and nothing is reimplemented here: these are the verdicts
    // the real run reached on the real frames.
    std::vector<orientation_processing::ClipWireProbe> probes;
    orientation_processing::clipWireProbeLog = &probes;

    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    orientation_processing::clipWireProbeLog = nullptr;

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const std::string camDir = outDir + "/cam" + std::to_string(i + 1);
        mkdir(camDir.c_str(), 0777);

        // A camera refused by calibration never reaches the orientation stage, so it has
        // no probe row. Said so by name rather than left as a gap: "this camera found no
        // clips" and "this camera was never asked" are different findings.
        const orientation_processing::ClipWireProbe *probe = nullptr;
        for (const auto &p : probes)
        {
            if (p.camera_index == calib.camera_index)
            {
                probe = &p;
            }
        }

        double maskBright = -1.0;
        int maskNonZero = -1;
        if (probe && !probe->collisionMask.empty())
        {
            maskNonZero = cv::countNonZero(probe->collisionMask > 128);
            maskBright = 100.0 * (double)maskNonZero /
                         (double)(probe->collisionMask.rows * probe->collisionMask.cols);
        }

        std::cout << "I1496CAM cam=" << (i + 1)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " asked=" << (probe ? 1 : 0)
                  << " wires=" << (probe ? probe->wireEndpointCount : (int)calib.wires.wireEndpoints.size())
                  << " clips=" << (probe ? probe->clipWireCount : -1)
                  << " star=" << (calib.orientation.isStarCamera ? 1 : 0)
                  << " anchored=" << (calib.orientation.anchored ? 1 : 0)
                  << " pos=" << orientation_processing::cameraPositionToString(calib.orientation.cameraPosition)
                  << " wedge20=" << calib.orientation.wedge20WireIndex
                  << " south=" << calib.orientation.southWireIndex
                  << " numbersRead=" << (calib.orientation.numbersRead ? 1 : 0)
                  << " maskBrightPct=" << fmt(maskBright)
                  << " maskBrightPx=" << maskNonZero
                  << std::endl;

        if (!probe)
        {
            continue;
        }

        for (size_t w = 0; w < probe->wires.size(); w++)
        {
            const auto &v = probe->wires[w];
            // `reason` is the census's whole point: a verdict without one is what #1363
            // left behind. It is derived from the tallies and from nothing else.
            std::string reason;
            if (!v.isClip)
            {
                reason = "hit-mask@" + std::to_string(v.firstHitSample);
            }
            else if (v.samplesInFrame == 0)
            {
                reason = "clip-wholly-offframe";
            }
            else if (v.samplesOffFrame > 0)
            {
                reason = "clip-partly-offframe";
            }
            else
            {
                reason = "clip-mask-empty-here";
            }
            // THE STRIDE. spiderSampleCount=50 over wireExtensionDistance=200 px is one
            // sample every 4 px, and a bright feature thinner than that can be STEPPED
            // OVER. So the same ray is walked again at 1 px and the two answers printed
            // beside each other. Where they differ, the wire was admitted as a clip by
            // the sampling and not by the picture.
            int denseFirstHit = -1;
            if (!probe->collisionMask.empty())
            {
                const cv::Point2f d = v.extendedEnd - v.endpoint;
                const float len = (float)cv::norm(d);
                const int steps = (int)len;
                for (int t = 1; t <= steps; t++)
                {
                    const cv::Point2f sp = v.endpoint + d * ((float)t / (float)steps);
                    const int sx = cvRound(sp.x), sy = cvRound(sp.y);
                    if (sx >= 0 && sx < probe->collisionMask.cols &&
                        sy >= 0 && sy < probe->collisionMask.rows &&
                        probe->collisionMask.at<uchar>(sy, sx) > 128)
                    {
                        denseFirstHit = t;
                        break;
                    }
                }
            }
            std::cout << "I1496WIRE cam=" << (i + 1)
                      << " wire=" << w
                      << " angleFromSouth=" << fmt(v.angleFromSouth)
                      << " radius=" << fmt(v.radius)
                      << " end=" << fmt(v.endpoint.x, 0) << "," << fmt(v.endpoint.y, 0)
                      << " ext=" << fmt(v.extendedEnd.x, 0) << "," << fmt(v.extendedEnd.y, 0)
                      << " inFrame=" << v.samplesInFrame
                      << " offFrame=" << v.samplesOffFrame
                      << " firstHit=" << v.firstHitSample
                      << " isClip=" << (v.isClip ? 1 : 0)
                      << " denseFirstHit=" << denseFirstHit
                      << " denseIsClip=" << (denseFirstHit < 0 ? 1 : 0)
                      << " steppedOver=" << ((v.isClip && denseFirstHit >= 0) ? 1 : 0)
                      << " reason=" << reason
                      << std::endl;
        }

        if (!probe->frame.empty())
        {
            cv::imwrite(camDir + "/averaged.png", probe->frame);
        }
        else
        {
            cv::imwrite(camDir + "/averaged.png", initial_frames[i]);
        }
        if (!probe->collisionMask.empty())
        {
            cv::imwrite(camDir + "/collision.png", probe->collisionMask);
        }

        // The annotated frame. Green = admitted as a clip, red = refused because the
        // extension crossed the number-ring mask, and the extension itself is drawn so a
        // reader can see where it went and whether it left the picture at all.
        cv::Mat ann = probe->frame.empty() ? initial_frames[i].clone() : probe->frame.clone();
        if (!probe->collisionMask.empty())
        {
            // The collision surface, faintly, underneath -- so "the mask was empty here"
            // and "the mask refused it" are told apart by eye and not only by a tally.
            cv::Mat tint;
            cv::cvtColor(probe->collisionMask, tint, cv::COLOR_GRAY2BGR);
            cv::addWeighted(ann, 0.65, tint, 0.35, 0, ann);
        }
        cv::circle(ann, probe->center, 6, cv::Scalar(255, 255, 0), -1);
        for (size_t w = 0; w < probe->wires.size(); w++)
        {
            const auto &v = probe->wires[w];
            const cv::Scalar col = v.isClip ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::line(ann, v.endpoint, v.extendedEnd, col, 2);
            cv::circle(ann, v.endpoint, 5, col, -1);
            cv::putText(ann, std::to_string(w), v.endpoint + cv::Point2f(6, -6),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, col, 1);
        }
        cv::putText(ann,
                    "cam " + std::to_string(i + 1) + "  wires " + std::to_string(probe->wireEndpointCount) +
                        "  clips " + std::to_string(probe->clipWireCount) +
                        "  " + orientation_processing::cameraPositionToString(calib.orientation.cameraPosition) +
                        (calib.orientation.anchored ? "  ANCHORED" : "  unanchored"),
                    cv::Point(12, 28), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        cv::imwrite(camDir + "/clips.png", ann);
    }

    std::cout << "I1496END cameras=" << calibrations.size() << " probed=" << probes.size() << std::endl;
    return 0;
}

// #1388: how long a disturbance during a run keeps the geometry measurement in
// disagreement with the calibration the board holds.
//
// This program exists to produce ONE number that ADR-0080 §1 says the implementing slice
// owns: the retry budget. The ADR's instruction is that the budget be measured rather
// than chosen, and that the event to measure against is a dart struck against the frame
// during a run. So what is measured here is the only observable that event has -- the
// board's own witness measurement disagreeing, and then agreeing again.
//
// WHY A PROGRAM AND NOT A DETECTOR RUN. The detector asks `reviewGeometry` once per
// recovery attempt and only after a sight loss, so a run yields two or three samples
// separated by a backoff. The question here is a shape over time -- how many consecutive
// samples a disturbance spoils -- and that wants the measurement taken densely over
// footage of the event, against a held calibration, by the same two calls the detector
// makes: `geometry_calibration::calibrateSingleCamera` and `geometry_agreement::measure`.
// Every figure this prints is therefore a figure the running board would have printed.
//
// WHAT A SAMPLE IS. `Scorer::attemptRecovery` reads `readAveraged(30)` and hands the mean
// of thirty consecutive frames to the detector, so a sample here is the mean of thirty
// consecutive frames of the clip, taken the same way (convert to CV_32F, sum, divide).
// A sample taken from one frame would be a noisier measurement than the board's own and
// every bit of that extra noise would read as movement.
//
//   i1388_disturbance <clip> <camera-index> <hold-at> <sample-every> <samples>
//                     [knock-from knock-to dx dy]
//
//   hold-at        first frame of the thirty the held calibration is taken from
//   sample-every   frames between the start of one sample and the start of the next
//   samples        how many samples to take after the held one
//   knock-*        OPTIONAL. Frames in [knock-from, knock-to) are translated by
//                  (dx, dy) px before they are averaged: the rig displaced for a bounded
//                  interval and then back where it was bolted, which is a frame strike
//                  with a known duration. Without it the clip is measured as recorded.
//
// It prints one line per sample -- the frame, the second of the clip it starts at, the
// three figures `geometry_agreement::measure` produces and the verdict `hasMoved` would
// return -- and then a summary naming the longest run of consecutive disagreeing samples,
// in samples and in seconds. That run is the thing a retry budget has to outlast.
#include <opencv2/opencv.hpp>

#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "geometry_agreement.hpp"

namespace
{
    /** The mean of `count` consecutive frames starting at `from`, or an empty Mat. */
    cv::Mat averaged(cv::VideoCapture &clip, int from, int count, bool knocked, int knock_from,
                     int knock_to, double dx, double dy)
    {
        cv::Mat sum;
        int taken = 0;
        clip.set(cv::CAP_PROP_POS_FRAMES, from);
        for (int n = 0; n < count; n++)
        {
            cv::Mat frame;
            if (!clip.read(frame) || frame.empty())
            {
                break;
            }
            const int at = from + n;
            if (knocked && at >= knock_from && at < knock_to)
            {
                cv::Mat shifted;
                cv::Mat warp = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
                cv::warpAffine(frame, shifted, warp, frame.size(), cv::INTER_LINEAR,
                               cv::BORDER_REPLICATE);
                frame = shifted;
            }
            cv::Mat as_float;
            frame.convertTo(as_float, CV_32F);
            if (taken == 0)
            {
                sum = as_float;
            }
            else
            {
                sum += as_float;
            }
            taken++;
        }
        if (taken == 0)
        {
            return cv::Mat();
        }
        cv::Mat scaled = sum / taken;
        cv::Mat mean;
        scaled.convertTo(mean, CV_8U);
        return mean;
    }

    std::string twoPlaces(double v)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.2f", v);
        return std::string(buffer);
    }
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::cerr << "usage: i1388_disturbance <clip> <camera-index> <hold-at> <sample-every>"
                  << " <samples> [knock-from knock-to dx dy]" << std::endl;
        return 2;
    }
    const std::string clip_path = argv[1];
    const int camera_index = atoi(argv[2]);
    const int hold_at = atoi(argv[3]);
    const int sample_every = atoi(argv[4]);
    const int samples = atoi(argv[5]);

    const bool knocked = argc >= 10;
    const int knock_from = knocked ? atoi(argv[6]) : 0;
    const int knock_to = knocked ? atoi(argv[7]) : 0;
    const double dx = knocked ? atof(argv[8]) : 0.0;
    const double dy = knocked ? atof(argv[9]) : 0.0;

    // The same thirty the board averages for a witness, and the same thirty its
    // constructor calibrated on.
    const int kSampleFrames = 30;

    cv::VideoCapture clip(clip_path);
    if (!clip.isOpened())
    {
        std::cerr << "cannot open " << clip_path << std::endl;
        return 1;
    }
    double fps = clip.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0 || fps > 240.0)
    {
        fps = 30.0;
    }

    const cv::Mat held_image = averaged(clip, hold_at, kSampleFrames, false, 0, 0, 0, 0);
    if (held_image.empty())
    {
        std::cerr << "no frames at " << hold_at << std::endl;
        return 1;
    }
    const DartboardCalibration held =
        geometry_calibration::calibrateSingleCamera(held_image, camera_index, false);
    std::cout << "CLIP " << clip_path << " camera=" << (camera_index + 1)
              << " fps=" << twoPlaces(fps) << " held_at=" << hold_at
              << " held_sees_board=" << (held.sees_board ? 1 : 0)
              << " held_bull=" << held.bullCenter.x << "," << held.bullCenter.y << std::endl;
    if (!held.sees_board)
    {
        std::cerr << "the held calibration does not see a board; nothing can be compared to it"
                  << std::endl;
        return 1;
    }

    const geometry_agreement::Limits limits;
    int disagreed = 0;
    int unreadable = 0;
    int longest_run = 0;
    int run = 0;
    int longest_run_ends_at = -1;

    for (int s = 1; s <= samples; s++)
    {
        const int from = hold_at + s * sample_every;
        const cv::Mat image = averaged(clip, from, kSampleFrames, knocked, knock_from, knock_to, dx, dy);
        if (image.empty())
        {
            std::cout << "SAMPLE " << s << " frame=" << from << " END OF CLIP" << std::endl;
            break;
        }
        const DartboardCalibration fresh =
            geometry_calibration::calibrateSingleCamera(image, camera_index, false);
        if (!fresh.sees_board)
        {
            // Not agreement and not a measured move: this is the `no_longer_sees` arm of
            // `GeometryDetector::reviewGeometry`, where the camera abstains as a witness.
            // It is counted separately because an abstention neither recovers a board nor
            // faults one, and a budget measured as if it did would be measuring the wrong
            // event.
            unreadable++;
            run = 0;
            std::cout << "SAMPLE " << s << " frame=" << from
                      << " second=" << twoPlaces(from / fps) << " NO BOARD IN THE PICTURE"
                      << std::endl;
            continue;
        }
        const geometry_agreement::Movement movement = geometry_agreement::measure(held, fresh);
        const bool moved = geometry_agreement::hasMoved(movement, limits);
        if (moved)
        {
            disagreed++;
            run++;
            if (run > longest_run)
            {
                longest_run = run;
                longest_run_ends_at = from;
            }
        }
        else
        {
            run = 0;
        }
        std::cout << "SAMPLE " << s << " frame=" << from
                  << " second=" << twoPlaces(from / fps)
                  << " bull_px=" << twoPlaces(movement.bull_shift_px)
                  << " angle_deg=" << (movement.angle_comparable ? twoPlaces(movement.angle_shift_deg) : std::string("-"))
                  << " radius_pct=" << (movement.radius_comparable ? twoPlaces(movement.radius_change * 100.0) : std::string("-"))
                  << " verdict=" << (moved ? "MOVED" : "UNCHANGED") << std::endl;
    }

    const double seconds_per_sample = sample_every / fps;
    std::cout << "SUMMARY clip=" << clip_path << " camera=" << (camera_index + 1)
              << " samples=" << samples
              << " disagreed=" << disagreed
              << " no_board=" << unreadable
              << " longest_disagreeing_run=" << longest_run
              << " samples_of=" << twoPlaces(seconds_per_sample) << "s"
              << " = " << twoPlaces(longest_run * seconds_per_sample) << "s"
              << " ending_at_frame=" << longest_run_ends_at << std::endl;
    return 0;
}

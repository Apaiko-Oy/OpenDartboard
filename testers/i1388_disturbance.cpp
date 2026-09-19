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

#include "board_look.hpp"
#include "wire_processing.hpp"
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

    // #1416: WHICH RING THE STAGE FITTED, asked of the calibration itself.
    //
    // `geometry_agreement::measure` compares the mean radius of one camera's
    // `outerDoubleEllipse` against the same camera's earlier one, and calls the
    // difference a camera that moved along its own axis. That reading is only available
    // while both pictures fitted the SAME physical ring. `mask_processing::processMask`
    // hands the ray trace the largest connected component of the red/green mask, so when
    // the doubles ring survives the colour stage the ray trace fits the doubles ring, and
    // when it does not the largest surviving annulus is the TREBLE ring and the ray trace
    // fits that -- under the same name, with no flag anywhere saying which happened
    // (#1423).
    //
    // Two witnesses are taken, and neither of them is the radius:
    //
    //   the ring's own width, as a fraction of its own outer radius. A doubles ring runs
    //   162 -> 170 mm and a treble 99 -> 107 mm: both 8 mm wide, but 0.047 and 0.075 of
    //   their own outer radius. It is scale-free, it is internal to the doubles fit
    //   alone, and it is the ratio #1423 names first. It is only meaningful when the
    //   inner ellipse was really fitted -- `validInnerPoints > 0`; the stage falls back
    //   to 0.92x the outer ellipse otherwise, which would read 0.080 for every picture
    //   and name the treble every time.
    //
    //   the ring measured against the bull, `outer / innerBullEllipse`. The 50-bull is
    //   carved out of the red by radius and fitted separately, so it is the one length in
    //   this struct that the doubles ray trace did not produce. This is the witness that
    //   separates the two explanations outright, and it does so WITHOUT needing the bull
    //   fit to be accurate: a camera really pushed along its own axis scales every ring
    //   in the picture by one factor, so the RATIO is invariant and only the radius moves;
    //   a ray trace that fitted a different ring moves the ratio by the same factor it
    //   moved the radius by. Equal movement in both means a different ring. Movement in
    //   the radius alone means a camera.
    struct RingEvidence
    {
        double outer = -1.0;          // mean semi-axis of outerDoubleEllipse, px
        double inner = -1.0;          // mean semi-axis of innerDoubleEllipse, px
        double width_fraction = -1.0; // (outer - inner) / outer
        int inner_points = 0;         // 0 means innerDoubleEllipse is the 0.92x fallback
        double bull = -1.0;           // mean semi-axis of innerBullEllipse, px
        double outer_over_bull = -1.0;
        double triple = -1.0; // mean semi-axis of outerTripleEllipse, px
    };

    double meanSemiAxis(const cv::RotatedRect &ellipse)
    {
        if (ellipse.size.width <= 0.0f || ellipse.size.height <= 0.0f)
        {
            return -1.0;
        }
        return ((double)ellipse.size.width + (double)ellipse.size.height) / 4.0;
    }

    RingEvidence ringEvidence(const DartboardCalibration &calibration)
    {
        RingEvidence evidence;
        if (calibration.ellipses.hasValidDoubles)
        {
            evidence.outer = meanSemiAxis(calibration.ellipses.outerDoubleEllipse);
            evidence.inner = meanSemiAxis(calibration.ellipses.innerDoubleEllipse);
            evidence.inner_points = calibration.ellipses.validInnerPoints;
            if (evidence.outer > 0.0 && evidence.inner > 0.0)
            {
                evidence.width_fraction = (evidence.outer - evidence.inner) / evidence.outer;
            }
        }
        evidence.bull = meanSemiAxis(calibration.ellipses.innerBullEllipse);
        evidence.triple = meanSemiAxis(calibration.ellipses.outerTripleEllipse);
        if (evidence.outer > 0.0 && evidence.bull > 0.0)
        {
            evidence.outer_over_bull = evidence.outer / evidence.bull;
        }
        return evidence;
    }

    /**
     * Why a calibration abstained, from whichever stage really refused it.
     *
     * `board_look::refusal` answers for the sight gate and returns an EMPTY string when
     * that gate is content, so printing it alone puts a blank reason beside a camera that
     * was refused somewhere else -- measured on this tree, where
     * mocks/rig-20260918/cam_2.mp4 abstains with a doubles ring cleanly fitted and the
     * sight gate saying nothing. `geometry_calibration` refuses a camera in two places
     * and the second is the wire stage (#1317), which has its own count and its own
     * threshold; a reader given the first reason for the second refusal looks in the
     * wrong half of the pipeline.
     */
    std::string whyItAbstained(const DartboardCalibration &calibration)
    {
        const std::string sight = board_look::refusal(calibration.look);
        if (!sight.empty())
        {
            return sight;
        }
        if (!calibration.wires.isValid)
        {
            return "was refused by the wire stage, not by the sight gate: it found " +
                   std::to_string(calibration.wires.wiresDetected) + " of the " +
                   std::to_string(wire_processing::kWiresRequired) +
                   " wire boundaries a board has";
        }
        return "no stage this instrument can read says why";
    }

    /** The evidence as log fields, with a `-` wherever a length was not produced. */
    std::string ringFields(const RingEvidence &evidence)
    {
        auto orDash = [](double v, int places)
        {
            if (v < 0.0)
            {
                return std::string("-");
            }
            char buffer[64];
            snprintf(buffer, sizeof(buffer), places == 4 ? "%.4f" : "%.2f", v);
            return std::string(buffer);
        };
        return " outer_px=" + orDash(evidence.outer, 2) +
               " inner_px=" + orDash(evidence.inner, 2) +
               " inner_points=" + std::to_string(evidence.inner_points) +
               " width_fraction=" + orDash(evidence.width_fraction, 4) +
               " bull_r_px=" + orDash(evidence.bull, 2) +
               " triple_px=" + orDash(evidence.triple, 2) +
               " outer_over_bull=" + orDash(evidence.outer_over_bull, 2);
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
    const RingEvidence held_ring = ringEvidence(held);
    std::cout << "CLIP " << clip_path << " camera=" << (camera_index + 1)
              << " fps=" << twoPlaces(fps) << " held_at=" << hold_at
              << " held_sees_board=" << (held.sees_board ? 1 : 0)
              << " held_bull=" << held.bullCenter.x << "," << held.bullCenter.y
              << ringFields(held_ring) << std::endl;
    if (!held.sees_board)
    {
        // #1416: with the reason, because the reason is the finding. A held calibration
        // that abstains takes its whole clip out of the census silently otherwise -- the
        // summary line is never printed, and a harness reading `longest_disagreeing_run`
        // out of a missing line reads 0 and calls the clip clean.
        std::cerr << "the held calibration does not see a board; nothing can be compared to it"
                  << std::endl;
        std::cout << "HELD REFUSED clip=" << clip_path << " at=" << hold_at
                  << ringFields(held_ring)
                  << " because: " << whyItAbstained(held) << std::endl;
        return 1;
    }

    const geometry_agreement::Limits limits;
    int disagreed = 0;
    int unreadable = 0;
    // #1416: of the disagreements, how many are a ring the stage renamed rather than a
    // camera that moved. The ratio is invariant under a camera pushed along its own axis
    // and moves with the radius when a different ring was fitted, so the two arms are
    // counted apart instead of being summed into one figure called movement.
    int radius_only = 0;
    int ring_changed = 0;
    int ring_not_measurable = 0;
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
                      << ringFields(ringEvidence(fresh))
                      << " because: " << whyItAbstained(fresh) << std::endl;
            continue;
        }
        const geometry_agreement::Movement movement = geometry_agreement::measure(held, fresh);
        const bool moved = geometry_agreement::hasMoved(movement, limits);
        const RingEvidence fresh_ring = ringEvidence(fresh);

        // The same difference the radius term takes, taken of the RATIO instead.
        double ratio_change = -1.0;
        if (held_ring.outer_over_bull > 0.0 && fresh_ring.outer_over_bull > 0.0)
        {
            ratio_change = std::fabs(fresh_ring.outer_over_bull - held_ring.outer_over_bull) /
                           held_ring.outer_over_bull;
        }

        // A verdict about the ring, and only where the radius term is what produced the
        // disagreement. `kRingMoved` is not a tolerance anything is judged against: it is
        // the reading threshold for a printed account, set an order of magnitude above
        // the ratio's own re-measurement noise and an order below the 0.37/0.59 a ring
        // rename produces, so which side a sample falls on is never a close call.
        const double kRingMoved = 0.05;
        std::string ring_verdict = "-";
        if (moved && movement.radius_comparable && movement.radius_change > limits.max_radius_change)
        {
            if (ratio_change < 0.0)
            {
                ring_verdict = "NOT_MEASURABLE";
                ring_not_measurable++;
            }
            else if (ratio_change > kRingMoved)
            {
                ring_verdict = "DIFFERENT_RING";
                ring_changed++;
            }
            else
            {
                ring_verdict = "SAME_RING";
                radius_only++;
            }
        }

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
                  << " verdict=" << (moved ? "MOVED" : "UNCHANGED")
                  << ringFields(fresh_ring)
                  << " ratio_change_pct=" << (ratio_change < 0.0 ? std::string("-") : twoPlaces(ratio_change * 100.0))
                  << " ring=" << ring_verdict << std::endl;
    }

    const double seconds_per_sample = sample_every / fps;
    std::cout << "SUMMARY clip=" << clip_path << " camera=" << (camera_index + 1)
              << " samples=" << samples
              << " disagreed=" << disagreed
              << " no_board=" << unreadable
              << " longest_disagreeing_run=" << longest_run
              << " samples_of=" << twoPlaces(seconds_per_sample) << "s"
              << " = " << twoPlaces(longest_run * seconds_per_sample) << "s"
              << " ending_at_frame=" << longest_run_ends_at
              << " radius_disagreements_on_a_different_ring=" << ring_changed
              << " radius_disagreements_on_the_same_ring=" << radius_only
              << " radius_disagreements_unmeasurable=" << ring_not_measurable << std::endl;
    return 0;
}

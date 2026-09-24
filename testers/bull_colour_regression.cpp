// Synthetic colour-stage labels, not footage from an unrelated rig.
// Optional image arguments also exercise the complete calibration pipeline.
#include "detector/geometry/calibration/bull_processing.hpp"
#include "detector/geometry/calibration/geometry_calibration.hpp"
#include <iostream>
#include <stdexcept>

using namespace cv;

static void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

static Mat board(bool connected = true)
{
    Mat frame = Mat::zeros(480, 640, CV_8UC3);
    // Grey retained by the colour stage can join the bull to the other markings.
    ellipse(frame, Point(320, 240), Size(200, 120), 0, 0, 360,
            Scalar(80, 80, 80), connected ? FILLED : 6);
    return frame;
}

static void bull(Mat &frame, Point centre, bool red = true, Point innerOffset = Point(), double angle = 0)
{
    ellipse(frame, centre, Size(24, 12), angle, 0, 360, Scalar(0, 255, 0), FILLED);
    ellipse(frame, centre + innerOffset, Size(10, 5), angle, 0, 360,
            red ? Scalar(0, 0, 255) : Scalar(0, 0, 0), FILLED);
}

static bull_processing::BullSighting detect(const Mat &frame)
{
    return bull_processing::processBull(frame, Point(frame.cols / 2, frame.rows / 2));
}

static bool nested(const bull_processing::BullSighting &s)
{
    return s.found && s.basis.find("concentric red centre") != std::string::npos;
}

int main(int argc, char **argv)
{
    try
    {
        for (double angle : {0., 40., 90.})
        {
            Mat frame = board();
            bull(frame, Point(320, 230), true, Point(), angle);
            const auto result = detect(frame);
            require(nested(result) && norm(result.center - Point(320, 230)) <= 1,
                    "connected bull must recover its centre, including rotated ellipses");
            Mat shifted;
            const Mat transform = (Mat_<double>(2, 3) << 1, 0, 45, 0, 1, -25);
            warpAffine(frame, shifted, transform, frame.size(), INTER_NEAREST);
            const auto translated = detect(shifted);
            require(nested(translated) && norm(translated.center - Point(365, 205)) <= 1,
                    "recovery must not depend on frame-centre coordinates");
            Mat smaller;
            resize(frame, smaller, Size(), 0.75, 0.75, INTER_NEAREST);
            const auto scaled = detect(smaller);
            require(nested(scaled) && norm(scaled.center - Point(240, 173)) <= 1.5,
                    "recovery must scale with the board");
        }

        Mat clean = board(false);
        bull(clean, Point(320, 230));
        auto ordinary = detect(clean);
        require(ordinary.found && !nested(ordinary) && norm(ordinary.center - Point(320, 230)) <= 2,
                "ordinary successful bull detection must be preserved");

        Mat distractor = board();
        bull(distractor, Point(320, 230));
        circle(distractor, Point(360, 195), 18, Scalar(), FILLED);
        const auto corrected = detect(distractor);
        require(nested(corrected) && norm(corrected.center - Point(320, 230)) <= 1,
                "a distant round blob must not beat the uniquely nested bull");

        Mat noRed = board();
        bull(noRed, Point(320, 230), false);
        require(!detect(noRed).found, "a green ring with no red centre must not recover");

        Mat offCentre = board();
        bull(offCentre, Point(320, 230), true, Point(9, 0));
        require(!detect(offCentre).found, "an off-centre red mark must not recover");

        Mat ambiguous = board();
        bull(ambiguous, Point(285, 230));
        bull(ambiguous, Point(355, 230));
        require(!detect(ambiguous).found, "two nested candidates must not choose an arbitrary bull");

        Mat tooFar = board();
        bull(tooFar, Point(460, 240));
        require(!detect(tooFar).found, "a nested mark outside the board-relative position gate must fail");

        Mat tiny = board();
        ellipse(tiny, Point(320, 230), Size(6, 4), 0, 0, 360, Scalar(0, 255, 0), FILLED);
        circle(tiny, Point(320, 230), 2, Scalar(0, 0, 255), FILLED);
        require(!detect(tiny).found, "a tiny nested speck must fail the existing size gate");
        require(!detect(board()).found, "a board without a bull must still fail");
        require(!detect(Mat::zeros(480, 640, CV_8UC3)).found, "an empty frame must still fail");
        std::cout << "PASS synthetic colour-bull regression checks\n";

        for (int i = 1; i < argc; ++i)
        {
            const Mat frame = imread(argv[i]);
            require(!frame.empty(), std::string("cannot read fixture: ") + argv[i]);
            auto result = geometry_calibration::calibrateSingleCamera(frame, (i - 1) % 3, false);
            std::cout << "IMAGE " << argv[i] << " calibrated=" << result.sees_board
                      << " bull=" << result.bullCenter << " wires=" << result.wires.wiresDetected
                      << " R=" << result.wires.fit_coherence << std::endl;
            require(result.sees_board && result.wires.wiresDetected >= 20 && result.wires.fit_coherence >= 0.65,
                    std::string("full camera calibration failed: ") + argv[i]);
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}

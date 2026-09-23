#pragma once

#include <opencv2/opencv.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

struct DartboardCalibration;

// #1510. Coordinates: millimetres on the playing surface, origin at the bull,
// +Y through 20, +X towards 6. Angles increase clockwise when viewed from the front.
// A homography is a plane mapping, not a calibrated 3D pose or a lens calibration.
namespace board_model
{
    struct Profile
    {
        std::string id = "standard-steel-tip";
        int version = 1;
        std::array<double, 6> radii{{6.35, 15.9, 99.0, 107.0, 162.0, 170.0}};
        std::array<int, 20> numbers{{20,1,18,4,13,6,10,15,2,17,3,19,7,16,8,11,14,9,12,5}};
        double rimRadius = 225.0; // Blade 6 validation profile: 450 mm overall, NOT scoring diameter.
        double boundaryToleranceMm = 2.0; // Acceptance budget, not a manufacturing tolerance.
        bool valid() const;
        static Profile load(const std::string &path);
    };
    enum class Kind { Ring, Wire, Centre, BandCentre };
    struct Observation
    {
        cv::Point2d image;
        Kind kind = Kind::Ring;
        double target = 0; // ring radius in mm, or wire angle clockwise from +Y
        int ring = -1;
        int sector = -1;
        bool heldOut = false;
        cv::Point2d bandInnerImage, bandOuterImage; // measured colour edges, diagnostics only

    };
    struct Quality
    {
        int training = 0, heldOut = 0, sectors = 0, quadrants = 0;
        int bandCentres = 0, radialWires = 0;
        double colourEdgeP95Mm = std::numeric_limits<double>::infinity();
        std::array<int, 6> ringSupport{};
        std::array<int, 2> bandSupport{};
        double trainRmsMm = std::numeric_limits<double>::infinity();
        double heldOutRmsMm = std::numeric_limits<double>::infinity();
        double heldOutP95Mm = std::numeric_limits<double>::infinity();
        double heldOutRmsPx = std::numeric_limits<double>::infinity();
        double parameterSigmaMm = std::numeric_limits<double>::infinity();
        double orientationErrorDeg = std::numeric_limits<double>::infinity();
    };
    struct Model
    {
        int version = 1;
        Profile profile;
        cv::Matx33d boardToImage = cv::Matx33d::eye();
        cv::Matx33d imageToBoard = cv::Matx33d::eye();
        cv::Size imageSize;
        Quality quality;
        bool valid = false;
        bool anchored = false;
        std::string reason;
        std::string anchorSource;
        std::string cameraIdentity;
        // No measured lens parameters exist yet. Residuals bound the observed ring/wire
        // support only; they are NOT a global bound on distortion between those landmarks.
        std::string distortion = "unmeasured; no global lens-error bound";
    };
    struct Measurement
    {
        Model model;
        std::vector<Observation> observations;
    };
    bool enabled(); // OD_BOARD_MODEL=physical, opt-in until independent/live gates pass.
inline cv::Point2d map(const cv::Matx33d &H,cv::Point2d p)
{
    const cv::Vec3d q=H*cv::Vec3d(p.x,p.y,1);
    if(!std::isfinite(q[2])||std::abs(q[2])<1e-10)
        return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
    return {q[0]/q[2],q[1]/q[2]};
}
    std::vector<Observation> observe(const cv::Mat &colourMask, const Model &seed,
                                    const std::vector<cv::Point2f> &wireCandidates,
                                    cv::Point2d bull, bool hasBull);
    Model fit(const Model &seed, const std::vector<Observation> &observations);
    Measurement measure(const cv::Mat &frame, const DartboardCalibration &initial,
                        const Profile &profile = Profile());
inline std::string score(const Model &m,cv::Point2d image,cv::Point2d *board = nullptr)
{
    if(!m.valid||!m.anchored) return "";
    const auto p=map(m.imageToBoard,image);if(!std::isfinite(p.x)||!std::isfinite(p.y))return "";
    if(board)*board=p;
    const double r=cv::norm(p);const auto &radii=m.profile.radii;
    if(r>radii[5])return "MISS";
    if(r<=radii[0])return "BULL";
    if(r<=radii[1])return "OUTER";
    int slot=((int)std::floor((std::atan2(p.x, p.y)+(CV_PI / 10.0)*0.5)/(CV_PI / 10.0))%20+20)%20;
    const char *ring=r>=radii[4]?"D":(r>=radii[2]&&r<=radii[3]?"T":"S");
    return ring+std::to_string(m.profile.numbers[slot]);
}
    cv::Mat overlay(const cv::Mat &frame, const Measurement &measurement);
    std::string describe(const Model &model);
    double displacementMm(const Model &before, const Model &after);
    std::string fingerprint(const std::vector<Model> &models);
    // Separate schema: never changes the raw-byte DartboardCalibration cache ABI.
    void save(const std::string &path, const Model &model);
    Model load(const std::string &path, const std::string &identity, cv::Size size,
               const Profile &profile);
}

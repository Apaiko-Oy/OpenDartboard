// #1450: the sealed geometry fingerprint records whether the board may be READ for a
// wedge, and not only whether a star was measured.
//
// `geometry_agreement::fingerprint` is a pure function over the calibrations a board
// holds, and `geometryBreach()` is a string comparison of two of its answers. So the
// whole of this issue can be measured without a camera, without footage and without a
// detector process: build two calibrations that differ in exactly one field, and ask
// whether the line the board would seal tells them apart.
//
// THE FIXTURE IS THE SHIPPED ONE. A camera anchored by OD_CAMERA_WEDGES -- the Winmau
// Blade 6 over a black surround, where the clip finder sees one clip and both branches of
// STEP 3 demand four, so no camera anchors itself -- has `anchored` true and
// `isStarCamera` FALSE. That is the configuration #1363 was written for and the one in
// which the two fields disagree, so it is the natural fixture rather than a contrived one.
//
// THE FALSIFIER, ON THIS BINARY. OD_SEAL=star restores the pre-#1450 spelling. Run twice
// -- once plain, once with that word -- the same source measures the defect's presence
// and its absence with no second build to confound it. testers/i1450_seal_check.sh runs
// both and is what run_all.sh reaches under the label `1450-seal`.
//
//   g++ -std=c++17 -I src -I src/utils -o seal_check testers/i1450_seal_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/calibration/geometry_agreement.hpp"

using geometry_agreement::fingerprint;
using orientation_processing::CameraPosition;
using orientation_processing::wedgeCanBeRead;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

/**
 * A camera that measured a whole board, with an orientation nothing has anchored yet.
 * Every field the fingerprint reads is given a value that is not its default, so that a
 * fingerprint which silently dropped a term would be visible rather than accidentally
 * right.
 */
static DartboardCalibration board(int camera_index)
{
    DartboardCalibration calibration;
    calibration.camera_index = camera_index;
    calibration.bullCenter = cv::Point(640, 360);
    calibration.sees_board = true;
    calibration.ellipses.hasValidDoubles = true;
    calibration.ellipses.outerDoubleEllipse =
        cv::RotatedRect(cv::Point2f(640.f, 360.f), cv::Size2f(620.f, 600.f), 0.f);
    calibration.orientation.camera_index = camera_index;
    calibration.orientation.isStarCamera = false;
    calibration.orientation.southWireIndex = 15;
    calibration.orientation.wedge20WireIndex = -1;
    calibration.orientation.angleOffsetFromSouth = 12.5f;
    calibration.orientation.cameraPosition = CameraPosition::UNKNOWN;
    calibration.orientation.wedgeNumber = -1;
    calibration.orientation.anchored = false;
    return calibration;
}

/** The same camera, anchored the way OD_CAMERA_WEDGES anchors one: no star, read anyway. */
static DartboardCalibration anchoredByConfiguration(int camera_index)
{
    DartboardCalibration calibration = board(camera_index);
    calibration.orientation.wedge20WireIndex = 10;
    calibration.orientation.wedgeNumber = 6;
    calibration.orientation.cameraPosition = CameraPosition::CONFIGURED;
    calibration.orientation.anchored = true;
    return calibration;
}

/** What utils/cache.hpp does to a calibration, exactly: sizeof() bytes out and back. */
static DartboardCalibration throughTheCache(const DartboardCalibration &calibration)
{
    std::vector<unsigned char> on_disk(sizeof(DartboardCalibration));
    std::memcpy(on_disk.data(), &calibration, sizeof(DartboardCalibration));
    DartboardCalibration read_back;
    std::memcpy(&read_back, on_disk.data(), sizeof(DartboardCalibration));
    return read_back;
}

int main()
{
    const bool old_spelling = geometry_agreement::sealsOnlyTheStarMeasurement();
    std::cout << (old_spelling ? "MODE old spelling (OD_SEAL=star), the pre-#1450 seal"
                               : "MODE this tree's seal")
              << std::endl;

    // ---- the preconditions, asserted rather than relied on ----------------------------
    //
    // Every measurement below is "these two lines differ", and a difference is evidence
    // of nothing until the pair is known to differ in ONE named field and in no other.
    // #708's rule: the needle is proved to be in the haystack first.
    const DartboardCalibration unanchored = board(1);
    const DartboardCalibration anchored = anchoredByConfiguration(1);

    say(!unanchored.orientation.isStarCamera && !anchored.orientation.isStarCamera,
        "PRECONDITION both fixtures measured no star, so `star=` cannot be what tells them apart");
    say(!wedgeCanBeRead(unanchored.orientation) && wedgeCanBeRead(anchored.orientation),
        "PRECONDITION the scorer reads one of them for a wedge and not the other");
    say(anchored.orientation.anchored && !unanchored.orientation.anchored,
        "PRECONDITION they differ in `anchored`, the field #1363 separated out");
    say(unanchored.bullCenter == anchored.bullCenter &&
            unanchored.camera_index == anchored.camera_index &&
            unanchored.sees_board == anchored.sees_board &&
            unanchored.orientation.angleOffsetFromSouth == anchored.orientation.angleOffsetFromSouth &&
            geometry_agreement::meanRadius(unanchored) == geometry_agreement::meanRadius(anchored),
        "PRECONDITION and in nothing the pre-#1450 seal was already reading");

    // A camera that really is a star camera, to hold `star=` in place: this issue does
    // not claim that the measurement should come out, only that it was not enough.
    DartboardCalibration star = anchoredByConfiguration(1);
    star.orientation.isStarCamera = true;
    star.orientation.cameraPosition = CameraPosition::MIDDLE;
    say(fingerprint({star}) != fingerprint({anchored}),
        "PRECONDITION a camera that stopped being a star camera is still caught, in both spellings");

    // ---- the finding ------------------------------------------------------------------
    const std::string sealed_unanchored = fingerprint({unanchored});
    const std::string sealed_anchored = fingerprint({anchored});
    std::cout << "SEAL unanchored: " << sealed_unanchored << std::endl;
    std::cout << "SEAL anchored:   " << sealed_anchored << std::endl;

    if (old_spelling)
    {
        // The defect, reachable on this binary. `geometryBreach()` is `now == sealed`,
        // so two equal lines are a board reporting that nothing moved.
        say(sealed_unanchored == sealed_anchored,
            "FALSIFIED the pre-#1450 seal cannot tell a readable board from an unreadable one");
        say(sealed_anchored.find("read=") == std::string::npos &&
                sealed_anchored.find("wedge20=") == std::string::npos,
            "FALSIFIED and it says nothing about whether the board may be read");
    }
    else
    {
        say(sealed_unanchored != sealed_anchored,
            "a camera whose `anchored` moved is caught: the two seals differ");
        say(sealed_anchored.find(" read=1") != std::string::npos,
            "the anchored camera seals read=1 -- the scorer will read its wedge");
        say(sealed_unanchored.find(" read=0") != std::string::npos,
            "and the unanchored one seals read=0");
        say(sealed_anchored.find(" star=0") != std::string::npos,
            "with star=0 beside it, which is the disagreement #1363 made possible");

        // The bit is not enough on its own: the index under it is where the wedge count
        // starts, so an index that moved is every dart wrong with `read=1` on both sides.
        DartboardCalibration turned = anchoredByConfiguration(1);
        turned.orientation.wedge20WireIndex = 15;
        say(wedgeCanBeRead(turned.orientation) && wedgeCanBeRead(anchored.orientation),
            "PRECONDITION a shifted 20-wire leaves both cameras readable, so `read=` agrees");
        say(fingerprint({turned}) != fingerprint({anchored}),
            "and the seal still catches it, because `wedge20=` is in the line");
    }

    // ---- `star=` is still there, in both spellings ------------------------------------
    say(sealed_anchored.find(" star=") != std::string::npos,
        "the star measurement is still sealed: #1450 adds a field, it removes none");

    // ---- what a board restarting across this change sees ------------------------------
    //
    // Nothing, and this is the measurement behind that claim rather than an argument for
    // it. The seal is never written down: `sealed_geometry` is a member of
    // GeometryDetector, taken at the end of every initialize() and compared only against
    // a fingerprint built in the same process. The one thing that IS persisted is the
    // calibration record, and the cache copies it raw. So the question a restart really
    // asks is whether a calibration that came off the disk seals the line it would have
    // sealed fresh -- which is also the whole of `--reuse-calibration`'s exposure here.
    say(fingerprint({throughTheCache(anchored)}) == sealed_anchored,
        "a calibration round-tripped through the cache's own bytes seals an identical line");
    say(fingerprint({throughTheCache(unanchored)}) == sealed_unanchored,
        "and so does an unanchored one, so --reuse-calibration seals what it read");
    say(wedgeCanBeRead(throughTheCache(anchored).orientation),
        "`anchored` and the 20-wire survive the cache, so the seal is reading real bytes");

    // ---- the whole board, and the empty one -------------------------------------------
    const std::vector<DartboardCalibration> three = {board(1), anchoredByConfiguration(2), board(3)};
    std::vector<DartboardCalibration> three_moved = three;
    three_moved[2].orientation.anchored = true;
    three_moved[2].orientation.wedge20WireIndex = 10;
    say(wedgeCanBeRead(three_moved[2].orientation) && !wedgeCanBeRead(three[2].orientation),
        "PRECONDITION on a three-camera board, camera 3 alone changes whether it is read");
    say(old_spelling ? fingerprint(three) == fingerprint(three_moved)
                     : fingerprint(three) != fingerprint(three_moved),
        old_spelling ? "FALSIFIED and on a three-camera board the old seal misses it too"
                     : "and one camera of three moving is enough to break the seal");
    say(fingerprint({}) == "no camera holds a calibration",
        "a board holding no calibration still seals the sentence it always did");

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}

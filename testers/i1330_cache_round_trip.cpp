// #1330: the calibration cache, written and read back, and the guarantee that makes that
// legal asked of the whole struct.
//
// The issue's subject is that utils/cache.hpp fwrites sizeof(DartboardCalibration) bytes,
// which is sound only while nothing in the struct owns memory -- and #1321 put a
// std::string one level down inside it, where the three prose warnings already in those
// headers were not looking. A string past its small-string buffer is a pointer into this
// process's heap: written raw, a short reason survives by accident and a long one comes
// back as an address that names nothing.
//
// Three things are asked here, and only the first of them is a compile-time question:
//
//   1. The guarantee itself, said again in a translation unit of its own so that it is
//      asked even by somebody who never builds the detector. The half that cannot be
//      asked from inside a passing build -- that the assert would REFUSE the old shape --
//      is asked by the phase script beside this file, which puts #1321's std::string back
//      into EllipseBoundaryData in a scratch copy of the tree and compiles it.
//
//   2. That a calibration really does survive the round trip, field for field, including
//      a reason long past the small-string buffer travelling the way it now travels --
//      as words returned beside the geometry and printed, never through the file. #1448
//      completed the orientation half of that comparison and says beside it why the other
//      blocks are a sample: `anchored` decides whether the board is READ at all, and it
//      was the one field of OrientationData the fixture never set.
//
//   3. That the file's own header refuses what the struct's guarantee cannot see: a
//      record of a different size, a different number of cameras, a different frame size,
//      and a cached run in which nothing could see the board. Each is fired with a
//      positive control on the same file, because a refusal that cannot be made to fire
//      is not a refusal (#708).
//
//   i1330_cache_round_trip     # one line per check, exits non-zero on any failure

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "geometry_calibration.hpp"
#include "utils.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

// ---------------------------------------------------------------------------------
// 1. The guarantee, in a translation unit that does nothing else.
// ---------------------------------------------------------------------------------
static_assert(std::is_trivially_destructible<DartboardCalibration>::value,
              "#1330: DartboardCalibration must own nothing -- it is fwritten as bytes");
static_assert(std::is_standard_layout<DartboardCalibration>::value,
              "#1330: DartboardCalibration must be standard layout -- it is fwritten as bytes");

/** A calibration of a board whose bull is at (640, 360), as a camera that saw one carries it. */
static DartboardCalibration boardAt(int index, int width, int height, bool sees)
{
    DartboardCalibration calib;
    calib.camera_index = index;
    calib.capture_width = width;
    calib.capture_height = height;
    calib.timestamp = 1758153600ull + (uint64_t)index;
    calib.bullCenter = cv::Point(640 + index, 360 - index);
    calib.frameCenter = cv::Point(width / 2, height / 2);
    calib.sees_board = sees;

    calib.look.frame_pixels = width * height;
    calib.look.red_green_pixels = 27624 + index;
    calib.look.outer_points = 103 - index;
    calib.look.inner_points = 96 - index;
    calib.look.traced_doubles = true;

    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.validOuterPoints = 103 - index;
    calib.ellipses.validInnerPoints = 96 - index;
    calib.ellipses.outerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(400, 398), 1.5f);
    calib.ellipses.innerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(376, 374), 1.5f);
    calib.ellipses.outerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(248, 247), 1.5f);
    calib.ellipses.innerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(224, 223), 1.5f);
    calib.ellipses.outerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(64, 63), 1.5f);
    calib.ellipses.innerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(26, 26), 1.5f);
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.hasDetectedEllipses = true;
    calib.ellipses.offsetX = 1.25;
    calib.ellipses.offsetY = -2.5;
    calib.ellipses.offsetMagnitude = 2.795;
    calib.ellipses.offsetAngle = -63.4;

    for (int i = 0; i < wire_processing::kWiresRequired; i++)
    {
        const double angle = (-CV_PI / 2.0) + (i * 2.0 * CV_PI / wire_processing::kWiresRequired);
        calib.wires.wireEndpoints.add(cv::Point2f(
            640.0f + 200.0f * (float)cos(angle),
            360.0f + 200.0f * (float)sin(angle)));
    }
    calib.wires.wiresDetected = wire_processing::kWiresRequired;
    calib.wires.camera_index = index;
    calib.wires.isValid = true;

    calib.orientation.camera_index = index;
    calib.orientation.isStarCamera = true;
    calib.orientation.orientation = cv::Point2f(0.0f, -1.0f);
    calib.orientation.southWireIndex = 10;
    calib.orientation.wedge20WireIndex = 0;
    calib.orientation.angleOffsetFromSouth = 18.0f;
    calib.orientation.cameraPosition = orientation_processing::CameraPosition::MIDDLE;
    calib.orientation.wedgeNumber = 6;
    calib.orientation.avgClipWireCrossProduct = 0.75f;
    // #1448: ANCHORED, and `true` rather than the default `false` on purpose. A fixture
    // that sets a field to its own default cannot tell a faithful round trip from a zeroed
    // one -- the comparison below would pass on a read that returned nothing at all -- so
    // every field this fixture sets is set away from its default, and this one was the
    // only field of OrientationData that was not set at all.
    //
    // It is worth a line of its own rather than a place in a list because of what it
    // decides. `scorePoint` computes `wedge_measured = anchored && wedge20WireIndex >= 0`,
    // so a calibration that comes back unanchored takes #1346's ASSERTED-twenty path --
    // `sequence_slot = 0`, `dartboard_numbers[0]`, the same number for every tip on any
    // ring -- while reporting itself perfectly valid. That is the state #1447 found a
    // scoring fixture sitting in one layer out. Every other field surviving would not save
    // a board that lost this one.
    calib.orientation.anchored = true;

    return calib;
}

static bool same(const DartboardCalibration &a, const DartboardCalibration &b, std::string &why)
{
    auto differs = [&why](bool bad, const char *field)
    {
        if (bad && why.empty())
        {
            why = field;
        }
        return bad;
    };

    differs(a.camera_index != b.camera_index, "camera_index");
    differs(a.capture_width != b.capture_width, "capture_width");
    differs(a.capture_height != b.capture_height, "capture_height");
    differs(a.timestamp != b.timestamp, "timestamp");
    differs(a.bullCenter != b.bullCenter, "bullCenter");
    differs(a.frameCenter != b.frameCenter, "frameCenter");
    differs(a.sees_board != b.sees_board, "sees_board");
    differs(a.look.red_green_pixels != b.look.red_green_pixels, "look.red_green_pixels");
    differs(a.look.outer_points != b.look.outer_points, "look.outer_points");
    differs(a.look.traced_doubles != b.look.traced_doubles, "look.traced_doubles");
    differs(a.ellipses.hasValidDoubles != b.ellipses.hasValidDoubles, "ellipses.hasValidDoubles");
    differs(a.ellipses.hasDetectedEllipses != b.ellipses.hasDetectedEllipses, "ellipses.hasDetectedEllipses");
    differs(a.ellipses.validOuterPoints != b.ellipses.validOuterPoints, "ellipses.validOuterPoints");
    differs(a.ellipses.offsetMagnitude != b.ellipses.offsetMagnitude, "ellipses.offsetMagnitude");
    differs(a.ellipses.outerDoubleEllipse.center != b.ellipses.outerDoubleEllipse.center, "outerDoubleEllipse.center");
    differs(a.ellipses.outerDoubleEllipse.size != b.ellipses.outerDoubleEllipse.size, "outerDoubleEllipse.size");
    differs(a.ellipses.outerDoubleEllipse.angle != b.ellipses.outerDoubleEllipse.angle, "outerDoubleEllipse.angle");
    differs(a.wires.wiresDetected != b.wires.wiresDetected, "wires.wiresDetected");
    differs(a.wires.isValid != b.wires.isValid, "wires.isValid");
    differs(a.wires.wireEndpoints.size() != b.wires.wireEndpoints.size(), "wireEndpoints.size");
    for (size_t i = 0; i < a.wires.wireEndpoints.size() && i < b.wires.wireEndpoints.size(); i++)
    {
        differs(a.wires.wireEndpoints[i] != b.wires.wireEndpoints[i], "a wire endpoint");
    }
    // #1448: THE ORIENTATION BLOCK IS NAMED WHOLE, AND THE OTHER BLOCKS ARE DELIBERATELY
    // A SAMPLE.
    //
    // This function reads as a statement of what the cache is held to, so the decision has
    // to be legible rather than arrived at. Three of these ten were named before, and the
    // one that was not is the one that decides whether the board is read at all: with
    // `anchored` false `scorePoint` asserts the 20 for every tip on any ring (#1346's path,
    // which is correct, reached by a fixture that should not be reaching it -- #1447).
    // Adding that field alone would have left `wedge20WireIndex` unnamed beside it, and the
    // two are one expression: `wedge_measured = anchored && wedge20WireIndex >= 0`. There
    // is no third thing to say about the rest of the block, so it is all named.
    //
    // The other blocks stay a sample because they are guarded by a different fact. The
    // failure this file exists for is a WHOLE-RECORD one -- a pointer written where words
    // were meant (#1321), a member added or retyped so every field after it is at the wrong
    // offset, a short read -- and any field of a block catches that, which is what choosing
    // a few per block was for. What orientation has and the others do not is a field whose
    // default is silently a different board: a zeroed `anchored` reads as a successful load
    // and scores every dart the same. `look`, `ellipses` and `wires` have no such field --
    // zeroed geometry is refused upstream or is visibly nonsense -- so completing them
    // would buy lines rather than a guarantee.
    differs(a.orientation.camera_index != b.orientation.camera_index, "orientation.camera_index");
    differs(a.orientation.isStarCamera != b.orientation.isStarCamera, "orientation.isStarCamera");
    differs(a.orientation.orientation != b.orientation.orientation, "orientation.orientation");
    differs(a.orientation.southWireIndex != b.orientation.southWireIndex, "orientation.southWireIndex");
    differs(a.orientation.wedge20WireIndex != b.orientation.wedge20WireIndex, "orientation.wedge20WireIndex");
    differs(a.orientation.angleOffsetFromSouth != b.orientation.angleOffsetFromSouth, "orientation.angleOffsetFromSouth");
    differs(a.orientation.cameraPosition != b.orientation.cameraPosition, "orientation.cameraPosition");
    differs(a.orientation.wedgeNumber != b.orientation.wedgeNumber, "orientation.wedgeNumber");
    differs(a.orientation.avgClipWireCrossProduct != b.orientation.avgClipWireCrossProduct,
            "orientation.avgClipWireCrossProduct");
    differs(a.orientation.anchored != b.orientation.anchored, "orientation.anchored");

    return why.empty();
}

/** Frames of the given size, standing in for what the cameras are producing now. */
static std::vector<cv::Mat> framesOf(int count, int width, int height)
{
    std::vector<cv::Mat> frames;
    for (int i = 0; i < count; i++)
    {
        frames.push_back(cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0)));
    }
    return frames;
}

/** Overwrites one uint32_t of the file header in place, to make a real file say something else. */
static bool poke(const std::string &path, long offset, uint32_t value)
{
    FILE *f = fopen(path.c_str(), "r+b");
    if (!f)
    {
        return false;
    }
    fseek(f, offset, SEEK_SET);
    const bool ok = fwrite(&value, sizeof(value), 1, f) == 1;
    fclose(f);
    return ok;
}

int main()
{
    logging::setLogLevel(logging::LogLevel::DEBUG);

    // Said out loud, because the board does not say it: reading the cache is off unless
    // somebody asks, and this file is the somebody. The default is asked for separately
    // below, where it is the first refusal.
    say(!cache::geometry::calibrationMayBeReused(),
        "the cache is NOT read by default -- a start looks at the board unless told not to");
    cache::geometry::allowReuse(true);

    std::cout << "sizeof(DartboardCalibration) = " << sizeof(DartboardCalibration)
              << "  trivially_destructible=" << std::is_trivially_destructible<DartboardCalibration>::value
              << "  standard_layout=" << std::is_standard_layout<DartboardCalibration>::value
              << "  trivially_copyable=" << std::is_trivially_copyable<DartboardCalibration>::value
              << std::endl;

    // The measurement the assert is chosen on, said in the output rather than argued in a
    // comment: is_trivially_copyable is false for a reason that is not ownership at all.
    say(!std::is_trivially_copyable<cv::Point2f>::value,
        "cv::Point2f is not trivially copyable, which is why that trait is not the assertion");
    say(std::is_trivially_destructible<cv::RotatedRect>::value &&
            std::is_trivially_destructible<wire_processing::WireData>::value &&
            std::is_trivially_destructible<board_look::Evidence>::value &&
            std::is_trivially_destructible<orientation_processing::OrientationData>::value &&
            std::is_trivially_destructible<ellipse_processing::EllipseBoundaryData>::value,
        "every member of DartboardCalibration owns nothing, one by one");
    say(!std::is_trivially_destructible<std::string>::value &&
            std::is_standard_layout<std::string>::value,
        "a std::string fails the ownership trait and PASSES standard layout, which is why "
        "standard layout alone was true for the whole life of the bug");

    // -----------------------------------------------------------------------------
    // 2. The round trip.
    // -----------------------------------------------------------------------------
    const std::string path = cache::geometry::generateFilename();
    remove(path.c_str());

    std::vector<DartboardCalibration> written;
    written.push_back(boardAt(0, 1280, 720, true));
    written.push_back(boardAt(1, 1280, 720, true));
    written.push_back(boardAt(2, 1280, 720, false));

    say(cache::geometry::save(written), "three calibrations are written to " + path);

    const std::vector<cv::Mat> frames = framesOf(3, 1280, 720);
    std::vector<DartboardCalibration> read = cache::geometry::load(frames);
    say(read.size() == written.size(), "three calibrations are read back");

    if (read.size() == written.size())
    {
        std::string why;
        bool all = true;
        for (size_t i = 0; i < read.size(); i++)
        {
            all = same(written[i], read[i], why) && all;
        }
        say(all, all ? "every field of every calibration survives the round trip"
                     : "a field did not survive the round trip: " + why);

        // #1448: and what one of those fields MEANS, in the scorer's own arithmetic rather
        // than as a byte. `same()` proves the value came back; this proves the value that
        // came back is one a board can be read on. They are not the same claim and the
        // distance between them is the whole issue: a cache that returned `anchored` false
        // for every camera would load, report valid, skip the eight-and-a-half-second
        // calibration, and then answer `dartboard_numbers[0]` for every dart of the
        // evening. The expression is `scorePoint`'s, copied so that a reader can see it is.
        bool every_camera_would_be_read = true;
        for (size_t i = 0; i < read.size(); i++)
        {
            const bool wedge_measured =
                read[i].orientation.anchored && read[i].orientation.wedge20WireIndex >= 0;
            every_camera_would_be_read = wedge_measured && every_camera_would_be_read;
        }
        say(every_camera_would_be_read,
            "every calibration comes back one the scorer will READ -- anchored, with a "
            "wedge-20 wire -- rather than one it silently asserts the 20 on");
    }

    // The reason string, in the place it now lives: returned beside the geometry, far past
    // the small-string buffer, and never anywhere near the file. This is the value that
    // would have been a heap pointer on disk before this issue.
    {
        ellipse_processing::EllipseReport report;
        report.doublesFailure =
            "the doubles mask holds 341 white pixels and this stage needs at least 1000 "
            "-- a frame this dark keys almost nothing as dartboard red or green, so there "
            "is no ring for the rays cast out from the bull to find an outer edge on";
        say(report.doublesFailure.size() > sizeof(std::string),
            "the reason is " + std::to_string(report.doublesFailure.size()) +
                " characters, well past the small-string buffer of " +
                std::to_string(sizeof(std::string)) + " bytes");
        say(std::is_trivially_destructible<decltype(report.ellipses)>::value &&
                !std::is_trivially_destructible<ellipse_processing::EllipseReport>::value,
            "the report owns memory and the geometry inside it does not: the split is "
            "what the cache is allowed to see");
    }

    // -----------------------------------------------------------------------------
    // 3. The header's refusals, each fired, each with the control beside it.
    // -----------------------------------------------------------------------------
    // Offsets into FileHeader: magic, version, record_bytes, count.
    const long OFF_VERSION = 4;
    const long OFF_RECORD_BYTES = 8;

    say(cache::geometry::load(framesOf(2, 1280, 720)).empty(),
        "a file holding three cameras is refused for a run with two");
    say(cache::geometry::load(framesOf(3, 640, 480)).empty(),
        "a file taken at 1280x720 is refused for cameras now producing 640x480");
    say(!cache::geometry::load(frames).empty(),
        "...and the same file is still accepted for the run it was written for");

    say(poke(path, OFF_RECORD_BYTES, (uint32_t)sizeof(DartboardCalibration) + 32) &&
            cache::geometry::load(frames).empty(),
        "a record 32 bytes longer -- exactly what one more std::string would cost -- is refused");
    say(poke(path, OFF_RECORD_BYTES, (uint32_t)sizeof(DartboardCalibration)) &&
            !cache::geometry::load(frames).empty(),
        "...and putting the true record size back makes the same file readable again");

    say(poke(path, OFF_VERSION, 1u) && cache::geometry::load(frames).empty(),
        "a version 1 file -- one written before this issue, with a pointer in it -- is refused");
    say(poke(path, OFF_VERSION, 2u) && !cache::geometry::load(frames).empty(),
        "...and version 2 reads");

    std::vector<DartboardCalibration> blind;
    blind.push_back(boardAt(0, 1280, 720, false));
    blind.push_back(boardAt(1, 1280, 720, false));
    blind.push_back(boardAt(2, 1280, 720, false));
    say(cache::geometry::save(blind) && cache::geometry::load(frames).empty(),
        "a cached run in which no camera saw the board is refused rather than inherited");

    std::cout << (failures ? "FAILURES: " : "ALL CHECKS PASSED (") << failures
              << (failures ? "" : " failures)") << std::endl;
    return failures ? 1 : 0;
}

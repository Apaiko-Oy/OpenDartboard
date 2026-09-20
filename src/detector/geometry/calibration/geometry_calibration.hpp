#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <type_traits>

#include "ellipse_processing.hpp"
#include "wire_processing.hpp"        // Include full definition for WireData
#include "orientation_processing.hpp" // Include orientation data
#include "board_look.hpp"             // #1318: whether this camera saw a dartboard at all

using namespace std;
using namespace cv;

struct DartboardCalibration
{
    // CORE: Basic calibration data
    Point bullCenter{0, 0};  // Bull center (true dartboard center)
    Point frameCenter{0, 0}; // Frame center
    int camera_index = -1;   // Which camera this is for
    int capture_width = -1;  // Width of the captured frame
    int capture_height = -1; // Height of the captured frame
    uint64_t timestamp = 0;  // Unix timestamp of when this calibration was captured

    // ELLIPSES: Dartboard shape and rings
    ellipse_processing::EllipseBoundaryData ellipses; // Contains all ellipse data

    // WIRE DATA: Contains wire positions for segment alignment
    wire_processing::WireData wires; // Contains wire positions and segment numbers

    // ORIENTATION: Dartboard rotation and "20" segment position
    orientation_processing::OrientationData orientation; // Where is the "20" segment?

    // #1318: SIGHT - was this camera looking at a dartboard? A slot is kept for every
    // camera whether or not it was, because score_processing reads calibrations[i] by
    // the camera's position in the vector and a shorter vector would silently hand one
    // camera another camera's board. `sees_board` is what the rest of the program asks;
    // `look` is the measurement it was decided on, kept so the refusal can be argued
    // with rather than only obeyed. Both are plain data: this struct is written to the
    // calibration cache with a raw fwrite.
    bool sees_board = false;
    board_look::Evidence look;
};

/**
 * #1330: THE WHOLE STRUCT, HELD TO OWNING NOTHING.
 *
 * utils/cache.hpp writes a DartboardCalibration to disk with a raw fwrite of
 * sizeof(DartboardCalibration) bytes and reads one back the same way. That is sound only
 * while every byte of it means the same thing in the process that reads it as in the one
 * that wrote it -- so nothing in here, at any depth, may own memory. A member that owns
 * memory holds a pointer into the heap of the process that made it; written raw, the file
 * gets the pointer, and the next process gets an address that names nothing.
 *
 * Three members' own headers already said so in prose -- WireEndpoints, board_look and
 * this struct's comment above -- and #1321 put a std::string into ellipses anyway, one
 * level down, where no prose was looking. Prose is not a guard. This is:
 *
 *   is_trivially_destructible is the ownership question, asked of the whole struct.
 *   Owning memory means releasing it, releasing it means a destructor, and a member with
 *   a non-trivial destructor makes its container non-trivially destructible all the way
 *   up. So std::string, std::vector, cv::Mat, any smart pointer and anything holding one
 *   of them fail this, at any depth, without anybody remembering to write an assert for
 *   the new member. Measured on this tree: with #1321's std::string in place this reads
 *   false and with it gone it reads true, and every other member reads true on its own.
 *
 *   is_trivially_copyable is deliberately NOT asked, and that is #1317's measurement
 *   rather than a preference: cv::Point_ declares its own copy constructor on the OpenCV
 *   this builds against, so the struct fails that trait today for a reason that has
 *   nothing to do with ownership. An assert nobody can satisfy teaches nothing.
 *
 *   is_standard_layout is asked because the bytes have to be laid out where the reader
 *   expects them. It is not sufficient on its own and is not doing the ownership work:
 *   std::string is itself standard layout, so this trait was TRUE for the whole of the
 *   time the bug existed.
 *
 * The gap, said out loud: a raw `T *` member owns nothing as far as the language is
 * concerned and passes both of these. utils/cache.hpp's second half covers that from the
 * other side -- the file records how many bytes a record is, so a struct whose size moved
 * for any reason is refused by the reader rather than misread. Between them, the shape
 * that gets through is a pointer-sized member that replaced something else pointer-sized,
 * and that is a smaller hole than the one #1321 fell into.
 */
static_assert(std::is_trivially_destructible<DartboardCalibration>::value,
              "DartboardCalibration is written to the calibration cache as raw bytes, so "
              "nothing in it may own memory: a member with a destructor (std::string, "
              "std::vector, cv::Mat, a smart pointer) would put a heap pointer in the file. "
              "Keep the words somewhere else -- see ellipse_processing::EllipseReport.");
static_assert(std::is_standard_layout<DartboardCalibration>::value,
              "DartboardCalibration is written to the calibration cache as raw bytes, so "
              "its members must be laid out where the reader expects to find them.");

// NAMESPACE WITH UTILITY FUNCTIONS
namespace geometry_calibration
{
    /**
     * WHETHER THIS CALL'S REFUSAL IS NEWS (#1457), which is a question only #1445 made
     * askable.
     *
     * Before #1445 a camera was calibrated once per start, so a refusal was said once by
     * construction and nothing had to decide anything. A further look calls this function
     * again on a fresh picture of a camera that has already been refused -- up to
     * `kFurtherLooks` more times -- and every one of those calls reaches the same refusal
     * sites and printed the same ERROR. One dead camera therefore wrote FOURTEEN lines
     * where an operator used to read one: thirteen byte-identical refusals and the
     * summary that says they happened, in the log where noise costs most.
     *
     * The picture is what changed between those calls. The refusal is not.
     *
     * So a caller says which kind of call this is. `News` is the default and is every
     * caller but one -- the refusal has not been said yet, and it is said out loud as one
     * ERROR naming the camera, the stage, and the count against the threshold that
     * refused it (#1321's shape). `ARepeat` is a further look: the same sentences, word
     * for word, are written at DEBUG instead, so `--debug` still holds every word of
     * every look -- a retry nobody can debug is not worth having -- while a log without
     * it holds the refusal once and the exhaustion line that closes it.
     *
     * It changes the LEVEL of those lines and nothing else. What this function measures,
     * what it returns, and the fault it records through `board_sight` are identical
     * either way; a look that finds a board still calibrates the camera, and a look that
     * does not still refuses it.
     */
    enum class RefusalIs
    {
        News,
        ARepeat,
    };

    // Calibrate one camera. Declared since #1318, because the probe that decides which
    // cameras a start with no --cams opens calls it on one candidate at a time -- the
    // real calibration rather than a cheaper imitation of it, so that a camera accepted
    // by the probe is one that has already calibrated.
    DartboardCalibration calibrateSingleCamera(const Mat &frame, int cameraIdx, bool debugMode,
                                               RefusalIs voice = RefusalIs::News);

    // Calibrate multiple cameras at once
    // #1445: the CAMERAS census, said by whoever last changed the answer. It used to be
    // printed inside calibrateMultipleCameras; since a refused camera may still be
    // calibrated by a further look, the first pass is no longer the thing that knows.
    void sayWhichCamerasSeeTheBoard(const vector<DartboardCalibration> &calibrations);

    vector<DartboardCalibration> calibrateMultipleCameras(const vector<Mat> &frames, bool debugMode = false, int targetWidth = 640, int targetHeight = 480);
}
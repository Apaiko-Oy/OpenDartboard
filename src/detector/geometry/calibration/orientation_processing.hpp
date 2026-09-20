#pragma once

#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cv;
using namespace std;

// Forward declaration to avoid circular dependency
struct DartboardCalibration;

namespace orientation_processing
{

    // Configuration for orientation detection methods
    struct OrientationParams
    {
        // Scale to dartboard edge (standard ratio 225mm/170mm ≈ 1.32)
        float boundryScaleFactor = 1.37f;      // Scale factor for dartboard boundary
        float innerBoundryScaleFactor = 1.10f; // Scale factor for inner boundary
        float carvingWireThickness = 75.0f;    // Thickness of wire lines for segment separation

        // Image preprocessing parameters
        int brightnessThreshold = 100; // Threshold for removing dark pixels

        // spider detection parameters
        int wireExtensionDistance = 200; // Distance to extend wires for collision detection
        int spiderSampleCount = 50;      // Number of samples to check along wire extension
    };

    // Camera position enumeration for fixed-size serialization
    enum class CameraPosition : int
    {
        UNKNOWN = 0,
        TOP = 1,
        MIDDLE = 2,
        BOTTOM = 3,
        CONFIGURED = 4 // #1363: the operator stated this camera's south wedge
    };

    // Result structure for orientation detection
    struct OrientationData
    {
        int camera_index = -1;                                   // Which camera this is for
        bool isStarCamera = false;                               // The star-camera MEASUREMENT succeeded (see `anchored`)
        Point2f orientation;                                     // Detected orientation vector (e.g., "20" segment position)
        int southWireIndex = -1;                                 // Index of the south wire for this camera
        int wedge20WireIndex = -1;                               // Index of the "20" segment wire
        float angleOffsetFromSouth = 0.0f;                       // Angle offset from the south
        CameraPosition cameraPosition = CameraPosition::UNKNOWN; // Camera position (enum instead of string)
        int wedgeNumber = -1;                                    // Wedge number (6, 7 or 12; the stated south wedge for CONFIGURED)
        float avgClipWireCrossProduct = 0.0f;                    // Average
        // #1363: whether wedge20WireIndex may be TRUSTED by the scorer -- true for the
        // star camera's measurement and for an operator-configured anchor, never for
        // the TOP/BOTTOM clip-wire guesses #797 measured one-wedge-loose (#1346's
        // recorded caution, unchanged). It exists because "is the star camera" and
        // "has a trustworthy anchor" were one question only by accident of upstream's
        // rig: on a Winmau Blade 6 over a black surround the clip finder sees one clip
        // where both its branches demand exactly four, so no camera could anchor and
        // every dart published as the asserted 20 (the maintainer's first live run).
        // Adding the field moves sizeof(DartboardCalibration), which the cache header's
        // record_bytes refusal turns into one clean recalibration (#1330).
        bool anchored = false;
    };

    // Helper function to convert enum to string for display
    inline string cameraPositionToString(CameraPosition pos)
    {
        switch (pos)
        {
        case CameraPosition::TOP:
            return "TOP";
        case CameraPosition::MIDDLE:
            return "MIDDLE";
        case CameraPosition::BOTTOM:
            return "BOTTOM";
        case CameraPosition::CONFIGURED:
            return "CONFIGURED";
        default:
            return "UNKNOWN";
        }
    }

    // ---- #1363: the configured anchor, pure, inline for the tester (#1338's shape) ----

    // The board's wedge sequence clockwise from the 20 -- the same sequence
    // score_processing scores against. seqIndexOfWedge answers where a wedge stands in
    // it, or -1 for a number no dartboard carries.
    inline int seqIndexOfWedge(int wedge)
    {
        static const int sequence[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};
        for (int i = 0; i < 20; i++)
        {
            if (sequence[i] == wedge)
            {
                return i;
            }
        }
        return -1;
    }

    /**
     * #1363: the wire bounding the 20, from the wedge the camera's image-south points
     * into. This is the star camera's own arithmetic generalised -- upstream hard-coded
     * three cases and each is this formula: MIDDLE looks at the 6 (index 5 in the
     * sequence, south-5), TOP at the 12 (index 18, south-18), BOTTOM at the 7 (index
     * 12, south-12). So a configured wedge rides exactly the math the mocks' star
     * camera has always proven. -1 for a wedge no board carries or wires that make no
     * ring.
     */
    inline int wedge20WireFromSouthWedge(int southWireIndex, int southWedge, int wireCount)
    {
        const int step = seqIndexOfWedge(southWedge);
        if (step < 0 || southWireIndex < 0 || wireCount <= 0)
        {
            return -1;
        }
        int index = (southWireIndex - step) % wireCount;
        if (index < 0)
        {
            index += wireCount;
        }
        return index;
    }

    /**
     * #1363: the operator's statement, parsed. The spec is a comma list with one entry
     * per camera in camera order -- the wedge NUMBER at the bottom of that camera's
     * image, read off the setup view once, 0 (or blank) for a camera the operator does
     * not anchor. "9,0,3" anchors cameras 1 and 3. Answers 0 for no statement, -1 for
     * an entry that is not a number a dartboard carries -- the caller refuses that by
     * name rather than guessing.
     */
    inline int configuredSouthWedge(const string &spec, int camera_index)
    {
        if (spec.empty() || camera_index < 0)
        {
            return 0;
        }
        int field = 0;
        size_t start = 0;
        while (start <= spec.size())
        {
            size_t comma = spec.find(',', start);
            const string entry = spec.substr(start, comma == string::npos ? string::npos : comma - start);
            if (field == camera_index)
            {
                if (entry.empty() || entry == "0")
                {
                    return 0;
                }
                char *end = nullptr;
                const long wedge = strtol(entry.c_str(), &end, 10);
                if (end == entry.c_str() || *end != '\0' || seqIndexOfWedge((int)wedge) < 0)
                {
                    return -1;
                }
                return (int)wedge;
            }
            if (comma == string::npos)
            {
                break;
            }
            field++;
            start = comma + 1;
        }
        return 0;
    }

    // ---- #1449: whether this board can be READ for a wedge, and how each camera reads ----
    //
    // The startup census counted cameras that produced a frame and cameras that see a
    // board, and said nothing about the one field the scorer asks before it reads a wedge
    // at all. So a board on which NO camera can be read was admitted, logged "Initial
    // calibration completed successfully on 3 of 3 cameras", beat READY -- and then gave
    // every dart on every ring the same number, because an unanchored camera takes
    // #1346's asserted-twenty path. That is the maintainer's first live scoring run
    // (#1363), and until #1449 the first thing that said so was a dart that had already
    // been published.
    //
    // This is NOT a refusal and must not become one. An unanchored camera is a legal
    // state: on a Winmau Blade 6 over a black surround the clip finder sees one clip
    // where both branches of STEP 3 demand exactly four, so no camera anchors and a
    // fresh calibration is unanchored too -- which is the whole reason OD_CAMERA_WEDGES
    // exists. Refusing the board would make it unusable on the rig the feature was
    // written for. What was missing is that nobody was TOLD, at the one moment an
    // operator can still act: delete cache/, aim a camera, or state the anchor.

    /**
     * #1449: whether the SCORER will read this camera's wedge.
     *
     * This is `score_processing`'s own expression and there is now one of it. The census
     * that reports anchoring and the scorer that acts on it must not be able to drift
     * apart -- a camera counted as readable here and skipped there is exactly the silence
     * this issue is about, one field further on. `anchored` alone is NOT the question:
     * trust without an index to the 20 reads no wedge either.
     */
    inline bool wedgeCanBeRead(const OrientationData &orientation)
    {
        return orientation.anchored && orientation.wedge20WireIndex >= 0;
    }

    /** How many of these cameras the scorer will read a wedge from. */
    inline int camerasWhoseWedgeCanBeRead(const vector<OrientationData> &orientations)
    {
        int readable = 0;
        for (const OrientationData &orientation : orientations)
        {
            if (wedgeCanBeRead(orientation))
            {
                readable++;
            }
        }
        return readable;
    }

    /**
     * #1389 / ADR-0081 §3: this camera's own reason, never just a count. "A message
     * saying only 'two of three' has told nobody anything."
     *
     * Every branch here is a state STEP 1 to STEP 3 above can really leave a camera in,
     * and each sends the reader somewhere different: a clip-wire guess is a camera the
     * heuristic DID place and #797 measured one wedge loose, so the remedy is to state
     * the anchor; no south wire is a wire stage that found nothing, so the remedy is the
     * aim or the lighting; neither star nor four clips is the Blade 6 itself, where
     * OD_CAMERA_WEDGES is the answer and no amount of re-aiming is.
     */
    inline string howItReads(const OrientationData &orientation)
    {
        if (wedgeCanBeRead(orientation))
        {
            return string(orientation.cameraPosition == CameraPosition::CONFIGURED
                              ? "anchored by configuration"
                              : "anchored by its own star-pattern measurement") +
                   ", wedge " + to_string(orientation.wedgeNumber) +
                   " at its image south, so its wedge is read";
        }
        if (orientation.anchored)
        {
            // Trust with nothing to point at. No path above produces this today -- both
            // set the index and the flag together -- and it is named rather than folded
            // into the branches below so that a future one that sets only the flag is
            // reported as itself instead of as a camera that found no clips.
            return "is anchored but holds no wire index for the 20, so its wedge is asserted";
        }
        if (orientation.cameraPosition == CameraPosition::TOP ||
            orientation.cameraPosition == CameraPosition::BOTTOM)
        {
            return "was placed " + cameraPositionToString(orientation.cameraPosition) +
                   " by the clip-wire heuristic, whose guess #797 measured one wedge loose, so "
                   "its wedge is asserted and not read; state it with OD_CAMERA_WEDGES";
        }
        if (orientation.southWireIndex < 0)
        {
            return "found no south wire to index a wedge from, so its wedge is asserted";
        }
        return "found neither the star pattern nor the four clips the heuristic needs, so its "
               "wedge is asserted; state it with OD_CAMERA_WEDGES";
    }

    /**
     * #1389's shape, in this file's vocabulary: every camera in its own slot, with its own
     * reason, readable or not. Deliberately NOT camera_quorum::namingEachCamera -- that
     * one's positive phrase is "sees the dartboard and can vote", which is a different
     * question about the same camera, and one sentence answering both would be wrong
     * about one of them.
     */
    inline string namingEachCamera(const vector<OrientationData> &orientations)
    {
        string out;
        for (size_t i = 0; i < orientations.size(); i++)
        {
            out += out.empty() ? "" : "; ";
            out += "camera " + to_string(i + 1) + ": " + howItReads(orientations[i]);
        }
        return out;
    }

    // Main processing function
    OrientationData processOrientation(
        const Mat &frame,
        const Mat &colorMask,
        const DartboardCalibration &calib,
        bool enableDebug = false,
        const OrientationParams &params = OrientationParams());

} // namespace orientation_processing

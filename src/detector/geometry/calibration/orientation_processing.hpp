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

    // Main processing function
    OrientationData processOrientation(
        const Mat &frame,
        const Mat &colorMask,
        const DartboardCalibration &calib,
        bool enableDebug = false,
        const OrientationParams &params = OrientationParams());

} // namespace orientation_processing

#pragma once

#include "orientation_processing.hpp"

#include <cstdio>
#include <string>
#include <vector>

/**
 * #1501: ONE SUPPORTED BOARD, AND A BOARD THAT IS NOT IT SAYS SO.
 *
 * The maintainer's decision (2026-09-21, placed 2026-09-24): the supported set is ONE board,
 * the Winmau Blade 6, and anything else is best-effort. What that asks of the detector is
 * not a new instrument -- #1498's number reader already carries a confidence against a
 * permutation null with a measured gap -- but a SENTENCE: the board saying "I do not
 * recognise this board" at default level, in words an operator can act on, with the remedy
 * in the same breath. Before this, the reader's refusal was said once per camera, and the
 * board-level consequence was left for the operator to assemble from three lines.
 *
 * WHAT "RECOGNISED" MEANS HERE, AND WHAT IT DOES NOT. Nothing in this repository can tell a
 * Blade 6 from another board with printed numbers; what it can measure is the two facts
 * that told our two boards apart on #1497's photographs:
 *
 *   - the printed numbers READ on at least one camera (#1498's cut), and
 *   - NO camera found the four clip wires of a wire number ring (`isStarCamera`): the
 *     Blade 6 has no wire number ring at all, so its finder sees one clip, never four.
 *
 * Both true is the shape of the supported board and is said as that and no more. Numbers
 * read beside a wire number ring is the upstream Unicorn mocks' shape: ANOTHER board,
 * scored best-effort, with `isStarCamera` kept beside the reader as #1498 built it. No
 * numbers read on any camera is the unrecognised board -- another board, or a Blade 6 this
 * light cannot read, and nothing here can say which -- and that is the WARNING, naming
 * OD_CAMERA_WEDGES (#1363) and #1486's spread of one stated anchor to the others.
 *
 * ANNOUNCE-ONLY. This reads OrientationData and writes nothing: which wire anchors the 20
 * is decided in orientation_processing and applyConfiguredAnchors exactly as before, so no
 * score can move because of this file. Pure and inline so a tester holds it (#1338's shape).
 */
namespace board_recognition
{
    /** The one supported board, by name. A search for this string finds every Blade 6 fact. */
    inline const char *kSupportedBoard = "Winmau Blade 6";

    enum class Verdict
    {
        NotAsked,        // OD_NUMBER_ANCHOR=off: nothing measured the board, and that is said
        SupportedShape,  // numbers read, no wire number ring: the Blade 6's shape
        AnotherBoard,    // numbers read AND a wire number ring: not a Blade 6, best-effort
        NotRecognised    // no camera read the numbers: another board, or this light
    };

    struct Recognition
    {
        Verdict verdict = Verdict::NotAsked;
        bool warn = false;      // said as a WARNING rather than INFO
        int cameras = 0;        // cameras on the board
        int numbersRead = 0;    // cameras on which #1498's reader cleared its cut
        int wireRing = 0;       // cameras that found the four clips of a wire number ring
        int configured = 0;     // cameras anchored by OD_CAMERA_WEDGES
        int readable = 0;       // cameras whose wedge the scorer will read, by any anchor
        std::string sentence;   // the whole announcement, prefix included
    };

    /** "camera 1 4.28, camera 2 -, camera 3 3.50": what each ring measured, or "-" for none. */
    inline std::string separations(const std::vector<orientation_processing::OrientationData> &o)
    {
        std::string out;
        for (size_t i = 0; i < o.size(); i++)
        {
            char one[48];
            if (o[i].numberSeparation > 0.0f)
            {
                snprintf(one, sizeof(one), "camera %zu %.2f", i + 1, (double)o[i].numberSeparation);
            }
            else
            {
                snprintf(one, sizeof(one), "camera %zu -", i + 1);
            }
            out += (out.empty() ? "" : ", ") + std::string(one);
        }
        return out;
    }

    /**
     * The board-level verdict, said once per start after every anchor is final (after
     * applyConfiguredAnchors on both the fresh and the cached path). `readerAsked` is
     * `!number_anchor::notAsked()` and `cut` is `number_anchor::minimumSeparation()`; both
     * are parameters so a tester can hold this without the reader's environment.
     */
    inline Recognition recognise(const std::vector<orientation_processing::OrientationData> &orientations,
                                 bool readerAsked, double cut)
    {
        Recognition r;
        r.cameras = (int)orientations.size();
        std::string wireCameras;
        for (size_t i = 0; i < orientations.size(); i++)
        {
            const orientation_processing::OrientationData &o = orientations[i];
            if (o.numbersRead)
            {
                r.numbersRead++;
            }
            if (o.isStarCamera)
            {
                r.wireRing++;
                wireCameras += (wireCameras.empty() ? "camera " : ", camera ") + std::to_string(i + 1);
            }
            if (o.cameraPosition == orientation_processing::CameraPosition::CONFIGURED &&
                orientation_processing::wedgeCanBeRead(o))
            {
                r.configured++;
            }
            if (orientation_processing::wedgeCanBeRead(o))
            {
                r.readable++;
            }
        }

        const std::string board(kSupportedBoard);
        const std::string ofAll = " of " + std::to_string(r.cameras) + " cameras";
        char cutText[16];
        snprintf(cutText, sizeof(cutText), "%.2f", cut);

        if (!readerAsked)
        {
            r.verdict = Verdict::NotAsked;
            r.sentence = "BOARD RECOGNITION: not attempted -- the number reader is off "
                         "(OD_NUMBER_ANCHOR=off), so nothing checked this board against the " +
                         board + ", the one supported board";
            return r;
        }

        if (r.numbersRead > 0 && r.wireRing == 0)
        {
            r.verdict = Verdict::SupportedShape;
            r.sentence = "BOARD RECOGNITION: this board has the " + board +
                         "'s shape, the one supported board -- its printed numbers were read on " +
                         std::to_string(r.numbersRead) + ofAll + " (separation " +
                         separations(orientations) + ", cut " + cutText +
                         ") and no camera found a wire number ring, which a " + board +
                         " does not have";
            return r;
        }

        if (r.numbersRead > 0)
        {
            r.verdict = Verdict::AnotherBoard;
            r.sentence = "BOARD RECOGNITION: this is NOT the " + board +
                         ", the one supported board, so it is scored best-effort: its numbers "
                         "were read on " + std::to_string(r.numbersRead) + ofAll + " (separation " +
                         separations(orientations) + ", cut " + cutText + "), but " + wireCameras +
                         " also found the four clip wires of a wire number ring, which a " + board +
                         " does not have. The clip wires stay beside the reader, and a camera on "
                         "which the two disagree is warned about above";
            return r;
        }

        r.verdict = Verdict::NotRecognised;
        std::string what = "BOARD RECOGNITION: this board is NOT recognised. The one supported board "
                           "is the " + board + ", recognised by reading its printed numbers, and no "
                           "camera could read them (separation " + separations(orientations) +
                           ", cut " + cutText + ") -- so this is another board, or a " + board +
                           " this light cannot read, and nothing measured here says where the 20 is";
        if (r.configured > 0)
        {
            // The remedy is already in effect: said, but not as a warning, so the one line
            // that matters is not the line an operator learns to skip.
            r.warn = false;
            r.sentence = what + ". OD_CAMERA_WEDGES states the anchor on " +
                         std::to_string(r.configured) + ofAll +
                         ", which is the remedy; the others are derived from darts every camera "
                         "sees (#1486)";
            return r;
        }
        r.warn = true;
        if (r.wireRing > 0)
        {
            what += "; " + wireCameras + " found the four clip wires of a wire number ring, "
                    "which a " + board + " does not have, and anchors by them";
        }
        r.sentence = what + ". REMEDY: set OD_CAMERA_WEDGES to the wedge number at each camera's "
                            "image south, read off the setup view once (\"9,0,3\" states cameras 1 "
                            "and 3); one camera is enough, because the others are derived from darts "
                            "every camera sees (#1486)";
        return r;
    }
}

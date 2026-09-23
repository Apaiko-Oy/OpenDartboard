#include "score_processing.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "utils/streamer.hpp"
#include "../calibration/geometry_calibration.hpp"
#include "../calibration/perspective_processing.hpp"
#include "../calibration/board_model.hpp"
#include "../calibration/wire_processing.hpp"

using namespace cv;
using namespace std;

namespace score_processing
{

    static bool initialized = false;
    static unique_ptr<streamer> point_on_screen_streamer;

    // Helper: Check if point is inside ellipse (pure math)
    bool isPointInEllipse(Point2f point, const RotatedRect &ellipse)
    {
        Point2f center = ellipse.center;
        Point2f relative = point - center;

        // Rotate point to ellipse coordinate system
        float angle_rad = -ellipse.angle * CV_PI / 180.0f;
        float cos_a = cos(angle_rad);
        float sin_a = sin(angle_rad);

        Point2f rotated(
            relative.x * cos_a - relative.y * sin_a,
            relative.x * sin_a + relative.y * cos_a);

        // Ellipse equation: (x/a)² + (y/b)² <= 1
        float a = ellipse.size.width / 2.0f;
        float b = ellipse.size.height / 2.0f;

        // #1485: a ring that was not fitted, or that was refused by the band check in
        // ellipse_processing, is a zeroed RotatedRect and contains nothing. It already
        // read that way -- the division makes an infinity or a NaN and every comparison
        // with one is false -- and saying it costs a branch and removes the reader's
        // need to work that out.
        if (!(a > 0.0f) || !(b > 0.0f))
        {
            return false;
        }

        return (rotated.x * rotated.x) / (a * a) + (rotated.y * rotated.y) / (b * b) <= 1.0f;
    }

    // #1186: distance along a ray from `origin` in direction `direction` (unit) to the
    // boundary of `ellipse`, or a negative number where the ray never crosses it. The
    // helper in utils/math.hpp answers a made-up radius in that case; a ruler mark that
    // was not found has to be absent, not invented.
    static float rayDistanceToEllipse(Point2f origin, Point2f direction, const RotatedRect &ellipse)
    {
        float a = ellipse.size.width / 2.0f;
        float b = ellipse.size.height / 2.0f;
        if (!(a > 0.0f) || !(b > 0.0f))
            return -1.0f;
        float theta = -ellipse.angle * CV_PI / 180.0f;
        float c = cos(theta), sn = sin(theta);
        Point2f rel = origin - ellipse.center;
        float x0 = rel.x * c - rel.y * sn;
        float y0 = rel.x * sn + rel.y * c;
        float dx = direction.x * c - direction.y * sn;
        float dy = direction.x * sn + direction.y * c;
        float A = (dx * dx) / (a * a) + (dy * dy) / (b * b);
        float B = 2.0f * ((x0 * dx) / (a * a) + (y0 * dy) / (b * b));
        float C = (x0 * x0) / (a * a) + (y0 * y0) / (b * b) - 1.0f;
        float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f || !(A > 0.0f))
            return -1.0f;
        float t = (-B + sqrt(disc)) / (2.0f * A); // the crossing ahead of an origin inside the ellipse
        return t > 0.0f ? t : -1.0f;
    }

    // #1186: the radial ruler. The six ring ellipses cross the ray from the bull through
    // the tip at known distances, and the board's own radii for those rings are known in
    // millimetres, so the tip's pixel distance is read off between the two marks it lies
    // between. Returns the radius normalised to the outer edge of the double ring, or a
    // negative number where the ruler has no outer mark. Membership in a ring by the
    // ellipse test above and position on this ruler agree by construction: a point on the
    // ray is inside a convex ellipse that contains the bull exactly when it is short of
    // the crossing.
    static float boardRadius(Point2f pixel, const DartboardCalibration &calib)
    {
        Point2f center = Point2f(calib.bullCenter);
        Point2f rel = pixel - center;
        float d = sqrt(rel.x * rel.x + rel.y * rel.y);
        if (d <= 0.0f)
            return 0.0f;
        Point2f u = rel * (1.0f / d);
        const perspective_processing::DartboardSpec spec;
        struct Mark
        {
            const RotatedRect *ellipse;
            float mm;
        };
        const Mark marks[6] = {
            {&calib.ellipses.innerBullEllipse, spec.bullRadius},
            {&calib.ellipses.outerBullEllipse, spec.bull25Radius},
            {&calib.ellipses.innerTripleEllipse, spec.innerTripleRadius},
            {&calib.ellipses.outerTripleEllipse, spec.outerTripleRadius},
            {&calib.ellipses.innerDoubleEllipse, spec.innerDoubleRadius},
            {&calib.ellipses.outerDoubleEllipse, spec.outerDoubleRadius},
        };
        // Usable marks, in increasing pixel distance; a mark that was not crossed or that
        // sits inside the previous one is dropped rather than bent into place.
        vector<pair<float, float>> ruler; // (pixels, mm)
        ruler.push_back({0.0f, 0.0f});
        for (const Mark &m : marks)
        {
            float t = rayDistanceToEllipse(center, u, *m.ellipse);
            if (t > ruler.back().first)
                ruler.push_back({t, m.mm});
        }
        if (ruler.back().second != spec.outerDoubleRadius)
            return -1.0f; // no outer mark, no ruler
        size_t k = 1;
        while (k + 1 < ruler.size() && d > ruler[k].first)
            k++;
        float p0 = ruler[k - 1].first, p1 = ruler[k].first;
        float m0 = ruler[k - 1].second, m1 = ruler[k].second;
        float mm = m0 + (d - p0) * (m1 - m0) / (p1 - p0); // extrapolates past the last mark
        return mm / spec.outerDoubleRadius;
    }

    // #1186: the angular ruler. Upstream's wedge loop, kept as it was - the first wire
    // pair whose span contains the tip's image angle, scanning from `start` - and made to
    // say where between the two wires the tip is. Returns the slot (0..19 from `start`)
    // or -1, and writes the fraction across the wedge from its first wire.
    static int findWedgeSlot(Point2f pixel, const DartboardCalibration &calib, int start, float &fraction)
    {
        Point2f center = Point2f(calib.bullCenter);
        Point2f direction = pixel - center;
        float point_angle = atan2(direction.y, direction.x);
        if (point_angle < 0)
            point_angle += 2 * CV_PI; // Normalize to 0-2π

        log_debug("SCORE: Point angle = " + log_string(point_angle * 180.0f / CV_PI) + " degrees");

        const int wires = (int)calib.wires.wireEndpoints.size();
        for (int i = 0; i < 20; i++)
        {
            int wire1_index = (start + i) % wires;
            int wire2_index = (start + i + 1) % wires;

            Point2f wire1 = calib.wires.wireEndpoints[wire1_index];
            Point2f wire2 = calib.wires.wireEndpoints[wire2_index];

            // Calculate angles for both wire boundaries
            Point2f dir1 = wire1 - center;
            Point2f dir2 = wire2 - center;
            float angle1 = atan2(dir1.y, dir1.x);
            float angle2 = atan2(dir2.y, dir2.x);

            if (angle1 < 0)
                angle1 += 2 * CV_PI;
            if (angle2 < 0)
                angle2 += 2 * CV_PI;

            // Ensure angle1 < angle2 (handle wraparound)
            if (angle2 < angle1)
                angle2 += 2 * CV_PI;

            // Check if point angle is between the two wire angles
            float test_angle = point_angle;
            if (test_angle < angle1)
                test_angle += 2 * CV_PI;

            if (test_angle >= angle1 && test_angle <= angle2)
            {
                float span = angle2 - angle1;
                fraction = span > 0.0f ? (test_angle - angle1) / span : 0.0f;
                log_debug("SCORE: Found wedge slot " + log_string(i) +
                          " (angle1=" + log_string(angle1 * 180.0f / CV_PI) +
                          ", angle2=" + log_string(angle2 * 180.0f / CV_PI) +
                          ", fraction=" + log_string(fraction) + ")");
                return i;
            }
        }
        return -1;
    }

    // Standard dartboard sequence starting from 20, clockwise
    static const vector<int> dartboard_numbers = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

    // Clean, angle-based scoring function. #1186: the same decision upstream made, stated
    // as fields; `score` is composed from them and is byte-for-byte what it was.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib,
                          const orientation_processing::DerivedAnchor &derived)
    {
        PointScore out;

        // Validation check. #1317: the second half of this could not fire -- it asked
        // std::array<Point2f, 20>::size(), which is 20 on every calibration ever made,
        // including a blank one. It is the guard on the SCORING path, and it is the last
        // thing between a partial detection and a dart being given a number: findWedgeSlot
        // below walks `(start + i) % wires` around the ring, so with nineteen wires every
        // wedge past the gap is the wrong segment, and with none it divides by zero.
        // A camera with no frame is exactly that case -- calibrateMultipleCameras keeps a
        // blank slot for it on purpose (#1318) and scoreDarts reads calibrations[i] for
        // every camera -- so this now refuses on a real calibration in a real run.
        //
        // #1442: and the half of it that STILL could not fire is repaired the same way
        // perspective_processing's twin is. `wireEndpoints.size() < kWiresRequired` reads
        // a store bounded at kWiresRequired, so it catches a short ring and can never
        // catch a long one; a camera proposing twenty-two filled that store to twenty and
        // arrived here indistinguishable from a clean board. And it is exactly this
        // function that the difference matters in: findWedgeSlot below walks the ring by
        // ORDINAL and indexes dartboard_numbers with it, so the twenty kept out of
        // twenty-two -- the twenty smallest angles, the last two dropped, a double-width
        // gap left behind -- give every wedge past that gap a different number, reported
        // with the confidence of a whole ring. That is the same fault the sentence above
        // describes for nineteen wires, reached from the other side.
        //
        // #1451: the same expression, with one copy of it, so that the startup census can
        // ask THIS question rather than a weaker one that happened to agree. It asked
        // `sees_board && hasValidDoubles` and never the ring, so a camera refused here was
        // counted a full voter at start and then abstained from every dart in silence --
        // this refusal is `log_debug`, below the default level.
        if (!canScoreAPoint(calib))
        {
            log_debug("SCORE: Invalid calibration data: camera " + log_string(calib.camera_index + 1) +
                      " has " + log_string(calib.wires.wiresDetected) + " wire boundaries where scoring needs the " +
                      log_string(wire_processing::kWiresRequired) + " a board has" +
                      string(calib.ellipses.hasValidDoubles ? "" : ", and no fitted doubles ring"));
            return out;
        }

        // #1517: a ring refused by the band check is a zeroed ellipse (#1485), so it is
        // the ellipses that are asked and not the fitting flags -- the flags are
        // recomputed after the hold, but the ellipses are what scoring below consults,
        // so asking them cannot drift from what this camera will actually do. The four
        // asked are the ones that tell a single from a treble or a double, which is the
        // measured failure the vote's preference exists for (#1485's zeroed treble ring
        // on the rig). The bull ellipses are deliberately not in it: a zeroed bull
        // degrades a different reading, and widening the field is a decision to take
        // on its own measurement rather than in passing.
        out.rings_complete = calib.ellipses.innerTripleEllipse.size.area() > 0 &&
                             calib.ellipses.outerTripleEllipse.size.area() > 0 &&
                             calib.ellipses.innerDoubleEllipse.size.area() > 0 &&
                             calib.ellipses.outerDoubleEllipse.size.area() > 0;

        // 1. RING DETECTION - Check from inside out
        bool in_inner_bull = isPointInEllipse(pixel, calib.ellipses.innerBullEllipse);
        bool in_outer_bull = !in_inner_bull && isPointInEllipse(pixel, calib.ellipses.outerBullEllipse);
        bool on_bull = in_inner_bull || in_outer_bull;

        string ring_prefix;
        if (in_inner_bull)
        {
            log_debug("SCORE: Point in INNER BULL");
            out.score = "BULL";
            out.ring = "bull";
        }
        else if (in_outer_bull)
        {
            log_debug("SCORE: Point in OUTER BULL");
            out.score = "OUTER";
            out.ring = "outer";
        }
        else
        {
            bool in_inner_triple = isPointInEllipse(pixel, calib.ellipses.innerTripleEllipse);
            bool in_outer_triple = isPointInEllipse(pixel, calib.ellipses.outerTripleEllipse);
            bool in_inner_double = isPointInEllipse(pixel, calib.ellipses.innerDoubleEllipse);
            bool in_outer_double = isPointInEllipse(pixel, calib.ellipses.outerDoubleEllipse);

            log_debug("SCORE: Ellipse tests - Inner_T:" + log_string(in_inner_triple) +
                      " Outer_T:" + log_string(in_outer_triple) +
                      " Inner_D:" + log_string(in_inner_double) +
                      " Outer_D:" + log_string(in_outer_double));

            // Determine ring type with clear logic
            if (in_outer_double && !in_inner_double)
            {
                ring_prefix = "D"; // In the double ring (narrow band)
                out.ring = "double";
                log_debug("SCORE: Ring type = DOUBLE");
            }
            else if (in_outer_triple && !in_inner_triple)
            {
                ring_prefix = "T"; // In the triple ring (narrow band)
                out.ring = "triple";
                log_debug("SCORE: Ring type = TRIPLE");
            }
            else if (in_outer_double)
            {
                ring_prefix = "S"; // Anywhere else inside the dartboard
                out.ring = "single";
                log_debug("SCORE: Ring type = SINGLE");
            }
            else
            {
                // #1505: WHERE outside is measured, though it decides nothing on an
                // ordinary run. Within the physical rim (the tip mask's own region,
                // #1364) the MISS is a measurement of a dart on the surround --
                // `aVoteIsCast` records why letting it vote was measured and refused,
                // and OD_SURROUND=votes is the pin that re-measures it. The ruler's
                // radius is kept on the surround reading (it extrapolates past the
                // outer mark), so a published MISS's BOARD line can say how far off
                // the board the tip was.
                RotatedRect rim = calib.ellipses.outerDoubleEllipse;
                rim.size.width *= kPhysicalRimOverBoard;
                rim.size.height *= kPhysicalRimOverBoard;
                if (isPointInEllipse(pixel, rim))
                {
                    out.on_surround = true;
                    float radius = boardRadius(pixel, calib);
                    if (radius >= 0.0f)
                    {
                        out.board.has_radius = true;
                        out.board.radius = radius;
                    }
                    log_debug("SCORE: Point outside dartboard, on the surround (radius " +
                              log_string(out.board.radius) + " of the board)");
                }
                else
                {
                    log_debug("SCORE: Point outside dartboard");
                }
                return out; // MISS; on_surround says whether it was measured there
            }
        }

        // The radial ruler answers for every ring, the bull included.
        float radius = boardRadius(pixel, calib);
        if (radius >= 0.0f)
        {
            out.board.has_radius = true;
            out.board.radius = radius;
        }

        // 2. WEDGE DETECTION using angles
        //
        // #1346 made this gate a DECISION where it read as an accident, and #1363 gave
        // the decision its own word: `anchored` is true for the star camera's
        // MEASUREMENT and for an operator-CONFIGURED anchor (OD_CAMERA_WEDGES, the
        // fixed-rig statement a Blade 6 over a black surround needs, because its clip
        // finder sees one clip where the heuristic demands four). It stays false for
        // the TOP/BOTTOM clip-wire guesses: #797 measured them agreeing with the star
        // camera on four wedges of seven, one wedge loose either way, and measured that
        // widening the voter set makes the published score WORSE while disagreements
        // fall back to the lowest index. Until that vote question is settled, an
        // unanchored camera's wedge is an assertion the vote keeps aside (chooseScore),
        // never a second voter.
        // #1449: the same expression this line always was, with one copy of it. The
        // startup census asks `wedgeCanBeRead` too, so a camera reported readable at
        // start is by construction a camera read here -- the two cannot drift, which is
        // the defect #1449 was filed about one field earlier. NOT a behaviour change:
        // `wedgeCanBeRead` is `anchored && wedge20WireIndex >= 0` and nothing else.
        //
        // #1489: `anchored` is a fact about the CAMERA -- whether its wedge can be read
        // at all -- and it is now kept apart from `wedge_measured`, which is a fact about
        // THIS READING. Every line below that asks whether there is an angular ruler to
        // use asks `anchored`, so none of them moves; the field says the narrower thing.
        //
        // #1486: or an anchor DERIVED from a camera that did measure one. The rotation
        // between two cameras' wire rings is a rigid fact about the rig, measured off
        // darts both cameras placed and believed only once two darts agreed on it
        // (`orientation_processing::noteAnchorSighting`). It is deliberately NOT written
        // back into `calib.orientation`: #1450 seals `read=` and `wedge20=` into the
        // geometry fingerprint at the end of `initialize`, so a scoring-time inference
        // stored there would read as the rig having moved. A derived anchor is a fact
        // about this RUN, held beside the geometry rather than in it.
        const bool ownAnchor = orientation_processing::wedgeCanBeRead(calib.orientation);
        const bool anchored = ownAnchor || derived.trusted;

        // #1489, stated rather than arrived at: on a bull and on an outer bull the ring
        // ellipses are the whole of the score and the wedge is no part of it. The reading
        // is therefore neither a measurement of a wedge nor an assertion of one, which is
        // #1346's sentence about a bull with the half it left implicit written down. Both
        // flags stay false here whatever this camera's anchor is worth, and the vote --
        // which split the cameras on `wedge_asserted` alone -- stops counting a ring
        // reading among the ones that measured a wedge.
        out.ring_only = on_bull && !ringOnlyReadingsCountAsMeasured();
        out.wedge_measured = anchored && !out.ring_only;
        if (!anchored && !on_bull)
        {
            log_debug("SCORE: No orientation data, defaulting to 20");
        }

        // On a bull there is no wedge to decide, but the angle is still known where the
        // orientation is; where it is not, nothing implies one, and the angle stays absent.
        if (on_bull && !anchored)
        {
            return out;
        }

        float fraction = 0.0f;
        int start = ownAnchor ? calib.orientation.wedge20WireIndex : (derived.trusted ? derived.wedge20WireIndex : 0);
        int slot = findWedgeSlot(pixel, calib, start, fraction);
        if (slot < 0)
        {
            log_debug("SCORE: No wedge found - this shouldn't happen");
            if (on_bull)
            {
                return out;
            }
            if (!anchored)
            {
                // Upstream asserted the 20 without looking at a wire; so does this, and
                // with no wedge to place the tip in there is no angle to state.
                out.wedge_asserted = true; // #1346: marked at the site of the assertion
                out.segment = 20;
                out.score = ring_prefix + "20";
                return out;
            }
            out.score = "MISS";
            out.ring.clear();
            out.board = BoardPosition();
            return out;
        }

        // With no orientation the wedge the tip is in is asserted to be the 20 - slot 0 of
        // the sequence - and the fraction says where across that wedge the tip is.
        // #1346: a bull cannot reach this as an assertion -- on_bull without an anchor
        // returned above -- so the flag marks exactly the asserted 20s. #1489 says that
        // in the expression rather than relying on the reader to trace the return: a
        // ring-only reading asserts nothing, whatever the anchor is worth.
        out.wedge_asserted = !anchored && !on_bull;
        int sequence_slot = anchored ? slot : 0;
        float angle = 18.0f * sequence_slot - 9.0f + 18.0f * fraction;
        while (angle < 0.0f)
            angle += 360.0f;
        while (angle >= 360.0f)
            angle -= 360.0f;
        out.board.has_angle = true;
        out.board.angle = angle;

        if (!on_bull)
        {
            int number = dartboard_numbers[sequence_slot];
            log_debug("SCORE: Found wedge " + log_string(number));
            out.segment = number;
            out.score = ring_prefix + to_string(number);
        }
        return out;
    }

    // ---- #1486: anchors derived from darts every camera saw -----------------------------
    //
    // Held here rather than in `calibrations` on purpose, and the reason is #1450: the
    // geometry fingerprint sealed at the end of `initialize` carries `read=` and
    // `wedge20=`, so writing a scoring-time derivation back into a calibration would
    // announce itself as the rig having moved. This is a fact about the run.
    //
    // A file-static beside `initialized`, which is this file's existing shape for
    // per-process state. The DECISION is pure and lives in orientation_processing.hpp,
    // where a tester holds it without building a detector (#1338).
    static vector<orientation_processing::DerivedAnchor> derived_anchors;

    /**
     * Where this camera placed the tip in its OWN wire ring, or nothing.
     *
     * `findWedgeSlot` from wire 0 is the whole of it -- the same angular ruler the score
     * is read with, asked without an anchor, so the two cannot drift. A tip off the board
     * is refused a sighting: its angle is still an angle, but a tip finder that has put it
     * outside the doubles ring is not a witness to where on the board anything is.
     */
    static orientation_processing::WedgeSighting sightingOf(Point2f pixel, const DartboardCalibration &calib)
    {
        orientation_processing::WedgeSighting out;
        if (!aDartIsScoredFrom(calib) || !isPointInEllipse(pixel, calib.ellipses.outerDoubleEllipse))
        {
            return out;
        }
        float fraction = 0.0f;
        const int slot = findWedgeSlot(pixel, calib, 0, fraction);
        if (slot < 0)
        {
            return out;
        }
        out.present = true;
        out.wireSlot = slot;
        out.fraction = fraction;
        return out;
    }

    /**
     * Let this dart say what it can about the cameras that cannot anchor themselves.
     *
     * The leader is the first camera the scorer would read a wedge from on its own
     * measurement -- `wedgeCanBeRead`, the one expression #1449 left in the codebase --
     * and it is never a derived one: a derivation derived from a derivation would let one
     * camera's reading travel round the board and come back as its own corroboration.
     *
     * Called BEFORE the scoring loop, so the dart that completes a derivation is itself
     * scored with it.
     */
    static void deriveAnchorsFromThisDart(const dart_processing::DartStateResult &dart_result,
                                          const vector<DartboardCalibration> &calibrations)
    {
        if (derived_anchors.size() != calibrations.size())
        {
            derived_anchors.assign(calibrations.size(), orientation_processing::DerivedAnchor());
        }
        if (orientation_processing::anchorsComeFromOneCameraOnly())
        {
            return; // #1486's falsifier: a camera is anchored by its own measurement or not at all
        }

        vector<orientation_processing::WedgeSighting> sightings(calibrations.size());
        for (size_t i = 0; i < calibrations.size() && i < dart_result.camera_results.size(); i++)
        {
            if (!dart_result.camera_results[i].frame_available || !dart_result.camera_results[i].tip_found)
            {
                continue;
            }
            sightings[i] = sightingOf(dart_result.camera_results[i].tip_position, calibrations[i]);
        }

        int leader = -1;
        for (size_t i = 0; i < calibrations.size(); i++)
        {
            if (sightings[i].present && orientation_processing::wedgeCanBeRead(calibrations[i].orientation))
            {
                leader = (int)i;
                break;
            }
        }
        if (leader < 0)
        {
            return; // nothing to propagate FROM. #1486 propagates an anchor; it cannot create one.
        }

        for (size_t i = 0; i < calibrations.size(); i++)
        {
            if ((int)i == leader || !sightings[i].present ||
                orientation_processing::wedgeCanBeRead(calibrations[i].orientation))
            {
                continue;
            }
            orientation_processing::DerivedAnchor &anchor = derived_anchors[i];
            const bool wasTrusted = anchor.trusted;
            const bool wasRefused = anchor.refused;
            const orientation_processing::AnchorSighting sighting =
                orientation_processing::anchorFromOneDart(sightings[leader],
                                                          calibrations[leader].orientation.wedge20WireIndex,
                                                          sightings[i], (int)calibrations[i].wires.wireEndpoints.size());
            orientation_processing::noteAnchorSighting(anchor, sighting, leader);

            // While a camera is undecided every sighting is said out loud, because those
            // are the sightings that decide; once it is trusted it goes quiet, and the
            // only thing that speaks again is the refusal -- which is said once, because a
            // camera that has been refused meets every later dart the same way and a line
            // per dart for the rest of the evening is not a second finding.
            if (!wasRefused && (!wasTrusted || anchor.refused))
            {
                log_info("ANCHOR: " + orientation_processing::howItDerived((int)i, anchor) +
                         " (this dart: offset " + to_string(sighting.offsetWedges) +
                         " wedges, residual " + to_string(sighting.residualWedges) + ")");
            }
            // #1486's acceptance criterion about the TOP/BOTTOM guess, measured rather
            // than argued: that branch computed an index from an assumption about where
            // the camera is bolted, and #797 measured it one wedge loose. It is still
            // computed and still trusted by nothing; where the board has now said what
            // the index really is, the two are printed against each other.
            if (anchor.trusted && !wasTrusted &&
                (calibrations[i].orientation.cameraPosition == orientation_processing::CameraPosition::TOP ||
                 calibrations[i].orientation.cameraPosition == orientation_processing::CameraPosition::BOTTOM))
            {
                log_info("ANCHOR: camera " + to_string(i + 1) + "'s positional guess (" +
                         orientation_processing::cameraPositionToString(calibrations[i].orientation.cameraPosition) +
                         ", assuming wedge " + to_string(calibrations[i].orientation.wedgeNumber) +
                         " at its image south) put wedge 20 at wire " +
                         to_string(calibrations[i].orientation.wedge20WireIndex) +
                         "; the board says wire " + to_string(anchor.wedge20WireIndex) +
                         ". The guess is not what was used.");
            }
        }
    }

    /** This camera's derived anchor, or an untrusted one, which changes nothing. */
    static orientation_processing::DerivedAnchor anchorFor(size_t camera)
    {
        return camera < derived_anchors.size() ? derived_anchors[camera] : orientation_processing::DerivedAnchor();
    }

    // ---- #1510 Phase 2: the model's answer, measured BESIDE the published one ----------
    //
    // OD_MODEL_SCORE=on prints, for every camera that scored a tip, what the fitted
    // board model (board_model::scoreFromModel) says about the SAME pixel the existing
    // per-ring machinery just scored -- one I1510P2 line per camera per dart, parsed by
    // testers/i1510p2_census.py into the paint-containment census. It is a shadow: the
    // published score comes from `point_scores` exactly as before, on and off alike,
    // and the default is off, so the binary's scores do not move in this slice. The
    // convention is OD_RING=span's: anything but the exact word `on` is ignored.
    static bool modelScoreShadowed()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_MODEL_SCORE");
            return e != nullptr && string(e) == "on";
        }();
        return v;
    }

    static void logModelShadow(int camera, const DartboardCalibration &calib, Point2f tip,
                               const PointScore &existing,
                               const orientation_processing::DerivedAnchor &derived)
    {
        // Refit per dart rather than caching: pure over the calibration, a few hundred
        // quadratics, and a cache keyed on camera index would go stale the day a
        // mid-run recalibration lands. Measurement-only code buys simplicity first.
        const board_model::BoardProfile profile =
            board_model::profileFromSpec(perspective_processing::DartboardSpec());
        const board_model::BoardFit fit =
            board_model::fitBoardToCamera(profile, calib, wire_processing::conicOfDoublesFor(calib));

        // The same anchor decision scorePoint just took: the camera's own wire first,
        // then a derived one (#1486), else unresolved -- the model never asserts a 20.
        int wedge20 = -1;
        const char *anchorWord = "none";
        if (orientation_processing::wedgeCanBeRead(calib.orientation))
        {
            wedge20 = calib.orientation.wedge20WireIndex;
            anchorWord = "own";
        }
        else if (derived.trusted)
        {
            wedge20 = derived.wedge20WireIndex;
            anchorWord = "derived";
        }
        vector<Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
        const board_model::ModelAnchor anchor = board_model::anchorOnBoard(fit, endpoints, wedge20);
        const board_model::ModelScore model = board_model::scoreFromModel(profile, fit, anchor, tip);

        // One line, greppable, every number the census needs. The wire-coherence margin
        // is here because of the watch item on #1510: an admission that scraped past
        // 0.60 must say by how much, per camera, where a dart was actually scored.
        char line[512];
        snprintf(line, sizeof(line),
                 "I1510P2 MODEL cam=%d tip=(%.1f,%.1f) existing=%s model=%s agree=%d "
                 "mm=(%.1f,%.1f) r=%.1f ringB=%.1f wedgeB=%.1f boundary=%.1f "
                 "fit=%s R=%.3f margin=%+.3f anchor=%s",
                 camera + 1, tip.x, tip.y, existing.score.c_str(),
                 model.valid ? model.score.c_str() : "NONE",
                 model.valid && model.score == existing.score ? 1 : 0,
                 model.boardMm.x, model.boardMm.y, model.radiusMm,
                 model.ringBoundaryMm, model.wedgeBoundaryMm, model.boundaryMm,
                 fit.accepted ? "ACCEPTED" : "REJECTED",
                 fit.wireCoherence, fit.wireCoherence - wire_model::minimumCoherence(),
                 anchorWord);
        log_info(line);
    }

    ScoreResult processScore(const vector<Mat> &background_frames, const dart_processing::DartStateResult &dart_result, const vector<DartboardCalibration> &calibrations, bool debug_mode)
    {

        if (!initialized)
        {
            log_debug("SCORE: Initializing score processing");
#ifdef DEBUG_VIA_VIDEO_INPUT
            // #812: this listener is an unauthenticated MJPEG view of the board on
            // 0.0.0.0:8088. It used to be opened here on the first scored cycle of
            // every run — behind no define and no debug test — while the only push
            // to it is already behind debug_mode. It is now behind both, which is
            // the same gate the raw camera feed on 8081 has always had, so a
            // release build (make build, OD_DEFS empty) opens neither.
            if (debug_mode)
            {
                point_on_screen_streamer = make_unique<streamer>(8088, 1);
            }
#endif
            initialized = true;
        }

        ScoreResult result;

        if (dart_result.previous_state == dart_result.current_state)
        {
            // No state change, return invalid result
            result.valid = false;
            return result;
        }

        // State changed - now process the scoring
        switch (dart_result.current_state)
        {
        case dart_processing::DartBoardState::CLEAN:
            result.score = "END";
            result.confidence = 1.0f;
            result.camera_index = -1;
            result.valid = true;
            break;

        case dart_processing::DartBoardState::DART_1:
        case dart_processing::DartBoardState::DART_2:
        case dart_processing::DartBoardState::DART_3:
            // Collect scores from all cameras with detected tips
            vector<Mat> points_on_screen;                                       // For debug images
            vector<PointScore> point_scores(dart_result.camera_results.size()); // #1186: each camera's decision, as fields
            vector<bool> may_vote(dart_result.camera_results.size(), false);    // #1346: a tip and not a MISS, as it always was

            // #1486: what this dart says about the cameras that cannot anchor themselves,
            // before anything is scored with it.
            deriveAnchorsFromThisDart(dart_result, calibrations);

            for (size_t i = 0; i < dart_result.camera_results.size(); i++)
            {
                // #798: no frame from this camera in the window, so calibrations[i] has
                // nothing to score. It abstains rather than contributing a MISS.
                if (!dart_result.camera_results[i].frame_available)
                {
                    log_warning("Camera " + to_string(i) + " score: ABSTAIN (no frame captured)");
                    if (debug_mode)
                        points_on_screen.push_back(Mat());
                    continue;
                }

                // #1318: and a camera that was refused at calibration abstains for the
                // life of the run. Its slot is still here -- so that every other camera
                // keeps its own calibration -- but there is no dartboard in its picture
                // to score a tip against, and a MISS from it is not an observation.
                if (i >= calibrations.size() || !calibrations[i].sees_board)
                {
                    log_warning("Camera " + to_string(i) + " score: ABSTAIN (this camera is not looking at the dartboard)");
                    if (debug_mode)
                        points_on_screen.push_back(Mat());
                    continue;
                }

                log_debug("-------");
                PointScore point = scorePoint(dart_result.camera_results[i].tip_position, calibrations[i], anchorFor(i));
                string score_test = point.score;
                point_scores[i] = point;
                log_debug("-------");

                // #1510 Phase 2, shadow only (OD_MODEL_SCORE=on): what the fitted board
                // model says about the same tip, beside what was just scored. Nothing
                // below reads `model`'s answer; the published score is `point`'s.
                if (modelScoreShadowed())
                {
                    logModelShadow((int)i, calibrations[i], dart_result.camera_results[i].tip_position,
                                   point, anchorFor(i));
                }

                // print image
                if (debug_mode)
                {
                    log_warning("Camera " + to_string(i) + " score: " + score_test);

                    // just draw the point on the screen
                    Mat some_mat = background_frames[i].clone();
                    circle(some_mat, dart_result.camera_results[i].tip_position, 5, Scalar(0, 255, 0), -1);
                    putText(some_mat, score_test, dart_result.camera_results[i].tip_position + Point2f(10, 10),
                            FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 0, 0), 1);
                    odfs::ensureDirectory("debug_frames/score_processing");
                    imwrite("debug_frames/score_processing/point_on_screen" + to_string(i) + ".jpg", some_mat);

                    points_on_screen.push_back(some_mat);
                }

                // #1346: whether this camera may vote -- a found tip and not a MISS,
                // as it always was. #1505 stated the MISS half as `aVoteIsCast`, where
                // the measurement that says why a surround MISS still may not vote is
                // recorded beside the decision. How a vote COUNTS is chooseScore's.
                may_vote[i] = dart_result.camera_results[i].tip_found && aVoteIsCast(point);
            }

            if (debug_mode && point_on_screen_streamer)
            {
                try
                {
                    point_on_screen_streamer->push(debug::createCombinedFrame(points_on_screen, "points_on_screen"));
                }
                catch (const std::exception &e)
                {
                    log_error("Failed to push points_on_screen frame: " + string(e.what()));
                }
            }

            // #1346: the vote itself is chooseScore, pure and in the header, where a
            // tester can hold it. A camera whose wedge was asserted -- the default-to-20
            // -- no longer counts toward a consensus and no longer earns the 0.9 by
            // agreeing with another assertion; it is published only when nothing
            // measured, at 0.5. #796 measured the failure this removes: S20 S20 S20
            // published over a hand-verified 36, two constants outvoting the camera
            // that measured.
            const ScoreChoice choice = chooseScore(point_scores, may_vote);
            if (choice.camera >= 0)
            {
                const int best_camera = choice.camera;
                const string final_score = point_scores[best_camera].score;

                if (choice.agreeing >= 2)
                {
                    log_info("Consensus score: " + final_score + " from " + to_string(choice.agreeing) + " cameras");
                }
                else if (!choice.by_default)
                {
                    // #1517: when the fallback passed over a camera missing a ring, the
                    // account says so -- a lone 0.7 that was chosen for completeness must
                    // not read like a lone 0.7 that was merely first. The base sentence
                    // is byte-for-byte what it was; i1484's census greps it.
                    log_info("No consensus, using single camera score: " + final_score + " from camera " + to_string(best_camera) +
                             (choice.preferred_complete
                                  ? " (preferred: it fitted every ring, a lower camera did not)"
                                  : ""));
                }
                else
                {
                    log_info("No measured wedge: publishing camera " + to_string(best_camera) +
                             "'s wedge-by-default " + final_score + " at low confidence");
                }

                result.score = final_score;
                result.pixel_position = dart_result.camera_results[best_camera].tip_position;
                result.center_position = dart_result.camera_results[best_camera].center_position;
                result.dartboard_position = dart_result.camera_results[best_camera].tip_position; // TODO: Convert to dartboard coordinates
                result.confidence = choice.confidence;
                result.camera_index = best_camera;
                result.valid = true;
                // #1186: the board-frame fields come from the same camera and the same
                // decision the score string came from, never from the string.
                result.ring = point_scores[best_camera].ring;
                result.segment = point_scores[best_camera].segment;
                result.board = point_scores[best_camera].board;
                // #1489: three states where this line printed two. "wedge measured" and
                // "wedge by default" are byte-for-byte what they were -- #1484's census
                // reads them -- and a BULL or an OUTER now says the thing it always was:
                // the ring ellipses scored this and no wedge entered it. It used to print
                // "wedge by default" for those, beside a confidence of 0.7 or 0.9 whose
                // whole meaning is that a camera measured one.
                log_info(string("BOARD: ") + howTheWedgeWasRead(point_scores[best_camera]) +
                         " | ring=" + result.ring +
                         " | segment=" + to_string(result.segment) +
                         " | radius=" + (result.board.has_radius ? to_string(result.board.radius) : string("none")) +
                         " | angle=" + (result.board.has_angle ? to_string(result.board.angle) : string("none")));
            }
            else
            {
                // State changed but no valid scores found
                result.score = "MISS";
                result.confidence = 0.5f;
                result.camera_index = -1;
                result.valid = true;

                if (debug_mode)
                {
                    log_warning("State changed but no valid scores found!");
                }
            }
            break;
        }

        return result;
    }

} // namespace score_processing

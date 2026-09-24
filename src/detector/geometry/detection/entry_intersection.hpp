#pragma once

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../calibration/board_model.hpp"
#include "shaft_axis.hpp"

/**
 * #1512: ONE BOARD-PLANE ENTRY POINT FROM EVERY RELIABLE CAMERA CONSTRAINT, SCORED ONCE.
 *
 * At detector v0.1.10 `processScore()` scores a tip PER CAMERA and `chooseScore()` picks
 * a camera's score STRING; no cross-camera geometry ever determines where the dart is.
 * This header is the geometric-localisation path under #1488 instead: each camera's
 * fitted shaft axis (#1511) is an image line; with `x_image ~ H * X_board` the
 * corresponding board-plane line is `l_board ~ H^T * l_image`, and for a straight dart
 * every camera's transported line passes through the one physical entry point -- the
 * dart's 3D axis lies in the plane the image line and the camera centre span, and that
 * plane cuts the board plane exactly along the transported line. Two sufficiently
 * independent constraints determine the entry; three permit a consistency check and a
 * robust weighted fit. The parallax trap the issue names is thereby avoided by
 * construction: no shaft or flight PIXEL is ever inverse-warped as if it lay on the
 * board surface -- only the LINE is transported, and the only point ever read off the
 * board plane is the intersection.
 *
 * THE SHARED FRAME IS THE NUMBERED ONE, AND AN ANCHOR IS ITS PRICE. Each camera's
 * `wire_model::Plane` is pinned only up to O(2) (board_model.hpp states it), so two
 * cameras' plane frames differ by an unknown rotation and possibly a mirror, and a line
 * transported into camera coordinates is NOT yet in a shared coordinate system. What all
 * cameras share is the physical board's numbering: `board_model::ModelAnchor` states
 * where the 20's first boundary sits (`theta20`) and which way the sequence advances
 * (`advance`) in each camera's own frame. The canonical frame here is therefore
 * SEQUENCE SPACE: angle phi = advance * (theta - theta20), radius in millimetres --
 * phi 0 at the 20's first boundary, advancing through 20, 1, 18... exactly as
 * `scoreFromModel`'s slot arithmetic walks it. The map is an isometry (rotation, plus a
 * mirror where advance is -1), so millimetre distances and uncertainties survive it.
 * A camera whose rotation is unresolved cannot place its evidence in this frame and is
 * excluded BY NAME -- never silently averaged in a frame nobody shares.
 *
 * WHAT #1511'S TWO FINDINGS DO TO THE WEIGHTS. Finding one: the axis DIRECTION is the
 * trustworthy half -- a parallel cast shadow displaced an accepted line laterally by up
 * to 192 px while the angle held within degrees (rig-20260922). A line constraint
 * cannot be used as a direction alone (a direction through an unknown point constrains
 * nothing on the board), so the lateral half is carried at a MEASURED statistical
 * uncertainty and the shadow's systematic part is left to the checks that can see it:
 * the three-camera consistency residual, and agreement with visible entry-point (tip)
 * evidence, both reported per camera on every solve. Finding two: `sigmaDeg` states
 * centreline scatter only and understated the annotation-judged error by a median
 * factor of 19-31, so the direction uncertainty is FLOORED (Params::sigmaDirFloorDeg)
 * rather than trusted raw.
 *
 * TIPS ARE EVIDENCE, NOT CONSTRAINTS -- A DESIGN DECISION WITH ITS NUMBERS. The issue
 * allows a directly observed surface-entry point as an explicit point constraint, and
 * this slice deliberately does not put the published tip into the solve: #1492 measured
 * the between-camera spread of that tip at median 73.6 mm on rig-20260918 (published
 * tips off their own contour, floor-bound pieces), and a point constraint that wrong
 * would outvote two honest lines. The tip is instead the CORROBORATION check: every
 * camera with a placed tip reports its millimetre distance from the solved entry, the
 * census measures the distribution, and promoting the tip to a constraint is a later
 * decision to take on those numbers, not in passing.
 *
 * MEASURED ON THE FIXTURES (testers/i1512_run.sh, 2026-09-24), and what a consumer
 * must carry. rig-20260918, 17 event-matched darts: the string vote scored 13/17 and
 * the solver 11 of the 13 it solved (4 refused by name), with solved-position error
 * median 6.0 mm, p90 12.5 mm against the hand-annotated entries -- and the claimed
 * sigma (4.6-10.2 mm) is the same size as the measured error, so the instrument is
 * honest about itself. BOTH of the vote's wedge errors on that clip (S7 published
 * over a thrown 19, twice) are corrected by the geometry, which read S19 with the
 * annotation agreeing to 2-6 mm. Ten of thirteen solves flagged WIRE-UNCERTAIN:
 * at ~6 mm precision most darts sit within one sigma of some wire, which is the
 * treble-edge band (#1510 Phase 2's T14) stated honestly rather than averaged over.
 * rig-20260922 is #1511 FINDING ONE at solve level and the recorded limitation of a
 * two-camera event: its shadow-displaced axes AGREE WITH ITS SHADOW-DISPLACED TIPS
 * (nearest tip ~5 mm) while both sit ~140 mm from the annotated entry, and with
 * camera 1 unfitted there is no third line for the chi-square to refuse on -- so a
 * consumer must treat a two-constraint solve on a hard-shadow scene as unproven,
 * and the census is what says which scenes those are. The #1505/#1535 targets:
 * rig-18 v6's phantom S7 and rig-22 v1.2's re-report both refuse as
 * TOO-FEW-CONSTRAINTS; rig-18 v4's out-of-plane miss (published T20@0.7) solves to
 * T7 with ZERO of two placed tips corroborating -- the tips=0/N field is the
 * exposure, and a reader of the census must not take an uncorroborated solve as a
 * position.
 *
 * WHAT REFUSES, EACH BY NAME (the issue's own list): fewer than two usable constraints
 * (every per-camera exclusion listed -- this is also what exposes a lone-witness
 * phantom, #1505: a dart that is not on the board has no second constraint); nearly
 * parallel constraints (the crossing angle is the conditioning, and 1/sin(angle) is
 * the error amplifier); inconsistent cameras (a chi-square the claimed uncertainties
 * cannot explain -- and because three disagreeing lines are SYMMETRIC, the liar is
 * named only where the tip evidence uniquely corroborates the other two, else the
 * whole solve refuses); and uncertainty crossing a scoring wire (`boundaryMm` against the
 * solved position's own sigma -- named as an outcome, never averaged over; Phase 2's
 * treble-edge finding is exactly this band). The degraded fallback stays what the
 * code does today -- the string vote -- reached honestly and labelled as itself:
 * nothing here publishes, and flipping any default is #1488's decision.
 *
 * #1556: AND THE WIRE QUESTION IS NOW ASKED IN THE WIRE'S OWN DIRECTION, AND ANSWERED
 * WITH TWO SCORES. The list above says "uncertainty crossing a scoring wire"; until this
 * issue that was `boundaryMm <= sigmaMajorMm`, the distance to the nearest call-flipping
 * boundary against the LONGEST axis of the error ellipse whichever way that axis pointed.
 * A ring wire is crossed radially and a sector wire tangentially, so the sigma that can
 * spend the demotion is the position sigma RESOLVED ALONG THAT BOUNDARY'S NORMAL --
 * floored at `Params::sigmaAcrossFloorMm` because resolving an ellipse can only shrink a
 * number and #1511's finding is that the claimed one is already optimistic. Only the
 * statistical half spends it: the treble band's systematic bloom is corrected per camera
 * per edge before `boundaryMm` is measured (#1553). A flagged dart names BOTH candidates
 * -- the published one and what the board reads just across that wire, re-scored through
 * the same fit and anchor -- because the maintainer's decision (#1557) is that it
 * publishes the more probable candidate immediately and a tap picks the other. Both
 * verdicts, the across-boundary one and #1555's, are computed on every solve;
 * `OD_WIRE_FLAG=sigma-major` publishes on the old one and `OD_ENTRY_SIGMA=zero` is the
 * mutation.
 *
 * Pure and inline for #1338's reason: a tester holds every verdict below with planted
 * homographies and the real pipeline holds it with fitted ones.
 * testers/i1512_intersect_check.cpp does, and testers/i1556_flag_check.cpp holds the
 * crossing rule on the same plants.
 */
namespace entry_intersection
{
    /**
     * The gates and floors, each carrying its denominator and census.
     */
    struct Params
    {
        // The direction-uncertainty floor, degrees at one sigma. #1511 finding two,
        // measured against the hand annotations (testers/i1511_annotations, 74 lines):
        // raw sigmaDeg understates the annotation-judged angle error by a median factor
        // of 19-31, and the annotation floor itself is 1.5-4 degrees (README's +-2 px
        // over 70-150 px baselines). 3.0 sits inside that measured floor band; nothing
        // below it is measurable with the reference this repository has.
        double sigmaDirFloorDeg = 3.0;

        // The lateral-placement uncertainty of an accepted axis, image px at one sigma
        // -- the STATISTICAL half only. Measured on rig-20260918 (the ground-truthed
        // scene, 34 annotated pairs, i1511_run.sh 2026-09-24): the per-camera median
        // perpendicular miss of accepted axes at the annotated entry point is
        // 3.2 / 4.4 / 2.3 px, and that median already contains the direction share, so
        // 4.5 covers the worst camera's median. The SYSTEMATIC part -- a parallel
        // shadow's pull, measured up to 192 px on rig-20260922 -- is deliberately NOT
        // in this number: a floor wide enough to contain it would make every
        // constraint worthless everywhere, and the consistency residual and tip
        // corroboration are the instruments that can see a displaced line on the rigs
        // where it happens.
        double sigmaLateralPx = 4.5;

        // The conditioning gate: the largest crossing angle among usable constraint
        // pairs, degrees on the board plane. Perpendicular error amplifies as
        // 1/sin(angle): at 15 degrees a 3.5 mm constraint sigma is already a 13.5 mm
        // position error along the poorly-conditioned direction -- the whole width of
        // more than a wedge at the treble ring -- and shallower pairs grow without
        // bound. 15 refuses the unusable geometry while keeping every pair the rigs
        // actually produce, MEASURED (i1512_run.sh, 2026-09-24): solved events cross
        // at 54.8-89.7 degrees on rig-20260918 and 37.6-50.3 on rig-20260922, so the
        // gate sits a factor of 2.5 under the shallowest real pair.
        double minPairAngleDeg = 15.0;

        // The consistency verdict on a three-constraint solve is a chi-square on the
        // joint residuals, per degree of freedom (three lines, two coordinates: one
        // dof), and 9.0 is the square of an ordinary 3-sigma. MEASURED reason it is a
        // chi-square and not a per-line sigma gate: a line displaced 30 mm among two
        // honest ones dilutes into ~10 mm joint residuals against lever-inflated
        // sigmas of ~4 mm -- every ratio 2.4-2.8, all under any per-line 3-sigma gate,
        // while the chi-square reads ~19 against this 9 (i1512_intersect_check's
        // displaced-line plant, measured on the first build of this file).
        double chi2PerDof = 9.0;

        // Before a named liar may be excluded, its residual against the OTHER
        // cameras' solve must exceed this many of its own claimed sigmas -- an
        // exclusion of a line the reduced solve does not even refute would be
        // arbitrary. 3.0: ordinary statistics on a floored sigma.
        double consistencySigmas = 3.0;

        // #1556: THE FLOOR ON THE ACROSS-BOUNDARY POSITION SIGMA, millimetres at one
        // sigma, and it is the thing that keeps the directional resolution below from
        // becoming a way of flagging less. #1511 finding two is that a claimed sigma
        // understates the error a reference can see; the same is measured one stage on
        // for the SOLVED POSITION, against the hand-annotated entries
        // (testers/i1511_annotations, i1512_census's POSITION line, rig-20260918):
        // solved-vs-annotated median 6.0 mm over 13 solves. A two-dimensional Gaussian
        // with per-axis sigma s has median radial error 1.177*s, so that median is a
        // per-axis sigma of 5.1 mm -- and the annotation's own +-2 px is inside the 6.0,
        // so 5.1 is if anything an over-reading of the instrument. 5.0 is the floor, and
        // the minor axis of the claimed ellipse runs 2.4-6.6 mm on the same darts, so
        // this binds on exactly the readings where resolving the ellipse would otherwise
        // claim a precision no reference in this repository can see.
        //
        // testers/i1556_census.py re-measures it on every run: it prints the same
        // position-error distribution beside the claimed across-boundary sigma, so a
        // floor that stopped matching the instrument is visible rather than inherited.
        double sigmaAcrossFloorMm = 5.0;

        // #1556: how many ACROSS-BOUNDARY sigmas of clearance a call needs before the
        // uncertainty is said not to reach the wire. One sigma, deliberately, because
        // that is what the published vocabulary already means: #1555 wired 0.7 to "the
        // sigma reaches a call-flipping wire" and this issue changes how that sigma is
        // measured, not what the demotion says. Fitting the number to the fixtures was
        // refused with its own arithmetic -- the two wrong solves on rig-20260918 sit at
        // 0.08 and 0.39 sigmas of their boundary, so any k down to 0.4 would still catch
        // both while flagging far less, which is a rule fitted to two darts. The sweep
        // is REPORTED by the census instead, at every k from 0.25 to 2.0, so moving it is
        // a decision somebody takes on a table rather than a constant somebody nudges.
        double crossingSigmas = 1.0;

        // Within how many millimetres a placed tip counts as CORROBORATING a solve.
        // Two jobs: the census's agreement count on every event, and the TIE-BREAK
        // when three lines disagree -- a triangle of three lines is symmetric (each
        // vertex sits equally far from the opposite line, measured in the check), so
        // lines alone cannot say which camera lies, and the tip evidence is what can.
        // 15 mm spans the annotation's own tip reading (+-2 px ~ 1-2 mm) plus the tip
        // detector's per-camera error scale on the repaired tree (#1494/#1495), and
        // it was MEASURED against the fixtures before being kept (i1512_run.sh,
        // 2026-09-24): the nearest placed tip sits median 4.5 mm, p90 8.1 mm from
        // rig-20260918's solved entries, so honest agreement fits inside 15 with
        // room and the census prints every distance for re-measurement.
        double tipAgreeMm = 15.0;
    };

    /** Why one camera's evidence was or was not usable, and what it said. */
    struct Constraint
    {
        int camera = -1;
        bool offered = false;    // this camera had a slot in the solve at all
        bool usable = false;     // a transported line in the shared frame, weights and all
        bool excluded = false;   // was usable, refused by the consistency check
        std::string exclusion;   // the named reason, when !usable or excluded

        // The transported line, canonical board mm: unit normal (nx,ny) and offset c,
        // a point X on the line satisfying nx*X.x + ny*X.y + c = 0.
        double nx = 0.0, ny = 0.0, c = 0.0;
        double tangentDeg = 0.0;      // the line's direction in the canonical frame, [0,180)
        cv::Point2f axisPointMm;      // the axis support centroid, transported (lever origin)
        double sigmaDirDeg = 0.0;     // floored direction uncertainty actually used
        double sigmaPerpMm = 0.0;     // perpendicular sigma AT the solved point
        double leverMm = 0.0;         // along-line distance from axisPointMm to the solve
        double pxPerMm = 0.0;         // this camera's scale at the bull (fit's own figure)
        double residualMm = 0.0;      // n.X + c at the solved point, signed
        double residualPx = 0.0;      // image-space distance, reprojected solve to image line

        // The observed image line, kept for overlays and reprojection residuals.
        cv::Point2f imagePoint;
        cv::Point2f imageDir;

        // The solved entry, reprojected into this camera's image -- filled for every
        // placeable camera when something solved, because the census judges position
        // against annotated entry points in IMAGE space, where the annotation lives.
        bool solvedImagePlaced = false;
        cv::Point2f solvedImage;

        // Entry-point (tip) evidence, independent of the axis: present wherever this
        // camera placed a tip and its frame could be placed in the shared frame.
        bool tipPlaced = false;
        cv::Point2f tipMm;            // canonical
        double tipRadiusMm = 0.0;
        double tipDistanceMm = -1.0;  // from the solved entry; -1 where nothing solved
    };

    /** The named outcomes. Everything but Solved and UncertainAcrossWire leaves the
     *  published path exactly as it is -- the string vote, labelled as itself. */
    enum class Outcome
    {
        TooFewConstraints,
        NearParallel,
        Inconsistent,
        UncertainAcrossWire, // solved AND scored, and the sigma reaches a call-flipping wire
        Solved
    };

    inline const char *outcomeWord(Outcome o)
    {
        switch (o)
        {
        case Outcome::TooFewConstraints:
            return "TOO-FEW-CONSTRAINTS";
        case Outcome::NearParallel:
            return "NEAR-PARALLEL";
        case Outcome::Inconsistent:
            return "INCONSISTENT";
        case Outcome::UncertainAcrossWire:
            return "WIRE-UNCERTAIN";
        default:
            return "SOLVED";
        }
    }

    /** One camera's offering to the solve. `fit` is caller-owned and outlives the call. */
    struct CameraEvidence
    {
        int camera = -1;
        const board_model::BoardFit *fit = nullptr;
        board_model::ModelAnchor anchor;

        bool axisValid = false;
        std::string axisRefusal;      // #1511's own words, carried into the exclusion
        cv::Point2f axisPoint;        // image px
        cv::Point2f axisDir;          // unit, image px
        double axisSigmaDeg = -1.0;

        bool tipFound = false;
        cv::Point2f tipImage;
    };

    /** The whole verdict of one solve. */
    struct EntrySolution
    {
        Outcome outcome = Outcome::TooFewConstraints;
        std::string story;                  // the verdict in words, numbers beside names
        std::vector<Constraint> constraints; // one per offered camera, in offer order

        bool solved = false;                // an entry point exists (Solved or WIRE-UNCERTAIN)
        cv::Point2f entryMm;                // canonical board mm
        double radiusMm = 0.0;
        double phiDeg = 0.0;                // canonical angle, degrees in [0,360)
        double sigmaMajorMm = 0.0;          // 1-sigma error ellipse, major axis
        double sigmaMinorMm = 0.0;
        double sigmaThetaDeg = 0.0;         // major axis direction, canonical frame
        double bestPairAngleDeg = 0.0;      // the conditioning actually available
        int usableConstraints = 0;          // in the final solve
        int offeredConstraints = 0;

        // The score, read ONCE -- through board_model::scoreFromModel on the reference
        // camera (the lowest-index usable constraint), never through new ring
        // arithmetic. `scoreByCamera` says what every placeable camera makes of the
        // same point, "/"-joined, "-" where a camera cannot be placed. What CAN
        // differ there is the ring, near a ring wire, because each fit carries its
        // own millimetre scale; the WEDGE cannot differ by construction -- the
        // read-back goes through the same anchor that placed the point, so a stale
        // anchor cancels in this reading and is caught by the consistency check on
        // the LINES instead (measured in i1512_intersect_check's stale-anchor plant).
        board_model::ModelScore score;
        int scoredThroughCamera = -1;
        std::string scoreByCamera;
        bool scoresAgree = false;

        int tipWitnesses = 0;               // cameras contributing placed tip evidence
        int tipCorroborations = 0;          // of those, within Params::tipAgreeMm
        double nearestTipMm = -1.0;

        // ---- #1556: THE CROSSING, MEASURED ACROSS THE BOUNDARY AND NAMED --------------
        //
        // Until this issue the wire question was `boundaryMm <= sigmaMajorMm`: the
        // distance to the nearest call-flipping boundary against the LONGEST axis of the
        // error ellipse, whichever way that axis happened to point. That is a real
        // demotion asked in the wrong direction -- a two-line solve at a shallow crossing
        // has a major axis tens of millimetres long ALONG the poorly-conditioned
        // direction, and a wire perpendicular to it was being called uncertain by an
        // uncertainty that does not point at it (measured on rig-20260918's own logs: a
        // solve with sigma 19.3 x 6.6 mm, and one on rig-20260922 at 55.8 x 7.8).
        //
        // The boundary that could flip a call has a direction. A ring wire is a circle,
        // so it is crossed RADIALLY; a sector wire is a radius, so it is crossed
        // TANGENTIALLY. The sigma that can spend the demotion is therefore the position
        // sigma resolved along that boundary's own normal -- floored at
        // Params::sigmaAcrossFloorMm, because resolving an ellipse can only ever shrink
        // the number and #1511's finding is that the claimed number is already the
        // optimistic one.
        //
        // ONLY THE STATISTICAL HALF SPENDS IT. The treble band's systematic segmentation
        // bloom is corrected per camera per edge before `boundaryMm` is measured at all
        // (#1553, board_model::trebleBloomAdjustMm), so `ringBoundaryMm` here is already a
        // distance to the CORRECTED edge and the systematic part is spent. What is left
        // in the covariance is the statistical scatter of the transported lines, which is
        // what this asks about.
        double sigmaRadialMm = -1.0;    // 1-sigma across a ring wire, floored
        double sigmaTangentMm = -1.0;   // 1-sigma across a sector wire, floored
        std::string boundaryKind;       // "ring" | "wedge"; empty where nothing can flip
        double boundaryAcrossMm = -1.0; // distance to THAT boundary
        double sigmaAcrossMm = -1.0;    // the sigma resolved across it, floored
        double crossingSigmas = -1.0;   // boundaryAcrossMm / sigmaAcrossMm; -1 undefined
        // What the board says on the OTHER side of that boundary, read back through the
        // same `scoreFromModel` and the same camera the published score was read through
        // -- never composed out of new ring or wedge arithmetic (#1512's rule). Empty
        // where no second candidate could be named, and `alternativeRefusal` says why.
        std::string alternativeScore;
        std::string alternativeRefusal;
        // The verdict. A dart is flagged only where a SECOND CANDIDATE COULD BE NAMED: a
        // demotion that cannot say what the other answer is has nothing for a consumer to
        // ask about, and #1557's decision is that the flag names both candidates.
        bool uncertaintyCrossesWire = false;
        // #1555's rule, kept and measured on the SAME dart in the same process, so the
        // two flag rates are a comparison rather than two runs: `boundaryMm` against the
        // major axis. `OD_WIRE_FLAG=sigma-major` publishes on this one instead.
        bool crudeWireClose = false;
    };

    /**
     * #1556's falsifier, in the od_fix shape #1339, #1348, #1495, #1518, #1552 and #1555
     * established: one binary, the rule chosen at run time, so the losing rule stays
     * reachable and measurable on a shipped board.
     *
     * `OD_WIRE_FLAG=sigma-major` publishes on #1555's crude test -- the nearest
     * call-flipping boundary against the LONGEST axis of the error ellipse, whichever
     * way that axis points -- exactly as every build between #1555 and this issue did.
     * Anything else, unset included, publishes on the across-boundary test. BOTH
     * verdicts are computed and reported on every solve either way; the pin moves only
     * which of them decides the outcome and therefore the published confidence.
     */
    inline bool crudeWireTestIsPinned()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_WIRE_FLAG");
            return e != nullptr && std::string(e) == "sigma-major";
        }();
        return v;
    }

    /**
     * #1556's MUTATION, on the shipping binary rather than in a patch: `OD_ENTRY_SIGMA=zero`
     * zeroes the solved position's uncertainty -- both axes of the ellipse AND the
     * measured floor. The prediction the census states before running it is that the flag
     * census then reads EMPTY on every fixture and in every window, because a call with
     * no uncertainty cannot have uncertainty that reaches a wire. It is a pin and nothing
     * reads it on an ordinary run.
     */
    inline bool entrySigmaIsZeroed()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_ENTRY_SIGMA");
            return e != nullptr && std::string(e) == "zero";
        }();
        return v;
    }

    namespace detail
    {
        inline std::string fmt(const char *pattern, double v)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), pattern, v);
            return buf;
        }

        /** A camera-frame board-mm point, carried into the canonical (sequence) frame. */
        inline cv::Point2f canonOf(const board_model::ModelAnchor &anchor, const cv::Point2f &mm)
        {
            const double r = std::sqrt((double)mm.x * mm.x + (double)mm.y * mm.y);
            const double theta = std::atan2((double)mm.y, (double)mm.x);
            const double phi = anchor.advance * (theta - anchor.theta20);
            return cv::Point2f((float)(r * std::cos(phi)), (float)(r * std::sin(phi)));
        }

        /** The canonical point, taken back into one camera's frame (mm, board angle). */
        inline void cameraFrameOf(const board_model::ModelAnchor &anchor, const cv::Point2f &canon,
                                  double &radiusMm, double &thetaCamera)
        {
            radiusMm = std::sqrt((double)canon.x * canon.x + (double)canon.y * canon.y);
            const double phi = std::atan2((double)canon.y, (double)canon.x);
            thetaCamera = anchor.theta20 + anchor.advance * phi;
        }

        /** Where the canonical point lands in one camera's image. */
        inline cv::Point2f imageOfCanonical(const board_model::BoardFit &fit,
                                            const board_model::ModelAnchor &anchor,
                                            const cv::Point2f &canon)
        {
            double r = 0.0, theta = 0.0;
            cameraFrameOf(anchor, canon, r, theta);
            return board_model::imageOfBoard(fit, r, theta);
        }

        /** The crossing angle of two lines given by unit normals, degrees in [0, 90]. */
        inline double crossingAngleDeg(const Constraint &a, const Constraint &b)
        {
            const double dot = std::fabs(a.nx * b.nx + a.ny * b.ny);
            return std::acos(std::min(1.0, dot)) * 180.0 / CV_PI;
        }

        /**
         * #1556: the one-sigma position uncertainty resolved ALONG one canonical
         * direction, from the error ellipse's two axes and its orientation. The ellipse
         * is the covariance's own: `sigmaThetaDeg` is the major axis's direction in the
         * canonical frame, so a direction at angle alpha to it reads
         * sqrt((s1 cos a)^2 + (s2 sin a)^2) -- which is s1 along the major axis, s2
         * across it, and the right number in between at every other angle. Pure, and
         * asserted against both axes and the circular case in i1556_flag_check.
         */
        inline double sigmaAlongDeg(double sigmaMajorMm, double sigmaMinorMm,
                                    double sigmaThetaDeg, double directionDeg)
        {
            const double a = (directionDeg - sigmaThetaDeg) * CV_PI / 180.0;
            const double ca = std::cos(a), sa = std::sin(a);
            return std::sqrt(sigmaMajorMm * sigmaMajorMm * ca * ca +
                             sigmaMinorMm * sigmaMinorMm * sa * sa);
        }

        /** What the board reads at one canonical point, through one camera's own fit. */
        inline std::string scoreAtCanonical(const board_model::BoardProfile &profile,
                                            const board_model::BoardFit &fit,
                                            const board_model::ModelAnchor &anchor,
                                            const cv::Point2f &canon)
        {
            const board_model::ModelScore ms =
                board_model::scoreFromModel(profile, fit, anchor,
                                            imageOfCanonical(fit, anchor, canon));
            return ms.valid ? ms.score : std::string();
        }
    }

    /** #1556: how far past a boundary a probe is placed, millimetres. Small against
     *  every band on a board -- the treble is 8 mm wide and the narrowest wedge a dart
     *  can be scored in spans 2.5 mm of arc at the outer bull's edge. */
    inline constexpr double kCrossingNudgeMm = 0.2;

    /**
     * #1556: THE OTHER CANDIDATE, NAMED BY RE-SCORING RATHER THAN BY ARITHMETIC.
     *
     * The entry is moved just past the boundary that could flip the call -- radially for
     * a ring wire, along the arc for a sector wire -- and the board is asked what it
     * reads THERE, through the same `scoreFromModel`, the same fit and the same anchor
     * the published score came through. So the second candidate is the same kind of
     * answer as the first: it carries #1553's per-camera bloom correction, this camera's
     * own millimetre scale and the anchor's own wedge walk, and no ring or wedge
     * arithmetic is written twice anywhere (#1512's rule, which is also why a stale
     * anchor cancels here instead of inventing a wedge).
     *
     * Both sides are probed because the distance alone does not say which way the nearest
     * boundary lies. Generically exactly one probe changes the score: the other lands the
     * same distance into open segment, where by construction there is no nearer boundary
     * to have crossed. Where NEITHER changes it, no second candidate can be named and the
     * empty string is the honest answer -- `solveEntry` then refuses to flag and says so.
     */
    inline std::string alternativeAcross(const board_model::BoardProfile &profile,
                                         const board_model::BoardFit &fit,
                                         const board_model::ModelAnchor &anchor,
                                         const cv::Point2f &entry, double distanceMm,
                                         bool radial, const std::string &published)
    {
        const double r = std::sqrt((double)entry.x * entry.x + (double)entry.y * entry.y);
        if (!(r > 0.0) || !(distanceMm >= 0.0))
        {
            return std::string();
        }
        const double step = distanceMm + kCrossingNudgeMm;
        for (int s = -1; s <= 1; s += 2)
        {
            cv::Point2f probe;
            if (radial)
            {
                const double rr = r + s * step;
                if (!(rr > 0.0))
                {
                    continue; // through the centre is not across this wire
                }
                probe = cv::Point2f((float)(entry.x * rr / r), (float)(entry.y * rr / r));
            }
            else
            {
                const double d = s * step / r; // arc length at this radius, as an angle
                const double c = std::cos(d), sn = std::sin(d);
                probe = cv::Point2f((float)(entry.x * c - entry.y * sn),
                                    (float)(entry.x * sn + entry.y * c));
            }
            const std::string word = detail::scoreAtCanonical(profile, fit, anchor, probe);
            if (!word.empty() && word != published)
            {
                return word;
            }
        }
        return std::string();
    }

    /**
     * One camera's evidence, transported. Every refusal is named where it happens, so
     * the TOO-FEW verdict can list exactly what each camera lacked. Tip evidence is
     * placed whenever the FRAME can be placed, valid axis or none: an occluded shaft
     * with a found tip still corroborates, and a refused axis does not delete a tip.
     */
    inline Constraint constraintFrom(const CameraEvidence &ev)
    {
        Constraint out;
        out.camera = ev.camera;
        out.offered = true;

        const bool framePlaceable = ev.fit != nullptr && ev.fit->planeBuilt &&
                                    ev.fit->geometryAccepted && ev.fit->unitPerMm > 0.0 &&
                                    ev.anchor.resolved;
        if (ev.fit == nullptr || !ev.fit->planeBuilt || !ev.fit->geometryAccepted ||
            !(ev.fit->unitPerMm > 0.0))
        {
            out.exclusion = "no accepted board fit, so nothing this camera saw can be "
                            "placed on the board";
        }
        else if (!ev.anchor.resolved)
        {
            out.exclusion = "rotation unresolved, so this camera's plane cannot be placed "
                            "in the shared numbered frame";
        }
        else if (!ev.axisValid)
        {
            out.exclusion = "no usable axis: " +
                            (ev.axisRefusal.empty() ? std::string("(unsaid)") : ev.axisRefusal);
        }

        if (framePlaceable && ev.tipFound)
        {
            const cv::Point2f tipCam = board_model::boardPointOf(*ev.fit, ev.tipImage);
            out.tipMm = detail::canonOf(ev.anchor, tipCam);
            out.tipRadiusMm = std::sqrt((double)out.tipMm.x * out.tipMm.x +
                                        (double)out.tipMm.y * out.tipMm.y);
            out.tipPlaced = true;
        }
        if (!out.exclusion.empty())
        {
            return out;
        }

        // The image line of the axis: through axisPoint along axisDir.
        // l = (a, b, c): a*x + b*y + c = 0 with (a,b) the normal.
        const double a = -ev.axisDir.y, b = ev.axisDir.x;
        const double cImg = (double)ev.axisDir.y * ev.axisPoint.x -
                            (double)ev.axisDir.x * ev.axisPoint.y;
        out.imagePoint = ev.axisPoint;
        out.imageDir = ev.axisDir;

        // Transport: l_plane = H^T * l_image (plane units), then plane units -> mm.
        const cv::Matx33d &H = ev.fit->plane.H;
        const cv::Vec3d lPlane(H(0, 0) * a + H(1, 0) * b + H(2, 0) * cImg,
                               H(0, 1) * a + H(1, 1) * b + H(2, 1) * cImg,
                               H(0, 2) * a + H(1, 2) * b + H(2, 2) * cImg);
        const double k = ev.fit->unitPerMm;
        double lnx = lPlane[0] * k, lny = lPlane[1] * k, lc = lPlane[2];
        const double norm = std::sqrt(lnx * lnx + lny * lny);
        if (!(norm > 0.0) || !std::isfinite(norm))
        {
            out.exclusion = "the transported line is degenerate (the image line passes "
                            "through this camera's vanishing line)";
            return out;
        }
        lnx /= norm;
        lny /= norm;
        lc /= norm;

        // Into the canonical frame: two points on the camera-frame mm line, carried
        // through the (isometric) anchor map, and the line rebuilt from them.
        const cv::Point2f foot((float)(-lc * lnx), (float)(-lc * lny));
        const cv::Point2f along((float)(foot.x - lny * 100.0), (float)(foot.y + lnx * 100.0));
        const cv::Point2f c0 = detail::canonOf(ev.anchor, foot);
        const cv::Point2f c1 = detail::canonOf(ev.anchor, along);
        double tx = c1.x - c0.x, ty = c1.y - c0.y;
        const double tlen = std::sqrt(tx * tx + ty * ty);
        if (!(tlen > 0.0))
        {
            out.exclusion = "the transported line is degenerate (zero length in the "
                            "canonical frame)";
            return out;
        }
        tx /= tlen;
        ty /= tlen;
        out.nx = -ty;
        out.ny = tx;
        out.c = -(out.nx * c0.x + out.ny * c0.y);
        out.tangentDeg = std::fmod(std::atan2(ty, tx) * 180.0 / CV_PI + 360.0, 180.0);

        const cv::Point2f axisCam = board_model::boardPointOf(*ev.fit, ev.axisPoint);
        out.axisPointMm = detail::canonOf(ev.anchor, axisCam);
        out.pxPerMm = ev.fit->pxPerMmAtCentre;
        out.usable = true;
        return out;
    }

    namespace detail
    {
        /** sigmaPerp at a point: floored direction sigma over the lever, plus the
         *  lateral floor, in quadrature. */
        inline void weighAt(Constraint &con, const cv::Point2f &at, const Params &p)
        {
            const double sigmaDir = std::max(con.sigmaDirDeg, p.sigmaDirFloorDeg);
            con.sigmaDirDeg = sigmaDir;
            const double tx = -con.ny, ty = con.nx;
            con.leverMm = std::fabs((at.x - con.axisPointMm.x) * tx +
                                    (at.y - con.axisPointMm.y) * ty);
            const double angular = std::tan(sigmaDir * CV_PI / 180.0) * con.leverMm;
            const double lateral = con.pxPerMm > 0.0 ? p.sigmaLateralPx / con.pxPerMm
                                                     : p.sigmaLateralPx;
            con.sigmaPerpMm = std::sqrt(angular * angular + lateral * lateral);
        }

        /** Weighted least squares over the usable, unexcluded constraints.
         *  Returns false when the 2x2 normal matrix has no usable inverse. */
        inline bool solveOnce(std::vector<Constraint> &cons, cv::Point2f &X, cv::Matx22d &cov)
        {
            double a11 = 0.0, a12 = 0.0, a22 = 0.0, b1 = 0.0, b2 = 0.0;
            for (const Constraint &con : cons)
            {
                if (!con.usable || con.excluded)
                {
                    continue;
                }
                const double w = 1.0 / std::max(1e-6, con.sigmaPerpMm * con.sigmaPerpMm);
                a11 += w * con.nx * con.nx;
                a12 += w * con.nx * con.ny;
                a22 += w * con.ny * con.ny;
                b1 -= w * con.nx * con.c;
                b2 -= w * con.ny * con.c;
            }
            const double det = a11 * a22 - a12 * a12;
            if (!(std::fabs(det) > 1e-12) || !std::isfinite(det))
            {
                return false;
            }
            X = cv::Point2f((float)((a22 * b1 - a12 * b2) / det),
                            (float)((a11 * b2 - a12 * b1) / det));
            cov = cv::Matx22d(a22 / det, -a12 / det, -a12 / det, a11 / det);
            return true;
        }
    }

    /**
     * The solve. Evidence in, one verdict out; nothing here reads a score string, a
     * confidence, or a vote -- which is the structural half of "no confidence increase
     * solely because two camera score strings agree" (#1505): agreement between strings
     * is not an input this function has.
     */
    inline EntrySolution solveEntry(const board_model::BoardProfile &profile,
                                    const std::vector<CameraEvidence> &evidence,
                                    const Params &params = Params())
    {
        EntrySolution out;
        out.offeredConstraints = (int)evidence.size();
        for (const CameraEvidence &ev : evidence)
        {
            Constraint con = constraintFrom(ev);
            con.sigmaDirDeg = ev.axisSigmaDeg;
            out.constraints.push_back(con);
        }

        std::vector<Constraint *> usable;
        for (Constraint &con : out.constraints)
        {
            if (con.usable)
            {
                usable.push_back(&con);
            }
        }

        auto exclusionsListed = [&]() -> std::string
        {
            std::string s;
            for (const Constraint &con : out.constraints)
            {
                if (con.usable && !con.excluded)
                {
                    continue;
                }
                s += (s.empty() ? "" : "; ") + std::string("cam ") +
                     std::to_string(con.camera + 1) + ": " + con.exclusion;
            }
            return s;
        };

        if ((int)usable.size() < 2)
        {
            out.outcome = Outcome::TooFewConstraints;
            out.story = "no entry: " + std::to_string(usable.size()) +
                        " usable constraint(s) of " + std::to_string(out.offeredConstraints) +
                        " cameras, and one line is anywhere along itself (" +
                        exclusionsListed() + ")";
            return out;
        }

        // Conditioning: the best crossing angle available among usable pairs.
        for (size_t i = 0; i < usable.size(); i++)
        {
            for (size_t j = i + 1; j < usable.size(); j++)
            {
                out.bestPairAngleDeg =
                    std::max(out.bestPairAngleDeg, detail::crossingAngleDeg(*usable[i], *usable[j]));
            }
        }
        if (out.bestPairAngleDeg < params.minPairAngleDeg)
        {
            out.outcome = Outcome::NearParallel;
            out.story = "near-parallel: the best crossing angle among " +
                        std::to_string(usable.size()) + " constraints is " +
                        detail::fmt("%.1f", out.bestPairAngleDeg) + " deg against a " +
                        detail::fmt("%.1f", params.minPairAngleDeg) +
                        " deg conditioning gate (perpendicular error amplifies as 1/sin)";
            return out;
        }

        // Solve equal-weighted first, then weigh at the solution and solve again,
        // twice: sigmaPerp depends on the lever from each axis point to the entry,
        // which is not known until an entry is. solveActive is that whole recipe over
        // whatever is not excluded, so the joint solve and every leave-one-out below
        // are one procedure.
        cv::Point2f X(0.f, 0.f);
        cv::Matx22d cov;

        // Consistency, judged as a chi-square over the joint residuals -- NOT as a
        // per-line sigma gate, and the difference was measured before it was chosen:
        // a line displaced 30 mm among two honest ones dilutes into ~10 mm joint
        // residuals against lever-inflated ~4 mm sigmas (every ratio under 3), while
        // the chi-square reads ~19 against Params::chi2PerDof's 9. And with three
        // lines the disagreement is SYMMETRIC -- the three pairwise intersections
        // form a triangle in which each vertex sits equally far from the opposite
        // line, so no residual arithmetic can say WHICH camera lies. What can is the
        // independent entry-point evidence: the liar is the one whose exclusion
        // leaves a solve the placed tips corroborate, uniquely, and where no tip
        // says so the verdict is INCONSISTENT, not a coin-flip exclusion.
        auto solveActive = [&](cv::Point2f &Xout, cv::Matx22d &covOut) -> bool
        {
            for (Constraint *con : usable)
            {
                if (!con->excluded)
                {
                    con->sigmaPerpMm = 1.0;
                }
            }
            if (!detail::solveOnce(out.constraints, Xout, covOut))
            {
                return false;
            }
            for (int pass = 0; pass < 2; pass++)
            {
                for (Constraint *con : usable)
                {
                    if (!con->excluded)
                    {
                        detail::weighAt(*con, Xout, params);
                    }
                }
                if (!detail::solveOnce(out.constraints, Xout, covOut))
                {
                    return false;
                }
            }
            return true;
        };
        auto chi2At = [&](const cv::Point2f &at) -> double
        {
            double sum = 0.0;
            for (Constraint *con : usable)
            {
                if (con->excluded)
                {
                    continue;
                }
                const double r = con->nx * at.x + con->ny * at.y + con->c;
                const double ratio = r / std::max(1e-6, con->sigmaPerpMm);
                sum += ratio * ratio;
            }
            return sum;
        };
        auto tipsWithin = [&](const cv::Point2f &at) -> int
        {
            int n = 0;
            for (const Constraint &con : out.constraints)
            {
                if (con.tipPlaced &&
                    std::sqrt((double)(con.tipMm.x - at.x) * (con.tipMm.x - at.x) +
                              (double)(con.tipMm.y - at.y) * (con.tipMm.y - at.y)) <= params.tipAgreeMm)
                {
                    n++;
                }
            }
            return n;
        };

        if (!solveActive(X, cov))
        {
            out.outcome = Outcome::NearParallel;
            out.story = "near-parallel: the weighted normal matrix is singular past the angle gate";
            return out;
        }

        int active = (int)usable.size();
        if (active >= 3)
        {
            const int dof = active - 2;
            const double chi2 = chi2At(X);
            if (chi2 > params.chi2PerDof * dof)
            {
                // Name the liar or refuse. For each constraint: solve without it,
                // demand the reduced pair still crosses, demand the left-out line is
                // genuinely REFUTED by that solve, and count the placed tips that
                // corroborate it. A unique tip-corroborated winner is excluded by
                // name; anything else is INCONSISTENT.
                Constraint *liar = nullptr;
                cv::Point2f bestX;
                cv::Matx22d bestCov;
                int bestTips = 0;
                bool tie = false;
                for (Constraint *cand : usable)
                {
                    cand->excluded = true;
                    double pairAngle = 0.0;
                    for (size_t i = 0; i < usable.size(); i++)
                    {
                        for (size_t j = i + 1; j < usable.size(); j++)
                        {
                            if (!usable[i]->excluded && !usable[j]->excluded)
                            {
                                pairAngle = std::max(pairAngle,
                                                     detail::crossingAngleDeg(*usable[i], *usable[j]));
                            }
                        }
                    }
                    cv::Point2f Xr;
                    cv::Matx22d covR;
                    if (pairAngle >= params.minPairAngleDeg && solveActive(Xr, covR))
                    {
                        detail::weighAt(*cand, Xr, params);
                        const double resid = std::fabs(cand->nx * Xr.x + cand->ny * Xr.y + cand->c);
                        const bool refuted = resid / std::max(1e-6, cand->sigmaPerpMm) >
                                             params.consistencySigmas;
                        const int tips = tipsWithin(Xr);
                        if (refuted && tips > 0)
                        {
                            if (liar == nullptr || tips > bestTips)
                            {
                                liar = cand;
                                bestTips = tips;
                                bestX = Xr;
                                bestCov = covR;
                                tie = false;
                            }
                            else if (tips == bestTips)
                            {
                                tie = true;
                            }
                        }
                    }
                    cand->excluded = false;
                }
                if (liar == nullptr || tie)
                {
                    std::string sizes;
                    for (Constraint *con : usable)
                    {
                        con->residualMm = con->nx * X.x + con->ny * X.y + con->c;
                        sizes += (sizes.empty() ? "" : ", ") + std::string("cam ") +
                                 std::to_string(con->camera + 1) + " residual " +
                                 detail::fmt("%.1f", std::fabs(con->residualMm)) + " mm vs sigma " +
                                 detail::fmt("%.1f", con->sigmaPerpMm);
                    }
                    out.outcome = Outcome::Inconsistent;
                    out.story = "inconsistent: the joint residuals read chi2 " +
                                detail::fmt("%.1f", chi2) + " against " +
                                detail::fmt("%.1f", params.chi2PerDof * dof) + " (" + sizes +
                                "), and " +
                                (tie ? "the tip evidence cannot choose between two exclusions"
                                     : "no tip evidence corroborates any two-camera solve") +
                                ", so no camera can be named the liar";
                    return out;
                }
                const double resid = std::fabs(liar->nx * bestX.x + liar->ny * bestX.y + liar->c);
                liar->excluded = true;
                liar->exclusion = "inconsistent with the other cameras: residual " +
                                  detail::fmt("%.1f", resid) + " mm against its own sigma " +
                                  detail::fmt("%.1f", liar->sigmaPerpMm) + " mm at the solve " +
                                  std::to_string(bestTips) + " placed tip(s) corroborate";
                // Re-solved once with the exclusion standing, so every kept
                // constraint's reported sigma and lever belong to THIS solve.
                if (!solveActive(X, cov))
                {
                    out.outcome = Outcome::NearParallel;
                    out.story = "near-parallel: singular after excluding cam " +
                                std::to_string(liar->camera + 1);
                    return out;
                }
                (void)bestX;
                (void)bestCov;
                active--;
                double pairAngle = 0.0;
                for (size_t i = 0; i < usable.size(); i++)
                {
                    for (size_t j = i + 1; j < usable.size(); j++)
                    {
                        if (!usable[i]->excluded && !usable[j]->excluded)
                        {
                            pairAngle = std::max(pairAngle, detail::crossingAngleDeg(*usable[i], *usable[j]));
                        }
                    }
                }
                out.bestPairAngleDeg = pairAngle;
            }
        }
        for (Constraint *con : usable)
        {
            // Recomputed against the FINAL solve for every transported line, the
            // excluded one included: its residual is the size of the disagreement a
            // reader is owed, at the point that was actually answered.
            con->residualMm = con->nx * X.x + con->ny * X.y + con->c;
        }
        out.usableConstraints = 0;
        for (const Constraint *con : usable)
        {
            if (!con->excluded)
            {
                out.usableConstraints++;
            }
        }

        // The error ellipse, from the weighted covariance.
        {
            const double tr = cov(0, 0) + cov(1, 1);
            const double dd = std::sqrt(std::max(0.0, (cov(0, 0) - cov(1, 1)) * (cov(0, 0) - cov(1, 1)) / 4.0 +
                                                          cov(0, 1) * cov(0, 1)));
            const double l1 = tr / 2.0 + dd, l2 = std::max(0.0, tr / 2.0 - dd);
            out.sigmaMajorMm = std::sqrt(std::max(0.0, l1));
            out.sigmaMinorMm = std::sqrt(l2);
            out.sigmaThetaDeg = 0.5 * std::atan2(2.0 * cov(0, 1), cov(0, 0) - cov(1, 1)) * 180.0 / CV_PI;
        }
        if (entrySigmaIsZeroed())
        {
            // #1556's mutation, applied where the uncertainty IS rather than where it is
            // read, so nothing downstream can route around it.
            out.sigmaMajorMm = 0.0;
            out.sigmaMinorMm = 0.0;
        }

        out.solved = true;
        out.entryMm = X;
        out.radiusMm = std::sqrt((double)X.x * X.x + (double)X.y * X.y);
        out.phiDeg = std::fmod(std::atan2((double)X.y, (double)X.x) * 180.0 / CV_PI + 360.0, 360.0);

        // Tip corroboration, over every camera whose frame placed a tip.
        for (Constraint &con : out.constraints)
        {
            if (!con.tipPlaced)
            {
                continue;
            }
            const double d = std::sqrt((double)(con.tipMm.x - X.x) * (con.tipMm.x - X.x) +
                                       (double)(con.tipMm.y - X.y) * (con.tipMm.y - X.y));
            con.tipDistanceMm = d;
            out.tipWitnesses++;
            if (d <= params.tipAgreeMm)
            {
                out.tipCorroborations++;
            }
            if (out.nearestTipMm < 0.0 || d < out.nearestTipMm)
            {
                out.nearestTipMm = d;
            }
        }

        // Scored ONCE, through scoreFromModel on the reference camera -- and read
        // through every placeable camera beside it, because two anchors disagreeing
        // about one point is a rig fact a reader must see.
        std::vector<std::string> words;
        // #1556: the reference camera's own fit and anchor, kept because the second
        // candidate has to be read back through THE SAME ONE the published score came
        // through. A second candidate read through a different camera's rulers would be
        // a different reading of a different board.
        const board_model::BoardFit *refFit = nullptr;
        board_model::ModelAnchor refAnchor;
        for (size_t i = 0; i < evidence.size(); i++)
        {
            const CameraEvidence &ev = evidence[i];
            Constraint &con = out.constraints[i];
            const bool placeable = ev.fit != nullptr && ev.fit->planeBuilt &&
                                   ev.fit->geometryAccepted && ev.fit->unitPerMm > 0.0 &&
                                   ev.anchor.resolved;
            if (!placeable)
            {
                out.scoreByCamera += (out.scoreByCamera.empty() ? "" : "/") + std::string("-");
                continue;
            }
            const cv::Point2f img = detail::imageOfCanonical(*ev.fit, ev.anchor, X);
            con.solvedImagePlaced = true;
            con.solvedImage = img;
            if (!(con.pxPerMm > 0.0))
            {
                con.pxPerMm = ev.fit->pxPerMmAtCentre;
            }
            const board_model::ModelScore ms = board_model::scoreFromModel(profile, *ev.fit, ev.anchor, img);
            out.scoreByCamera += (out.scoreByCamera.empty() ? "" : "/") + ms.score;
            words.push_back(ms.score);
            if (con.usable && !con.excluded && out.scoredThroughCamera < 0)
            {
                out.scoredThroughCamera = ev.camera;
                out.score = ms;
                refFit = ev.fit;
                refAnchor = ev.anchor;
            }
        }
        out.scoresAgree = !words.empty();
        for (const std::string &w : words)
        {
            if (w != words.front())
            {
                out.scoresAgree = false;
            }
        }

        // Image-space reprojection residual per used constraint: the solved point,
        // reprojected into that camera, against its observed image line.
        for (size_t i = 0; i < evidence.size(); i++)
        {
            Constraint &con = out.constraints[i];
            if (!con.usable)
            {
                continue;
            }
            const cv::Point2f img = detail::imageOfCanonical(*evidence[i].fit, evidence[i].anchor, X);
            const double dx = img.x - con.imagePoint.x, dy = img.y - con.imagePoint.y;
            con.residualPx = std::fabs(dx * con.imageDir.y - dy * con.imageDir.x);
        }

        // ---- #1556: THE WIRE QUESTION, ASKED ACROSS THE BOUNDARY AND ANSWERED WITH TWO
        //      CANDIDATE SCORES -------------------------------------------------------
        //
        // #1555's rule is kept and measured on the same dart (`crudeWireClose`) so the
        // two flag rates are one comparison in one process rather than two runs; the pin
        // decides which one publishes.
        out.crudeWireClose = out.score.valid && out.score.boundaryMm >= 0.0 &&
                             out.score.boundaryMm <= out.sigmaMajorMm;
        {
            const double floorMm = entrySigmaIsZeroed() ? 0.0 : params.sigmaAcrossFloorMm;
            out.sigmaRadialMm = std::max(
                detail::sigmaAlongDeg(out.sigmaMajorMm, out.sigmaMinorMm, out.sigmaThetaDeg,
                                      out.phiDeg),
                floorMm);
            out.sigmaTangentMm = std::max(
                detail::sigmaAlongDeg(out.sigmaMajorMm, out.sigmaMinorMm, out.sigmaThetaDeg,
                                      out.phiDeg + 90.0),
                floorMm);
            // WHICH boundaries could flip THIS call is `ModelScore::boundaryMm`'s own
            // rule, restated as the two it is a minimum of: a bull, an outer bull and a
            // miss are ring decisions with no wedge in them at all, so no sector wire can
            // change what they say however close it is. `segment >= 1` is set exactly
            // where the ring has a segment AND the anchor resolved it.
            const bool wedgeCanFlip = out.score.valid && out.score.segment >= 1 &&
                                      out.score.wedgeBoundaryMm >= 0.0;
            const bool ringCanFlip = out.score.valid && out.score.ringBoundaryMm >= 0.0;
            const double zRing = (ringCanFlip && out.sigmaRadialMm > 0.0)
                                     ? out.score.ringBoundaryMm / out.sigmaRadialMm
                                     : -1.0;
            const double zWedge = (wedgeCanFlip && out.sigmaTangentMm > 0.0)
                                      ? out.score.wedgeBoundaryMm / out.sigmaTangentMm
                                      : -1.0;
            // The crossing is the boundary this call is nearest to IN SIGMAS, not in
            // millimetres: a ring wire 3 mm away across a 2 mm sigma is a further call
            // than a sector wire 6 mm away across a 9 mm one.
            bool radial = true;
            double z = -1.0;
            if (zRing >= 0.0 && (zWedge < 0.0 || zRing <= zWedge))
            {
                z = zRing;
                radial = true;
            }
            else if (zWedge >= 0.0)
            {
                z = zWedge;
                radial = false;
            }
            if (z >= 0.0)
            {
                out.boundaryKind = radial ? "ring" : "wedge";
                out.boundaryAcrossMm = radial ? out.score.ringBoundaryMm
                                              : out.score.wedgeBoundaryMm;
                out.sigmaAcrossMm = radial ? out.sigmaRadialMm : out.sigmaTangentMm;
                out.crossingSigmas = z;
                if (z <= params.crossingSigmas)
                {
                    if (refFit == nullptr)
                    {
                        out.alternativeRefusal =
                            "no camera's fit could be kept to read the other side through";
                    }
                    else
                    {
                        out.alternativeScore =
                            alternativeAcross(profile, *refFit, refAnchor, X,
                                              out.boundaryAcrossMm, radial, out.score.score);
                        if (out.alternativeScore.empty())
                        {
                            out.alternativeRefusal =
                                "the board reads " + out.score.score +
                                " on both sides of the nearest boundary, so there is no "
                                "second candidate to name";
                        }
                    }
                }
            }
            else
            {
                out.alternativeRefusal = "nothing about this reading could be flipped by a "
                                         "wire, so no boundary was measured to";
            }
            // A DEMOTION THAT CANNOT NAME THE OTHER ANSWER IS NOT PUBLISHED AS ONE. The
            // maintainer's decision (#1557) is that a flagged dart publishes the more
            // probable candidate AND names the alternative a tap can pick; a flag with one
            // name in it is a confidence drop with nothing for a consumer to ask about,
            // which is the state this issue exists to replace.
            out.uncertaintyCrossesWire = !out.boundaryKind.empty() &&
                                         out.crossingSigmas <= params.crossingSigmas &&
                                         !out.alternativeScore.empty();
        }
        out.outcome = (crudeWireTestIsPinned() ? out.crudeWireClose : out.uncertaintyCrossesWire)
                          ? Outcome::UncertainAcrossWire
                          : Outcome::Solved;
        const bool wireClose = out.outcome == Outcome::UncertainAcrossWire;

        out.story = std::string(outcomeWord(out.outcome)) + ": entry (" +
                    detail::fmt("%.1f", X.x) + ", " + detail::fmt("%.1f", X.y) + ") mm, r " +
                    detail::fmt("%.1f", out.radiusMm) + ", " +
                    (out.score.valid ? out.score.score : std::string("(unscored)")) +
                    " through cam " + std::to_string(out.scoredThroughCamera + 1) +
                    ", sigma " + detail::fmt("%.1f", out.sigmaMajorMm) + "x" +
                    detail::fmt("%.1f", out.sigmaMinorMm) + " mm, boundary " +
                    detail::fmt("%.1f", out.score.boundaryMm) + " mm" +
                    // #1556: the crossing in the words a reader needs -- which wire, how
                    // far across it in its OWN sigma, and what the other answer is.
                    (out.boundaryKind.empty()
                         ? ""
                         : ", nearest " + out.boundaryKind + " wire " +
                               detail::fmt("%.1f", out.boundaryAcrossMm) + " mm away across a " +
                               detail::fmt("%.1f", out.sigmaAcrossMm) + " mm sigma (" +
                               detail::fmt("%.2f", out.crossingSigmas) + " sigma)") +
                    (!wireClose ? std::string()
                     : out.alternativeScore.empty()
                         ? std::string(" -- the sigma reaches a call-flipping wire")
                         : " -- the uncertainty reaches it, so this is " + out.score.score +
                               " or " + out.alternativeScore) +
                    (out.alternativeRefusal.empty() ? "" : " [" + out.alternativeRefusal + "]") +
                    ", pair angle " + detail::fmt("%.1f", out.bestPairAngleDeg) + " deg, tips " +
                    std::to_string(out.tipCorroborations) + "/" + std::to_string(out.tipWitnesses) +
                    " within " + detail::fmt("%.0f", params.tipAgreeMm) + " mm" +
                    (exclusionsListed().empty() ? "" : " (" + exclusionsListed() + ")");
        return out;
    }

    /** The event line the #1512 census parses. One per solve; refusal words last. */
    inline std::string censusEntryLine(const EntrySolution &sol, long window,
                                       const std::string &publishedScore, double publishedConfidence)
    {
        char head[512];
        snprintf(head, sizeof(head),
                 "I1512ENTRY window=%ld outcome=%s solved=%d x=%.2f y=%.2f r=%.2f phi=%.2f "
                 "sigma=%.2f/%.2f pair=%.1f score=%s boundary=%.2f ring=%.2f wedge=%.2f "
                 "byCam=%s agree=%d usable=%d offered=%d tips=%d/%d nearestTip=%.1f "
                 "published=%s conf=%.2f story=",
                 window, outcomeWord(sol.outcome), sol.solved ? 1 : 0,
                 sol.entryMm.x, sol.entryMm.y, sol.radiusMm, sol.phiDeg,
                 sol.sigmaMajorMm, sol.sigmaMinorMm, sol.bestPairAngleDeg,
                 sol.score.valid ? sol.score.score.c_str() : "NONE",
                 sol.score.boundaryMm, sol.score.ringBoundaryMm, sol.score.wedgeBoundaryMm,
                 sol.scoreByCamera.empty() ? "-" : sol.scoreByCamera.c_str(),
                 sol.scoresAgree ? 1 : 0, sol.usableConstraints, sol.offeredConstraints,
                 sol.tipCorroborations, sol.tipWitnesses, sol.nearestTipMm,
                 publishedScore.c_str(), publishedConfidence);
        return std::string(head) + sol.story;
    }

    /**
     * #1556: the crossing, in a shape testers/i1556_census.py parses -- one per solve,
     * printed beside the #1512 lines under the same census pin.
     *
     * A NEW LINE rather than more fields on `I1512ENTRY`, for the mechanical reason
     * #1555 wrote down one issue earlier and then had to obey: that line's parser matches
     * its whole head contiguously, so a field inserted anywhere before `story=` silently
     * stops every #1512 census reading anything. It carries BOTH verdicts on the same
     * dart -- `flag=` is the across-boundary rule and `crude=` is #1555's major-axis one
     * -- so the flag rates the census reports are a comparison made in one process, and
     * `published=` says which of the two the run actually demoted on.
     */
    inline std::string censusFlagLine(const EntrySolution &sol, long window)
    {
        char head[520];
        snprintf(head, sizeof(head),
                 "I1556FLAG window=%ld solved=%d score=%s alt=%s kind=%s boundary=%.2f "
                 "sigmaAcross=%.2f sigmaRadial=%.2f sigmaTangent=%.2f z=%.3f "
                 "sigmaMajor=%.2f sigmaMinor=%.2f sigmaTheta=%.1f r=%.2f phi=%.2f "
                 "flag=%d crude=%d published=%s refusal=",
                 window, sol.solved ? 1 : 0,
                 sol.score.valid ? sol.score.score.c_str() : "NONE",
                 sol.alternativeScore.empty() ? "-" : sol.alternativeScore.c_str(),
                 sol.boundaryKind.empty() ? "-" : sol.boundaryKind.c_str(),
                 sol.boundaryAcrossMm, sol.sigmaAcrossMm, sol.sigmaRadialMm,
                 sol.sigmaTangentMm, sol.crossingSigmas, sol.sigmaMajorMm, sol.sigmaMinorMm,
                 sol.sigmaThetaDeg, sol.radiusMm, sol.phiDeg,
                 sol.uncertaintyCrossesWire ? 1 : 0, sol.crudeWireClose ? 1 : 0,
                 crudeWireTestIsPinned() ? "crude" : "across");
        return std::string(head) +
               (sol.alternativeRefusal.empty() ? "-" : sol.alternativeRefusal);
    }

    /** One line per offered camera; exclusion words last. */
    inline std::string censusCameraLine(const Constraint &con, long window)
    {
        char head[440];
        snprintf(head, sizeof(head),
                 "I1512CAM window=%ld cam=%d usable=%d excluded=%d tangent=%.1f "
                 "resid_mm=%.2f resid_px=%.2f sigmaPerp=%.2f lever=%.1f sigmaDir=%.2f "
                 "img=(%.1f,%.1f) pxmm=%.3f tipPlaced=%d tipDist=%.1f tipR=%.1f excl=",
                 window, con.camera + 1, con.usable ? 1 : 0, con.excluded ? 1 : 0,
                 con.tangentDeg, con.residualMm, con.residualPx, con.sigmaPerpMm,
                 con.leverMm, con.sigmaDirDeg,
                 con.solvedImagePlaced ? con.solvedImage.x : -1.f,
                 con.solvedImagePlaced ? con.solvedImage.y : -1.f, con.pxPerMm,
                 con.tipPlaced ? 1 : 0, con.tipDistanceMm, con.tipRadiusMm);
        return std::string(head) +
               ((con.usable && !con.excluded) ? "-" : con.exclusion);
    }

    /**
     * The acceptance's overlay, one camera's view: the observed axis line (yellow),
     * the solved entry reprojected (green cross, red when refused), the one-sigma
     * circle at this camera's scale, and the words -- support, residual, uncertainty,
     * exclusion -- a reader without the log needs.
     */
    inline void drawEntryOverlay(cv::Mat &canvas, const CameraEvidence &ev,
                                 const EntrySolution &sol, const Constraint &con)
    {
        if (canvas.empty())
        {
            return;
        }
        if (con.usable)
        {
            const cv::Point2f p = con.imagePoint, d = con.imageDir;
            cv::line(canvas, cv::Point((int)(p.x - d.x * 400), (int)(p.y - d.y * 400)),
                     cv::Point((int)(p.x + d.x * 400), (int)(p.y + d.y * 400)),
                     con.excluded ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 255), 1);
        }
        const bool placeable = ev.fit != nullptr && ev.fit->planeBuilt &&
                               ev.fit->geometryAccepted && ev.fit->unitPerMm > 0.0 &&
                               ev.anchor.resolved;
        if (sol.solved && placeable)
        {
            const cv::Point2f img = detail::imageOfCanonical(*ev.fit, ev.anchor, sol.entryMm);
            const cv::Scalar colour = sol.outcome == Outcome::Solved ? cv::Scalar(0, 220, 0)
                                                                     : cv::Scalar(0, 160, 255);
            cv::drawMarker(canvas, cv::Point((int)img.x, (int)img.y), colour,
                           cv::MARKER_CROSS, 18, 2);
            const double rPx = sol.sigmaMajorMm * (con.pxPerMm > 0.0 ? con.pxPerMm
                                                                     : ev.fit->pxPerMmAtCentre);
            if (rPx > 0.5 && rPx < 500)
            {
                cv::circle(canvas, cv::Point((int)img.x, (int)img.y), (int)std::lround(rPx),
                           colour, 1);
            }
        }
        if (ev.tipFound)
        {
            cv::circle(canvas, ev.tipImage, 7, cv::Scalar(255, 0, 0), 2);
        }
        const std::string caption =
            std::string(outcomeWord(sol.outcome)) + " " +
            (sol.score.valid ? sol.score.score : std::string("-")) +
            // #1556: the acceptance's own picture says both candidates, because a reader
            // looking at an overlay is exactly the reader the flag is for.
            (sol.alternativeScore.empty() ? std::string() : " or " + sol.alternativeScore) +
            " | cam " + std::to_string(con.camera + 1) + " " +
            ((con.usable && !con.excluded)
                 ? "used, resid " + detail::fmt("%.1f", std::fabs(con.residualMm)) + " mm (" +
                       detail::fmt("%.1f", con.residualPx) + " px), sigma " +
                       detail::fmt("%.1f", con.sigmaPerpMm) + " mm"
                 : "not used: " + con.exclusion);
        cv::putText(canvas, caption.substr(0, 110), cv::Point(10, 22), cv::FONT_HERSHEY_SIMPLEX,
                    0.5, sol.solved ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 1);
        if (sol.solved)
        {
            cv::putText(canvas,
                        ("entry (" + detail::fmt("%.1f", sol.entryMm.x) + "," +
                         detail::fmt("%.1f", sol.entryMm.y) + ") mm sigma " +
                         detail::fmt("%.1f", sol.sigmaMajorMm) + " mm boundary " +
                         detail::fmt("%.1f", sol.score.boundaryMm) + " mm")
                            .substr(0, 110),
                        cv::Point(10, 44), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 0), 1);
        }
    }
}

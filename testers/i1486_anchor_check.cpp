// #1486: the anchor a camera did not measure, held to the decisions it claims to make.
//
// The pure half of the slice, on its own -- no detector, no container-length run, no
// footage. `orientation_processing.hpp` carries the derivation as free functions over a
// plain struct precisely so that this file can ask it things directly (#1338's shape),
// and `testers/i1486_run.sh` runs this against the tree AND against five planted
// mutations of it, so what each assertion is worth is measured rather than asserted.
//
// The subject is the arithmetic and the refusal, never a number some fixture produced.

#include "../src/detector/geometry/calibration/orientation_processing.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace orientation_processing;

static int failures = 0;
static int checks = 0;

static void ok(bool condition, const std::string &what)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", what.c_str());
    }
    else
    {
        printf("ok   %s\n", what.c_str());
    }
}

static WedgeSighting seen(int slot, float fraction)
{
    WedgeSighting s;
    s.present = true;
    s.wireSlot = slot;
    s.fraction = fraction;
    return s;
}

int main()
{
    const int wires = 20;

    // ---- 1. the offset, and that it is the rotation between two rings -------------------
    //
    // A leader whose wedge 20 starts at wire 3 and that placed the tip between wires 7 and
    // 8 read the FIFTH wedge of the sequence (7 - 3). A follower that placed the same tip
    // between its own wires 11 and 12 must therefore have its wedge 20 at wire 11 - 4 = 7.
    {
        const AnchorSighting a = anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires);
        ok(a.usable, "an offset exists when both cameras placed the tip");
        ok(a.wedge20WireIndex == 7, "the follower's wedge 20 is the leader's sequence slot subtracted from its own");
        ok(std::fabs(a.residualWedges) < 1e-9, "two cameras that place the tip identically leave no residual");
    }

    // The ring wraps, and a derivation that forgot to would answer a negative wire.
    {
        const AnchorSighting a = anchorFromOneDart(seen(2, 0.5f), 0, seen(0, 0.5f), wires);
        ok(a.wedge20WireIndex == 18, "the offset wraps round the ring rather than going negative");
    }

    // The fraction across the wedge is part of the offset, not decoration. A residual is
    // a distance to the NEAREST whole wedge, so half a wedge is the largest there is: two
    // cameras that place the tip half a wedge apart have reached the point where the
    // derived wire is as near its neighbour as to itself.
    {
        const AnchorSighting a = anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.0f), wires);
        ok(a.residualWedges > 0.49, "a tip placed half a wedge apart leaves the largest residual there is");
        const AnchorSighting b = anchorFromOneDart(seen(7, 0.05f), 3, seen(11, 0.95f), wires);
        ok(b.residualWedges > 0.05 && b.residualWedges < 0.2,
           "and the fractions move the offset rather than being dropped");
    }

    // ---- 2. what is believed, and what is not -------------------------------------------
    {
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires), 1);
        ok(!anchor.trusted, "one dart is a statement with nothing able to contradict it, so it is not trusted");
        ok(anchor.agreeing == 1, "and it is counted");
        noteAnchorSighting(anchor, anchorFromOneDart(seen(12, 0.2f), 3, seen(16, 0.2f), wires), 1);
        ok(anchor.trusted, "a second dart agreeing on the same wire is what makes it readable");
        ok(anchor.wedge20WireIndex == 7, "and the wire is the one both darts named");
        ok(anchor.leader == 1, "the camera it was derived from is remembered");
    }

    // A residual past the cut is not a vote. The two cameras have disagreed about where
    // this tip is, which says nothing about the rig.
    {
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.1f), 3, seen(11, 0.6f), wires), 1);
        ok(anchor.agreeing == 0, "a sighting past the residual cut casts no vote");
        ok(anchor.discarded == 1, "and is counted as discarded rather than silently dropped");
        ok(!anchor.trusted, "so nothing is trusted on the strength of it");
    }

    // ---- 3. the refusal, which is this issue's own acceptance criterion ------------------
    //
    // A confidently wrong second opinion is worse than an abstention (#1451). A dart that
    // names a different wire must not replace the index, must not average with it, and
    // must not be ignored in favour of the first answer: it refuses the camera outright.
    {
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires), 1);
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(13, 0.5f), wires), 1);
        ok(anchor.refused, "two darts naming different wires refuse the camera");
        ok(!anchor.trusted, "a refused camera is never read");
    }

    // ... and the refusal outlives the disagreement: a third and fourth dart agreeing with
    // each other cannot talk the camera back into being read.
    {
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires), 1);
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(13, 0.5f), wires), 1);
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(13, 0.5f), wires), 1);
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(13, 0.5f), wires), 1);
        ok(anchor.refused && !anchor.trusted, "a refusal is for the rest of the run");
    }

    // A disagreement AFTER the camera was trusted refuses it too -- the reading stops,
    // rather than the board going on publishing the index that has been contradicted.
    {
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires), 1);
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(11, 0.5f), wires), 1);
        ok(anchor.trusted, "trusted first");
        noteAnchorSighting(anchor, anchorFromOneDart(seen(7, 0.5f), 3, seen(14, 0.5f), wires), 1);
        ok(anchor.refused && !anchor.trusted, "and refused when a later dart contradicts it");
    }

    // ---- 4. nothing to derive from is nothing derived ------------------------------------
    {
        WedgeSighting absent;
        ok(!anchorFromOneDart(absent, 3, seen(11, 0.5f), wires).usable, "a leader that placed no tip derives nothing");
        ok(!anchorFromOneDart(seen(7, 0.5f), 3, absent, wires).usable, "nor does a follower that placed none");
        ok(!anchorFromOneDart(seen(7, 0.5f), -1, seen(11, 0.5f), wires).usable, "nor a leader with no wedge 20 of its own");
        DerivedAnchor anchor;
        noteAnchorSighting(anchor, AnchorSighting(), 1);
        ok(!anchor.trusted && anchor.agreeing == 0, "an unusable sighting changes nothing");
    }

    // ---- 5. the words a reader of the log gets -------------------------------------------
    {
        DerivedAnchor anchor;
        anchor.refused = true;
        ok(howItDerived(1, anchor).find("REFUSED") != std::string::npos,
           "a refused camera says so in the log rather than going quiet");
        DerivedAnchor trusted;
        trusted.trusted = true;
        trusted.wedge20WireIndex = 7;
        trusted.leader = 0;
        trusted.agreeing = 2;
        ok(howItDerived(1, trusted).find("wire 7") != std::string::npos &&
               howItDerived(1, trusted).find("camera 1") != std::string::npos,
           "a derived camera names the wire and the camera it came from");
    }

    printf("CHECKS=%d FAILURES=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// turnaus#1994: OD_FUSE -- a camera's placed tip as a weak point constraint in the
// geometric solve. Pure: planted boards, planted lines, planted tips; no footage.
//
// The planted cameras and the evidence builders are i1512_intersect_check.cpp's, included
// with its main renamed, so the two checks cannot drift apart on what a camera is.
//
// Every assertion carries a label (`unset:` `parse:` `enter:` `weight:` `check:` `floor:`
// `oneline:` `tips:` `lone:` `line:` `all:` `account:`), and testers/i1994_check.sh predicts,
// per label, how many turn red under each mutation of entry_intersection.hpp.
#define main i1512_main
#include "i1512_intersect_check.cpp"
#undef main

static void label(bool ok, const std::string &tag, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << tag << ": " << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

int main()
{
    failures = 0;
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const Camera cam1 = makeCamera(640, 360, 300, 240, 25.0, 0.18, 0.7, 0.13, false);
    const Camera cam2 = makeCamera(590, 410, 270, 250, -40.0, 0.12, 2.1, 0.47, false);
    const Camera cam3 = makeCamera(700, 330, 280, 230, 110.0, 0.20, -1.2, 0.90, true);

    Params off;
    Params on;
    on.fuseTips = true;
    Params every = on;
    every.fuseEveryTip = true;

    const cv::Point2f C(40.f, 85.f);
    const cv::Point2f tipNear(46.f, 85.f); // 6 mm from the lines' crossing
    const cv::Point2f tipFar(70.f, 85.f);  // 30 mm from it

    // ---- the switch's words ---------------------------------------------------------------
    label(fuseModeFrom(nullptr) == FuseMode::Off && fuseModeFrom("") == FuseMode::Off &&
              fuseModeFrom("1") == FuseMode::Off && fuseModeFrom("off") == FuseMode::Off,
          "parse", "unset, empty, `1` and `off` are off");
    label(fuseModeFrom("on") == FuseMode::Spare && fuseModeFrom("all") == FuseMode::Every,
          "parse", "`on` is a tip where the camera has no line, `all` every tip");
    label(!Params().fuseTips && !Params().fuseEveryTip, "unset", "a default Params fuses nothing");

    // ---- two lines and the third camera's tip ---------------------------------------------
    auto twoLinesAndATip = [&](cv::Point2f tip)
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          refusedEvidence(1, cam2, "not straight: planted"),
                                          lineEvidence(2, cam3, C, 80.0)};
        plantTip(ev[1], cam2, tip);
        return ev;
    };
    const EntrySolution base = solveEntry(profile, twoLinesAndATip(tipNear), off);
    const EntrySolution fused = solveEntry(profile, twoLinesAndATip(tipNear), on);
    label(base.solved && base.tipsEntered == 0 && !base.fuseAsked && distMm(base.entryMm, C) < 1.5 &&
              !base.constraints[1].tipEntered,
          "unset", "off, the two lines solve alone and no tip enters (" + fmt1(distMm(base.entryMm, C)) + " mm off the plant)");
    label(fusionAccount(base).empty(), "unset", "off, no FUSED sentence is written");
    label(fused.solved && fused.tipsEntered == 1 && fused.constraints[1].tipEntered &&
              fused.usableConstraints == 2,
          "enter", "on, the third camera's tip enters beside the two lines");
    const double shift = distMm(fused.entryMm, base.entryMm);
    const double towards = distMm(base.entryMm, tipNear) - distMm(fused.entryMm, tipNear);
    label(shift > 0.5 && towards > 0.5, "enter",
          "and it pulls the entry towards itself: " + fmt1(shift) + " mm moved, " + fmt1(towards) + " mm nearer the tip");
    label(shift < 3.0, "weight",
          "weakly -- under half of the 6 mm between them: " + fmt1(shift) + " mm (want < 3.0)");
    Params tight = on;
    tight.tipSigmaMm = 2.0;
    const EntrySolution fusedTight = solveEntry(profile, twoLinesAndATip(tipNear), tight);
    const double shiftTight = distMm(fusedTight.entryMm, base.entryMm);
    label(shiftTight > shift + 0.5, "weight",
          "a tip claimed at 2.0 mm pulls harder than one at 7.5: " + fmt1(shiftTight) + " mm against " + fmt1(shift));
    label(base.minRedundancy >= 0.0 && base.minRedundancy < 0.01 && fused.minRedundancy > 0.05,
          "check", "the tip is the two-line solve's check: redundancy " + fmt1(base.minRedundancy) +
                       " alone, " + fmt1(fused.minRedundancy) + " fused");
    label(fused.sigmaMajorMm < base.sigmaMajorMm, "check",
          "and the sigma is the fused solve's own: " + fmt1(fused.sigmaMajorMm) + " mm against " + fmt1(base.sigmaMajorMm));

    // ---- the floor: a tip that disagrees is refused, by name, and nothing moves ------------
    const EntrySolution far = solveEntry(profile, twoLinesAndATip(tipFar), on);
    label(far.solved && far.tipsEntered == 0 && distMm(far.entryMm, base.entryMm) < 0.001, "floor",
          "a tip 30 mm from the lines' entry does not enter and the entry is the lines' own");
    label(far.constraints[1].tipRefusal.find("from the lines' entry, past 15") != std::string::npos, "floor",
          "and it is refused by name: " + far.constraints[1].tipRefusal);
    const EntrySolution rim = solveEntry(profile, twoLinesAndATip(cv::Point2f(0.f, 285.f)), on);
    label(rim.tipsEntered == 0 && rim.constraints[1].tipRefusal.find("off the board") != std::string::npos, "floor",
          "a tip at r 285 mm is the flight's end, not a tip: " + rim.constraints[1].tipRefusal);

    // ---- a camera that gave a line does not also give its tip, unless `all` ----------------
    auto threeLinesOneTip = [&]()
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          lineEvidence(1, cam2, C, 70.0),
                                          lineEvidence(2, cam3, C, 130.0)};
        plantTip(ev[1], cam2, tipNear);
        return ev;
    };
    const EntrySolution l3off = solveEntry(profile, threeLinesOneTip(), off);
    const EntrySolution l3on = solveEntry(profile, threeLinesOneTip(), on);
    const EntrySolution l3all = solveEntry(profile, threeLinesOneTip(), every);
    label(l3on.tipsEntered == 0 && distMm(l3on.entryMm, l3off.entryMm) < 0.001, "line",
          "on, a camera with a line keeps its tip out and three lines solve as they always did");
    label(l3all.tipsEntered == 1 && distMm(l3all.entryMm, l3off.entryMm) > 0.3, "all",
          "all, the same tip enters and moves the entry " + fmt1(distMm(l3all.entryMm, l3off.entryMm)) + " mm");

    // ---- one line and another camera's tip --------------------------------------------------
    auto oneLineAndATip = [&](cv::Point2f tip)
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          refusedEvidence(1, cam2, "not straight: planted"),
                                          refusedEvidence(2, cam3, "rim only: planted")};
        plantTip(ev[1], cam2, tip);
        return ev;
    };
    const double psi = 10.0 * CV_PI / 180.0;
    const cv::Point2f offLine5((float)(C.x - 5.0 * std::sin(psi)), (float)(C.y + 5.0 * std::cos(psi)));
    const cv::Point2f offLine30((float)(C.x - 30.0 * std::sin(psi)), (float)(C.y + 30.0 * std::cos(psi)));
    const EntrySolution one0 = solveEntry(profile, oneLineAndATip(offLine5), off);
    const EntrySolution one = solveEntry(profile, oneLineAndATip(offLine5), on);
    const EntrySolution oneFar = solveEntry(profile, oneLineAndATip(offLine30), on);
    label(one0.outcome == Outcome::TooFewConstraints && !one0.solved, "unset",
          "off, one line and a tip is TOO-FEW-CONSTRAINTS as it always was");
    label(one.solved && one.fusedWithoutCrossing && one.usableConstraints == 1 && one.tipsEntered == 1, "oneline",
          std::string("on, one line and a tip 5 mm off it give an entry (got ") + outcomeWord(one.outcome) + ")");
    label(one.solved && distMm(one.entryMm, C) < 2.5 && one.score.valid && one.scoredThroughCamera == 0, "weight",
          "nearer the line than the tip (the line is the stronger claim), scored through the line's camera: " +
              fmt1(distMm(one.entryMm, C)) + " mm from the plant (want < 2.5)");
    label(!oneFar.solved && oneFar.outcome == Outcome::TooFewConstraints && oneFar.story == one0.story, "floor",
          "a tip 30 mm off the line gives none, and the refusal is today's word for word");

    // ---- no line: two tips that agree, two that do not, and one alone -----------------------
    auto tipsOnly = [&](bool second, cv::Point2f b)
    {
        std::vector<CameraEvidence> ev = {refusedEvidence(0, cam1, "not straight: planted"),
                                          refusedEvidence(1, cam2, "not straight: planted"),
                                          refusedEvidence(2, cam3, "rim only: planted")};
        plantTip(ev[0], cam1, C);
        if (second)
        {
            plantTip(ev[1], cam2, b);
        }
        return ev;
    };
    const cv::Point2f mid(44.f, 85.f);
    const EntrySolution two = solveEntry(profile, tipsOnly(true, cv::Point2f(48.f, 85.f)), on);
    label(two.solved && two.tipsEntered == 2 && two.usableConstraints == 0 && two.score.valid, "tips",
          std::string("two tips 8 mm apart and no line give an entry (got ") + outcomeWord(two.outcome) + ")");
    label(two.solved && distMm(two.entryMm, mid) < 0.5, "tips",
          "at their midpoint: " + fmt1(distMm(two.entryMm, mid)) + " mm off it");
    const EntrySolution apart = solveEntry(profile, tipsOnly(true, cv::Point2f(70.f, 85.f)), on);
    label(!apart.solved && apart.fuseRefusal.find("apart") != std::string::npos, "floor",
          "two tips 30 mm apart place nothing together: " + apart.fuseRefusal);
    const EntrySolution lone = solveEntry(profile, tipsOnly(false, C), on);
    label(!lone.solved && lone.outcome == Outcome::TooFewConstraints && lone.tipsEntered == 0, "lone",
          "one tip alone is one camera's reading: no entry, so the vote publishes it flagged as today");

    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          refusedEvidence(1, cam2, "rim only: planted"),
                                          refusedEvidence(2, cam3, "rim only: planted")};
        plantTip(ev[0], cam1, C);
        const EntrySolution own = solveEntry(profile, ev, every);
        label(!own.solved && own.outcome == Outcome::TooFewConstraints && own.tipsEntered == 0, "lone",
              "all, one camera's line and its own tip are still one camera's reading: no entry");
    }

    // ---- the account ----------------------------------------------------------------------
    const std::string said = fusionAccount(fused);
    label(said.find("FUSED: ") == 0 && said.find("cam 1 line sigma ") != std::string::npos &&
              said.find("cam 3 line sigma ") != std::string::npos &&
              said.find("cam 2 tip sigma 7.5 mm") != std::string::npos && said.find("moved the lines' entry") != std::string::npos,
          "account", "names every constraint and its weight: " + said);
    label(fusionAccount(far).find("cam 2 tip refused: 30.") != std::string::npos, "account",
          "and a refused tip with its reason: " + fusionAccount(far));
    label(sigmaProvenance(fused).find("2 line(s) and 1 camera tip(s)") != std::string::npos, "account",
          "the UNCERTAINTY provenance says the sigma is a fused one: " + sigmaProvenance(fused));
    label(censusFuseLine(fused, 7).find("I1994FUSE window=7 solved=1 lines=2 tips=1 ") == 0, "account",
          "the census line: " + censusFuseLine(fused, 7));

    std::cout << (failures == 0 ? "I1994 FUSE CHECK PASS" : "I1994 FUSE CHECK FAIL") << " (" << failures << " failure(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

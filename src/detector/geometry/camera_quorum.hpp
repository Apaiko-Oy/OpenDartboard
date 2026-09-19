#pragma once

#include <cstdlib>
#include <string>
#include <vector>

// #1389 / ADR-0081: how many cameras a board must have before it may score. One number,
// one home, read by every site that counts cameras.
//
// THE DEFECT THIS FILE EXISTS TO REMOVE. Three quorums existed with three values, each
// set by a different slice and none of them aware of the others:
//
//   cameras_that_must_see = 1   calibration admission (#1318), in geometry_detector.cpp
//   min_cameras_for_event = 1   the dart event's board census (#1353), motion_processing
//   an absolute 2               the state vote's floor, dart_processing
//
// The consequence is ADR-0081 §1 and it was measured on this binary before this file
// existed: a one-camera board is ADMITTED ("Initial calibration completed successfully on
// 1 of 3 cameras", "Scorer running with 1 of 3 cameras"), it opens dart windows -- 29 of
// them in 75 seconds of the shipped mocks -- and every one of them ends "so it stays
// CLEAN", because `goes_clean >= 2` and `moves_up >= 2` cannot be reached by one voter.
// It is not a board that scores badly. It is INERT, and it reports itself healthy, which
// is ADR-0055's exact nightmare.
//
// WHY TWO, AND WHY THAT IS NOT A NEW POLICY. The state vote has required two voters all
// along (#1348 made the number a majority of the voters with two as its floor). So two is
// not a tolerance somebody chose here; it is the arithmetic the rest of the detector
// already enforces, and every other quorum is now made to say it. Below it a board is
// arithmetically unable to advance its own state, so it can neither call a dart nor see
// one taken out.
//
// WHY THREE IS STILL THE EXPECTATION. ADR-0081 §1: three cameras is what the rig IS --
// fixed to a frame bolted to the wall, with the board in its own fixed place (ADR-0080,
// ADR-0079 §3) -- so a board calibrating on two has a fault somewhere and the floor is a
// safety net rather than a target. ADR-0081 §4 makes the floor of two PROVISIONAL on a
// measurement nobody has taken: whether two cameras score as accurately as three. If that
// measurement comes back badly the floor becomes three, and this constant is the only
// thing that has to move -- which is the whole reason it is one constant.
//
// WHAT IS DELIBERATELY NOT HERE. `MotionParams::min_cameras_for_event` is NOT this number
// and is not read from here. It looks like a camera quorum and it is not one: inside
// `whyNoEventIsPossible` it is a census of the cameras a board HAS, and at the runtime
// gate in processMotion it is how many cameras must SPIKE TOGETHER inside one window --
// two different questions that shared one integer, which is the same defect one stage
// down. #1353 measured the second on the rig (a throw's weak-side camera reads
// 0.0002-0.008 of its board, under any threshold that clears noise, and which camera is
// the strong side varies per throw), so a dart splash is a ONE-camera motion fact and
// that trigger stays at 1. The census half is what reads this file. Raising the spike
// trigger to two would stop a HEALTHY three-camera board scoring at all, which is worse
// than the failure ADR-0081 is about.
namespace camera_quorum
{
    /**
     * Two. The floor, and the reason is the state vote: `stateVoteQuorum` never returns
     * fewer than this, so a board with fewer cameras than this able to vote can never
     * move its own state.
     *
     * This is the only place this number is written. `testers/i1389_quorum_census.py`
     * refuses a second one.
     */
    inline constexpr int kCameras = 2;

    /**
     * The quorum this run is using.
     *
     * OD_CAMERA_QUORUM=<n> moves it, in the falsification shape #1339's
     * `OD_MOTION_DENOMINATOR`, #1358's `OD_DART_WINDOW` and #1348's `OD_STATE_QUORUM`
     * established: one binary, the rule chosen at run time, so "a different build" is
     * never a confound when a tester shows the answer moving with the number. A gate
     * nothing can be made to fail is not evidence that anything was gated.
     *
     * Read once for the life of the process, because every site that reads it must read
     * the same answer and a board whose floor changed halfway through a run would be a
     * worse thing than any of them.
     */
    inline int cameras()
    {
        static const int chosen = []
        {
            const char *e = std::getenv("OD_CAMERA_QUORUM");
            if (e == nullptr || *e == '\0')
            {
                return kCameras;
            }
            const int n = std::atoi(e);
            return n > 0 ? n : kCameras;
        }();
        return chosen;
    }

    /**
     * ADR-0081 §3: each camera and its reason, never just the count.
     *
     * "The count is the less useful half. The per-camera reason is what somebody can act
     * on: `camera 2: NoFrame` sends them to the USB bus and to #1319; `camera 2:
     * BoardClipped` is #1331. A board reporting 'scoring on two of three' and nothing
     * else has told nobody anything."
     *
     * `reasons[i]` is why camera i+1 cannot vote, in that camera's own slot, or an empty
     * string when it can. The words are `board_look::refusal`'s and are not retyped here
     * -- this file deliberately does not include that header, so the refusal vocabulary
     * has exactly one author (board_look.hpp, #1318, and #1392 after it) and this
     * function has no opinion about what a camera may be refused for.
     */
    inline std::string namingEachCamera(const std::vector<std::string> &reasons)
    {
        std::string out;
        for (std::size_t i = 0; i < reasons.size(); i++)
        {
            out += out.empty() ? "" : "; ";
            out += "camera " + std::to_string(i + 1) + ": " +
                   (reasons[i].empty() ? std::string("sees the dartboard and can vote")
                                       : reasons[i]);
        }
        return out;
    }

    /**
     * Why this board has too few cameras looking at the dartboard to score, or an empty
     * string when it has enough.
     *
     * This is calibration admission's own sentence. It is the one that used to be
     * `seeing >= cameras_that_must_see` with the constant at 1, where it decided nothing
     * and said nothing: #1348's NOTE in geometry_detector.cpp says so out loud -- "with
     * the vote's floor at 2, `cameras_that_must_see = 1` no longer decides anything by
     * itself". Making it the floor is what this issue calls making the admission gate say
     * what the rest of the code already enforces.
     *
     * #1321's rule on the sentence: the count is stated against the threshold it fell
     * short of, so a line reporting the wrong number can be seen to be wrong.
     */
    inline std::string whyTooFewCamerasSee(int camera_slots, int cameras_that_see,
                                           int quorum = cameras())
    {
        if (cameras_that_see >= quorum)
        {
            return "";
        }
        return "only " + std::to_string(cameras_that_see) + " of " +
               std::to_string(camera_slots) + " cameras " +
               // camera-quorum-exempt: English, not arithmetic -- one camera takes "is"
               (cameras_that_see == 1 ? "is" : "are") +
               " looking at the dartboard and this board needs " + std::to_string(quorum) +
               " before it may score -- below that the state vote can never be reached, so "
               "the board could neither call a dart nor see one taken out";
    }
}

#pragma once
// turnaus#1537: a visit that closes with fewer than three darts publishes the unseen ones as
// inferred MISSes, before its END. The rule, pure, over the stream of results the scorer
// publishes; scorer.cpp feeds every result through one Ledger and sends what it answers.
//
// WHY. A dart that lands beyond the rim opens no event: nothing moves inside the board mask,
// so no window publishes it, and the takeout's END closes a visit of two. A Marker would have
// written three; the board recorded two, and Turnaus stores a two-dart visit. Live on
// 2026-10-10 (build 2b56b48, OD_SPIKE_THRESHOLD=0.006, OD_LONE_CAMERA=on) three visits closed
// short that way in one session -- 16:12 (miss, miss, miss: two published), 16:21 and 16:38.
// The maintainer's decision, the same day: "some darts that do not land on board do not count
// even they should". A visit that closes short marks the unseen darts as thrown misses.
//
// THE RULE. At an END, if the visit published 1 or 2 darts (a MISS is a dart; an END is not),
// the board publishes 3 - n MISSes first, then the END. Nothing else:
//   - a visit that published NOTHING is not filled. An END with no dart before it has no round
//     on the server to close (TurnausClient opens a round at its first dart), and filling it
//     would invent a whole visit from a takeout alone -- a spurious END would become three
//     misses in somebody's leg.
//   - a visit that published three or more (OD_PAST_THREE) is not filled.
//   - the visit the thrower genuinely cut short (two darts, then the takeout) is filled too,
//     and wrongly. The rule cannot tell it from an unseen miss; docs/rig.md says what the
//     fixtures hold of it.
//
// WHAT AN INFERRED MISS CARRIES, so nothing downstream mistakes it for a measurement: score
// MISS, confidence 0.5, camera -1, no position, no board fields, no candidates, `path`
// "inferred" (the account's third value beside "geometry" and "vote") and `inferred` true,
// which TurnausClient::detectionBody posts as `"inferred": true`. Its timestamp is the END's:
// the dart was not seen, so the only instant the board has for it is the one it was inferred
// at.
//
// `OD_VISIT_FILL`:
//   off (default)  as before; a short visit is still said at INFO (I1537 VISIT SHORT), so a
//                  replay without the switch counts what it would have filled
//   on             the unseen darts are published as above (I1537 VISIT FILL, one per dart)
//
// Pure: no environment, no clock, no log. scorer.cpp reads the switch and writes the lines.
#include <string>
#include <vector>
#include "../detector/detector_interface.hpp"

namespace visit_fill
{
    constexpr int kDartsPerVisit = 3;
    constexpr float kInferredConfidence = 0.5f;
    inline const char *inferredPath() { return "inferred"; }

    // How many darts a visit that published `published` is short at its END: 0 for an empty
    // visit and for a whole (or over-full) one.
    inline int unseenAtEnd(int published)
    {
        if (published <= 0 || published >= kDartsPerVisit)
        {
            return 0;
        }
        return kDartsPerVisit - published;
    }

    // The MISS the board publishes for a dart it did not see, from the END that closed its visit.
    inline DetectorResult inferredMiss(const DetectorResult &end)
    {
        DetectorResult miss;
        miss.dart_detected = true;
        miss.score = "MISS";
        miss.confidence = kInferredConfidence;
        miss.camera_index = -1;
        miss.timestamp = end.timestamp;
        miss.processing_time_ms = end.processing_time_ms;
        miss.path = inferredPath();
        miss.inferred = true;
        return miss;
    }

    // One visit's count, and what each result the detector publishes becomes.
    class Ledger
    {
    public:
        struct Answer
        {
            std::vector<DetectorResult> send; // in order; the result itself is always last
            int published = 0;                // darts the visit had published before this END
            int unseen = 0;                   // how short the visit closed; 0 unless an END
        };

        Answer admit(const DetectorResult &result, bool fill)
        {
            Answer out;
            if (result.score == "END")
            {
                out.published = published_;
                out.unseen = unseenAtEnd(published_);
                if (fill)
                {
                    for (int i = 0; i < out.unseen; i++)
                    {
                        out.send.push_back(inferredMiss(result));
                    }
                }
                published_ = 0;
            }
            else
            {
                published_++;
            }
            out.send.push_back(result);
            return out;
        }

        int published() const { return published_; }

    private:
        int published_ = 0;
    };

    // The sentences, written once so the census and the check read the same words.
    inline std::string shortSentence(int published, bool filled)
    {
        const int unseen = unseenAtEnd(published);
        return "I1537 VISIT SHORT: the visit closed with " + std::to_string(published) + " of " +
               std::to_string(kDartsPerVisit) + " darts published, " + std::to_string(unseen) +
               (unseen == 1 ? " dart" : " darts") + " unseen; " +
               (filled ? "published as inferred MISS before the END (OD_VISIT_FILL=on)"
                       : "not filled (OD_VISIT_FILL is off)") +
               " (turnaus#1537)";
    }

    inline std::string fillSentence(int published, int ordinal)
    {
        return "I1537 VISIT FILL: dart " + std::to_string(ordinal) + " of " + std::to_string(kDartsPerVisit) +
               " was not seen (the visit published " + std::to_string(published) +
               "); published as MISS at confidence 0.5, inferred rather than measured, path=inferred";
    }
}

// turnaus#1537: a visit that closes with fewer than three darts publishes the unseen ones as
// inferred MISSes before its END (src/scorer/visit_fill.hpp, OD_VISIT_FILL=on), and the body
// Turnaus receives says which MISS was inferred.
//
// The visits are the live session's three short ones (2026-10-10, build 2b56b48: 16:12 miss,
// miss, [miss]; 16:21 D3, S3, [miss]; 16:38 S19, T3, [miss]) and the shapes the rule must
// leave alone: a whole visit, an END with no dart before it, a visit past three, the next
// visit after a filled one, and the switch off.
//
//   g++ -std=c++17 -I src -I src/utils -I build/_deps/nlohmann_json-src/include \
//       -I build/_deps/httplib-src testers/i1537_fill_check.cpp \
//       src/communication/turnaus_client.cpp $(pkg-config --cflags --libs opencv4) -lpthread
//
// testers/i1537_check.sh runs it on the tree and on mutated copies, with the predictions.
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "communication/turnaus_client.hpp"
#include "scorer/visit_fill.hpp"

using json = nlohmann::json;

static int checks = 0;
static int failed = 0;

static void say(bool ok, const std::string &label, const std::string &detail)
{
    checks++;
    if (!ok)
    {
        failed++;
    }
    std::cout << (ok ? "PASS " : "FAIL ") << label << " " << detail << std::endl;
}

static DetectorResult dart(const std::string &score)
{
    DetectorResult r;
    r.dart_detected = true;
    r.score = score;
    r.confidence = 0.9f;
    r.camera_index = 0;
    r.timestamp = 1000;
    return r;
}

static DetectorResult endOfVisit(uint64_t at = 5000)
{
    DetectorResult r;
    r.dart_detected = true;
    r.score = "END";
    r.confidence = 1.0f;
    r.camera_index = -1;
    r.timestamp = at;
    return r;
}

struct Replay
{
    std::string sequence;     // what the board published, in order; an inferred MISS is MISS*
    std::vector<int> unseen;  // per END, how short the ledger said the visit closed
    std::vector<DetectorResult> sent;
};

static Replay replay(const std::vector<DetectorResult> &in, bool fill)
{
    visit_fill::Ledger ledger;
    Replay out;
    for (const DetectorResult &r : in)
    {
        const visit_fill::Ledger::Answer a = ledger.admit(r, fill);
        if (r.score == "END")
        {
            out.unseen.push_back(a.unseen);
        }
        for (const DetectorResult &s : a.send)
        {
            out.sequence += (out.sequence.empty() ? "" : " ") + s.score + (s.inferred ? "*" : "");
            out.sent.push_back(s);
        }
    }
    return out;
}

static std::string ints(const std::vector<int> &v)
{
    std::string s;
    for (int i : v)
    {
        s += (s.empty() ? "" : ",") + std::to_string(i);
    }
    return "[" + s + "]";
}

static void expectSequence(const std::string &label, const std::vector<DetectorResult> &in, bool fill,
                           const std::string &want, const std::vector<int> &want_unseen)
{
    const Replay r = replay(in, fill);
    say(r.sequence == want && r.unseen == want_unseen, label,
        "published \"" + r.sequence + "\" unseen " + ints(r.unseen) + " (want \"" + want + "\" unseen " +
            ints(want_unseen) + ")");
}

int main()
{
    // ---- the live session's three short visits, one ledger each --------------------------
    expectSequence("live: 16:12", {dart("MISS"), dart("MISS"), endOfVisit()}, true, "MISS MISS MISS* END", {1});
    expectSequence("live: 16:21", {dart("D3"), dart("S3"), endOfVisit()}, true, "D3 S3 MISS* END", {1});
    expectSequence("live: 16:38", {dart("S19"), dart("T3"), endOfVisit()}, true, "S19 T3 MISS* END", {1});

    // ---- the counts the rule turns on ------------------------------------------------------
    expectSequence("one dart:", {dart("S20"), endOfVisit()}, true, "S20 MISS* MISS* END", {2});
    expectSequence("whole:", {dart("T20"), dart("S20"), dart("S1"), endOfVisit()}, true, "T20 S20 S1 END", {0});
    expectSequence("empty:", {endOfVisit()}, true, "END", {0});
    expectSequence("past three:", {dart("S20"), dart("S20"), dart("S20"), dart("S5"), endOfVisit()}, true,
                   "S20 S20 S20 S5 END", {0});

    // ---- the count is the visit's, not the session's ---------------------------------------
    expectSequence("reset:",
                   {dart("S20"), dart("S20"), endOfVisit(), dart("T20"), endOfVisit()}, true,
                   "S20 S20 MISS* END T20 MISS* MISS* END", {1, 2});

    // ---- the switch: off fills nothing, and still says how short the visit was -------------
    expectSequence("switch:", {dart("S20"), dart("S20"), endOfVisit()}, false, "S20 S20 END", {1});

    // ---- order: every inferred MISS before the END, the END once and last ------------------
    {
        const Replay r = replay({dart("S20"), endOfVisit()}, true);
        int ends = 0;
        bool after_end = false;
        bool ordered = true;
        for (const DetectorResult &s : r.sent)
        {
            if (s.score == "END")
            {
                ends++;
                after_end = true;
            }
            else if (after_end)
            {
                ordered = false;
            }
        }
        say(ends == 1 && ordered && !r.sent.empty() && r.sent.back().score == "END", "order:",
            "\"" + r.sequence + "\": the END once, and last");
    }

    // ---- what an inferred MISS carries ------------------------------------------------------
    DetectorResult inferred;
    {
        const Replay r = replay({dart("S20"), dart("S20"), endOfVisit(7777)}, true);
        for (const DetectorResult &s : r.sent)
        {
            if (s.inferred)
            {
                inferred = s;
            }
        }
        const bool ok = inferred.inferred && inferred.dart_detected && inferred.score == "MISS" &&
                        inferred.confidence == 0.5f && inferred.camera_index == -1 && inferred.path == "inferred" &&
                        inferred.candidates.empty() && !inferred.board_radius_known && !inferred.board_angle_known &&
                        inferred.ring.empty() && inferred.segment == -1 && !inferred.boundary_flagged &&
                        inferred.timestamp == 7777 && inferred.window_ordinal == -1;
        say(ok, "marked:",
            "score=" + inferred.score + " confidence=" + std::to_string(inferred.confidence) +
                " camera=" + std::to_string(inferred.camera_index) + " path=" + inferred.path +
                " inferred=" + std::to_string(inferred.inferred) + " candidates=" +
                std::to_string(inferred.candidates.size()) + " timestamp=" + std::to_string(inferred.timestamp));
    }

    // ---- the body Turnaus receives ----------------------------------------------------------
    const std::string REF = "01HZZZZZZZZZZZZZZZZZZZZZZZ";
    {
        const std::string text = TurnausClient::detectionBody(REF, inferred).json;
        const json b = text.empty() ? json::object() : json::parse(text);
        const bool ok = b.value("sector", "") == "None" && b.value("inferred", false) == true &&
                        b.value("path", "") == "inferred" && b.contains("confidence") &&
                        b["confidence"].get<double>() == 0.5 && !b.contains("candidates") &&
                        !b.contains("board_radius") && !b.contains("board_angle");
        say(ok, "body:", text);
    }
    {
        // A measured MISS, with and without the account: no `inferred` key, and the bare one
        // is the bytes it was before this issue.
        DetectorResult seen = dart("MISS");
        const std::string bare = TurnausClient::detectionBody(REF, seen).json;
        seen.path = "geometry";
        const std::string accounted = TurnausClient::detectionBody(REF, seen).json;
        const std::string before = "{\"bounced_out\":false,\"reference\":\"" + REF + "\",\"sector\":\"None\"}";
        say(bare == before && accounted.find("\"inferred\"") == std::string::npos, "body unchanged:",
            bare + " / " + accounted);
    }

    std::cout << "CHECKS " << checks << " FAILURES " << failed << std::endl;
    return failed == 0 ? 0 : 1;
}

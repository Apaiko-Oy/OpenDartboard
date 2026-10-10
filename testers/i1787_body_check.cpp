// #1787: the rest of the board's account, in the body that is posted for ever -- and
// #1366's bytes untouched beside it.
//
// `TurnausClient::detectionBody` is the one place a body is composed and #822 rule 3 posts
// those bytes for ever, so what the account says is decided there and held here: no
// client, no socket, no spool. Three darts the way the detector hands them over -- a
// flagged geometric T20 solved from two lines, an unflagged vote S19 a lone camera read
// and LONE-WIRE measured, and the MISS the vote publishes when no camera scored -- each
// with the fields the issue names, present exactly when the result has the number and
// absent otherwise (never null, never a nought: #1366's spelling of an absence).
//
// THE BYTES THAT MUST NOT MOVE. A result that carries no account (`path` empty) builds
// the body it built before this issue, byte for byte: the literal #1366 wrote out by
// hand is compared here again, unchanged. And on a result that DOES carry one, erasing
// the account's keys from the body leaves exactly today's bytes -- so nothing about an
// existing field's name, order or formatting moved.
//
// THE MUTATION, with the prediction stated first: a body with one account field erased
// is a body `accountFieldsExpected` names as missing THAT field and no other. Run on
// every field of every dart in the same process, so the needle is shown to be
// load-bearing on every run (#1463) rather than argued about.
//
//   g++ -std=c++17 -I src -I src/utils -I build/_deps/nlohmann_json-src/include \
//       -I build/_deps/httplib-src testers/i1787_body_check.cpp \
//       src/communication/turnaus_client.cpp -o body_check \
//       $(pkg-config --cflags --libs opencv4) -lpthread

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "communication/turnaus_client.hpp"

using json = nlohmann::json;

static int checks = 0;
static std::vector<std::string> failures;

static bool say(bool ok, const std::string &what)
{
    checks++;
    std::cout << (ok ? "PASS " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures.push_back(what);
    }
    return ok;
}

static const std::string REF = "01HZZZZZZZZZZZZZZZZZZZZZZZ";

/** A placed dart the way #1366's check builds one: nothing about the account known. */
static DetectorResult placed(const std::string &score, float radius, float angle)
{
    DetectorResult r;
    r.dart_detected = true;
    r.score = score;
    r.confidence = 0.9f;
    r.camera_index = 1;
    r.board_radius_known = true;
    r.board_angle_known = true;
    r.board_radius = radius;
    r.board_angle = angle;
    return r;
}

/** The literal #1366 wrote out by hand: what an unflagged placed T20 has always posted. */
static const std::string TODAY =
    "{\"board_angle\":12.3456,\"board_radius\":0.6123,\"bounced_out\":false,"
    "\"reference\":\"" + REF + "\",\"sector\":\"T20\"}";

static json bodyOf(const DetectorResult &r)
{
    return json::parse(TurnausClient::detectionBody(REF, r).json);
}

/** Which of the fields `expected` are not in `body`, by name. */
static std::vector<std::string> missing(const json &body, const std::vector<std::string> &expected)
{
    std::vector<std::string> out;
    for (const std::string &f : expected)
    {
        if (!body.contains(f))
        {
            out.push_back(f);
        }
    }
    return out;
}

static std::string joined(const std::vector<std::string> &v)
{
    std::string s;
    for (const std::string &w : v)
    {
        s += (s.empty() ? "" : ",") + w;
    }
    return s.empty() ? "-" : s;
}

int main()
{
    // ---- #1366's bytes, unchanged: a result with no account posts today's body -----------
    {
        TurnausClient::DetectionBody plain = TurnausClient::detectionBody(REF, placed("T20", 0.6123f, 12.3456f));
        say(plain.json == TODAY && !plain.carries_account,
            "a result with no account (path empty) posts exactly #1366's bytes (" + plain.json + ")");
        say(TurnausClient::accountFieldsExpected(placed("T20", 0.6123f, 12.3456f)).empty(),
            "and the rule expects no account field of it");
    }

    // ---- THE FLAGGED GEOMETRIC DART: a two-line solve of cameras 2 and 3 -------------------
    //
    // The live shape of 2026-10-08 (docs/rig.md, #1766): T3 flagged against S3 across the
    // ring wire, 4.4 mm away across a 5.0 mm sigma, the two lines crossing at 27 deg.
    DetectorResult geo = placed("T20", 0.6123f, 12.3456f);
    geo.path = "geometry";
    geo.confidence = 0.7f;
    geo.boundary_flagged = true;
    geo.alternative_score = "S20";
    geo.boundary_kind = "ring";
    geo.boundary_mm = 4.4f;
    geo.uncertainty_mm = 5.0f;
    geo.cameras_used = {2, 3};
    geo.crossing_deg = 27.0f;
    geo.candidates = {"S20", "T1"};
    {
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, geo);
        json j = json::parse(b.json);
        std::cout << "  BODY flagged geometric: " << b.json << std::endl;
        say(b.carries_account, "a flagged geometric dart carries the account");
        say(j["path"] == "geometry", "path=geometry");
        say(j["flagged"] == true && j["alternative"] == "S20", "flagged, and the alternative #1651 posts is still there");
        say(j["confidence"].dump() == "0.7", "confidence 0.7 at two places (" + j["confidence"].dump() + ")");
        say(j["degraded"] == false, "not degraded: the geometry published");
        say(j["cameras_used"] == json({2, 3}), "cameras_used names the two lines, numbered from 1 as the log does");
        say(j["crossing_deg"].dump() == "27.0", "crossing_deg is the two-line solve's pair angle (" + j["crossing_deg"].dump() + ")");
        say(j["sigma_mm"].dump() == "5.0" && j["margin_mm"].dump() == "4.4" && j["wire_kind"] == "ring",
            "sigma_mm, margin_mm and wire_kind are the UNCERTAINTY sentence's numbers");
        say(!j.contains("agreeing") && !j.contains("lone_wire_mm") && !j.contains("ring_wire_mm"),
            "and nothing of the vote's story is in it: no agreeing, no lone_wire_mm, no ring_wire_mm");
        say(j["candidates"] == json({"S20", "T1"}), "#1721's candidates are untouched");

        // Erase the account's keys and the alternative and candidates #1651/#1721 add: today's bytes.
        json without = j;
        for (const std::string &f : TurnausClient::accountFields())
        {
            without.erase(f);
        }
        without.erase("alternative");
        without.erase("candidates");
        say(without.dump() == TODAY, "take the account out and it is #1366's bytes: no existing field moved");
        say(missing(j, TurnausClient::accountFieldsExpected(geo)).empty(),
            "every field the rule expects of it is in the body (expected " +
                joined(TurnausClient::accountFieldsExpected(geo)) + ")");
    }

    // A three-line solve carries no crossing_deg: the sigma is not one pair's angle.
    {
        DetectorResult three = geo;
        three.cameras_used = {1, 2, 3};
        three.crossing_deg = -1.0f;
        json j = bodyOf(three);
        say(!j.contains("crossing_deg") && j["cameras_used"] == json({1, 2, 3}),
            "a three-line solve names three cameras and no crossing_deg");
    }

    // ---- THE UNFLAGGED VOTE DART: a lone camera's S19, LONE-WIRE measured ------------------
    DetectorResult vote = placed("S19", 0.55f, 200.0f);
    vote.path = "vote";
    vote.confidence = 0.7f;
    vote.degraded = true; // the geometry was asked, refused by name, and the vote published
    vote.cameras_used = {2};
    vote.agreeing = 1;
    vote.lone_wire_mm = 7.8f;
    {
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, vote);
        json j = json::parse(b.json);
        std::cout << "  BODY unflagged vote: " << b.json << std::endl;
        say(b.carries_account && j["path"] == "vote", "a vote dart carries the account with path=vote");
        say(j["flagged"] == false && !j.contains("alternative"), "unflagged, and no alternative key");
        say(j["degraded"] == true, "degraded: the vote published because the geometry refused");
        say(j["cameras_used"] == json({2}) && j["agreeing"] == 1,
            "cameras_used is the one camera whose string published, agreeing is the Consensus count");
        say(j["lone_wire_mm"].dump() == "7.8", "lone_wire_mm is LONE-WIRE's margin (" + j["lone_wire_mm"].dump() + ")");
        say(!j.contains("sigma_mm") && !j.contains("margin_mm") && !j.contains("wire_kind") && !j.contains("crossing_deg"),
            "and no millimetre the vote never measured: no sigma_mm, margin_mm, wire_kind or crossing_deg");
        say(!j.contains("ring_wire_mm"), "ring_wire_mm is absent where no RING-WIRE line measured one (#1773 fills it)");
        say(missing(j, TurnausClient::accountFieldsExpected(vote)).empty(),
            "every field the rule expects of it is in the body (expected " +
                joined(TurnausClient::accountFieldsExpected(vote)) + ")");
        say(j["board_radius"].dump() == "0.55" && j["board_angle"].dump() == "200.0",
            "the position is as #1366 writes it");

        DetectorResult ringed = vote;
        ringed.ring_wire_mm = 2.5f;
        say(bodyOf(ringed)["ring_wire_mm"].dump() == "2.5", "and a RING-WIRE margin, once measured, is posted");
        DetectorResult agreed = vote;
        agreed.agreeing = 2;
        agreed.confidence = 0.9f;
        agreed.degraded = false;
        agreed.lone_wire_mm = -1.0f;
        json a = bodyOf(agreed);
        say(a["agreeing"] == 2 && a["confidence"].dump() == "0.9" && !a.contains("lone_wire_mm"),
            "a consensus dart says agreeing=2 at 0.9 and has no LONE-WIRE margin");
    }

    // ---- THE MISS the vote publishes when no camera scored ---------------------------------
    DetectorResult miss;
    miss.dart_detected = true;
    miss.score = "MISS";
    miss.confidence = 0.5f;
    miss.camera_index = -1;
    miss.path = "vote";
    {
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, miss);
        json j = json::parse(b.json);
        std::cout << "  BODY miss: " << b.json << std::endl;
        say(j["sector"] == "None" && b.carries_account, "a MISS is still None, and carries the account");
        say(j["path"] == "vote" && j["flagged"] == false && j["confidence"].dump() == "0.5" && j["agreeing"] == 0,
            "path=vote, unflagged, 0.5, agreeing 0");
        say(!j.contains("cameras_used"), "no camera scored it, so no cameras_used");
        say(!j.contains("board_radius") && !j.contains("sigma_mm"), "no place and no sigma");
        say(missing(j, TurnausClient::accountFieldsExpected(miss)).empty(),
            "every field the rule expects of it is in the body (expected " +
                joined(TurnausClient::accountFieldsExpected(miss)) + ")");
    }

    // ---- POSTED FOR EVER: the same result and reference build the same bytes ---------------
    say(TurnausClient::detectionBody(REF, geo).json == TurnausClient::detectionBody(REF, geo).json,
        "the same detection and the same reference build the same bytes, account included");

    // ---- an unpostable sector is still no body at all, account or none ---------------------
    {
        DetectorResult bad = geo;
        bad.score = "S21";
        say(TurnausClient::detectionBody(REF, bad).json.empty(), "'S21' with an account is still no body");
    }

    // ---- THE MUTATION PROOF ------------------------------------------------------------------
    //
    // PREDICTION, stated before the run: for each of the three darts and for each account
    // field its body carries, a body with that one field erased is reported as missing
    // exactly that field -- by name -- and nothing else. 9 + 7 + 5 = 21 mutations (the
    // geometric dart's nine, the vote's seven, the miss's five); every one is named; none
    // names a second field.
    {
        struct Dart
        {
            const char *name;
            DetectorResult r;
        };
        const Dart darts[] = {{"flagged geometric", geo}, {"unflagged vote", vote}, {"miss", miss}};
        int mutations = 0, named = 0, overnamed = 0;
        for (const Dart &d : darts)
        {
            const std::vector<std::string> expected = TurnausClient::accountFieldsExpected(d.r);
            for (const std::string &f : expected)
            {
                json mutated = bodyOf(d.r);
                mutated.erase(f);
                const std::vector<std::string> lost = missing(mutated, expected);
                mutations++;
                if (lost.size() == 1 && lost[0] == f)
                {
                    named++;
                }
                else if (!lost.empty())
                {
                    overnamed++;
                }
                std::cout << "  MUTATION " << d.name << " without " << f << ": the check names " << joined(lost)
                          << std::endl;
            }
        }
        say(mutations == 21, "21 mutations were run, one per field per dart (" + std::to_string(mutations) + ")");
        say(named == mutations && overnamed == 0,
            "every mutation is named by exactly the field it dropped (" + std::to_string(named) + " of " +
                std::to_string(mutations) + ")");
        // And the unmutated bodies are named for nothing: the needle has a quiet side.
        say(missing(bodyOf(geo), TurnausClient::accountFieldsExpected(geo)).empty() &&
                missing(bodyOf(vote), TurnausClient::accountFieldsExpected(vote)).empty() &&
                missing(bodyOf(miss), TurnausClient::accountFieldsExpected(miss)).empty(),
            "and the unmutated bodies are named for nothing");
    }

    std::cout << std::endl
              << "checks " << checks << "   failed " << failures.size() << std::endl;
    for (const std::string &f : failures)
    {
        std::cout << "  - " << f << std::endl;
    }
    return failures.empty() ? 0 : 1;
}

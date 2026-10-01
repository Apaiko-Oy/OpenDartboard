// #1366: where the dart landed, held at the seam that writes it.
//
// `TurnausClient::detectionBody` is the one place a detection body is composed, and #822
// rule 3 means those bytes are posted FOR EVER -- the spool file keeps them and every
// retry after every restart sends them again. So the whole of what a body says has to be
// decided there, and this file is what holds it: no client, no socket, no spool.
//
// The needles are proved before their absence means anything (#708's shape). A MISS is
// shown to drop a position that a scoring dart WITH THE SAME NUMBERS is shown to carry,
// so "no board_radius in a miss" is a refusal rather than a body that never had one. The
// float just below 360 is shown to be ABOVE the door's published bound before it is shown
// to leave as 0, so the wrap is closing a real 422 rather than tidying.
//
// #1365's rules are quoted here as the server really spells them --
// `App\Autoscoring\PushedPosition::rules()` and `Domain\Scoring\Darts\BoardPosition` --
// so a server that moves a bound makes this file's control wrong rather than silently
// stale, which is the arrangement #1347 made for the sector grammar.
//
//   g++ -std=c++17 -I src -I src/utils -I build/_deps/nlohmann_json-src/include \
//       -I build/_deps/httplib-src testers/i1366_position_check.cpp \
//       src/communication/turnaus_client.cpp -o position_check \
//       $(pkg-config --cflags --libs opencv4) -lpthread

#include <cmath>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "communication/turnaus_client.hpp"
#include "detector/geometry/detection/dart_candidates.hpp"

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

/** A detection the way the geometry hands one over, with nothing about the board known. */
static DetectorResult dart(const std::string &score)
{
    DetectorResult r;
    r.dart_detected = true;
    r.score = score;
    r.confidence = 0.9f;
    r.camera_index = 1;
    return r;
}

static DetectorResult placed(const std::string &score, float radius, float angle)
{
    DetectorResult r = dart(score);
    r.board_radius_known = true;
    r.board_angle_known = true;
    r.board_radius = radius;
    r.board_angle = angle;
    return r;
}

// ---- #1365's door, reimplemented from its own source ------------------------------------
//
// `PushedPosition::rules()`:
//   board_radius: required_with:board_angle, nullable, numeric, min:0.0,  max:1.2
//   board_angle:  required_with:board_radius, nullable, numeric, min:0.0, max:359.9999
//
// Laravel's `required_with:other` is satisfied only when this field is present AND not
// null, and it is skipped only when `other` is itself absent or null. So an explicit pair
// of nulls is no position, and ONE null beside one number is half a position and a 422.
// A 422 is the expensive refusal here: `TurnausClient::deliver` drops a 422 rather than
// retrying it, so a body this function refuses is a DART lost, not a position.
struct DoorVerdict
{
    bool accepted = true;
    std::string because;
};

static DoorVerdict theDoor(const std::string &body)
{
    DoorVerdict out;
    json j;
    try
    {
        j = json::parse(body);
    }
    catch (const std::exception &e)
    {
        out.accepted = false;
        out.because = std::string("the body is not JSON: ") + e.what();
        return out;
    }
    // #1720's `PushedCandidates::rules()`: 'candidates' => ['sometimes', 'nullable', 'array',
    // 'max:3']. The entries are never read by a rule (garbage is dropped past the door);
    // the COUNT is refused, and a fourth costs the whole dart.
    if (j.contains("candidates") && !j["candidates"].is_null())
    {
        if (!j["candidates"].is_array())
        {
            out.accepted = false;
            out.because = "candidates is not an array: " + body;
            return out;
        }
        if (j["candidates"].size() > 3)
        {
            out.accepted = false;
            out.because = "candidates carries more than 3 (max:3): " + body;
            return out;
        }
    }
    const bool has_radius = j.contains("board_radius") && !j["board_radius"].is_null();
    const bool has_angle = j.contains("board_angle") && !j["board_angle"].is_null();
    if (has_radius != has_angle)
    {
        out.accepted = false;
        out.because = "required_with: half a polar position";
        return out;
    }
    if (!has_radius)
    {
        return out; // absent, or an explicit pair of nulls: no position, and admitted
    }
    if (!j["board_radius"].is_number() || !j["board_angle"].is_number())
    {
        out.accepted = false;
        out.because = "numeric";
        return out;
    }
    const double radius = j["board_radius"].get<double>();
    const double angle = j["board_angle"].get<double>();
    if (!(radius >= 0.0) || !(radius <= 1.2))
    {
        out.accepted = false;
        out.because = "board_radius is outside [0, 1.2]: " + body;
        return out;
    }
    if (!(angle >= 0.0) || !(angle <= 359.9999))
    {
        out.accepted = false;
        out.because = "board_angle is outside [0, 359.9999]: " + body;
        return out;
    }
    return out;
}

static json bodyOf(const DetectorResult &r)
{
    return json::parse(TurnausClient::detectionBody("01HZZZZZZZZZZZZZZZZZZZZZZZ", r).json);
}

int main()
{
    const std::string REF = "01HZZZZZZZZZZZZZZZZZZZZZZZ";

    // ---- what a body has always said, unchanged --------------------------------------
    {
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, dart("T20"));
        json j = json::parse(b.json);
        say(j["reference"] == REF, "the reference passed in is the reference sent");
        say(j["sector"] == "T20", "the sector is #821's grammar");
        say(j["bounced_out"] == false, "bounced_out is still there and still false");
        say(!b.carries_position && b.position_withheld.empty(),
            "a detection that knows no position carries none and withholds nothing");
        say(b.json.find("board_radius") == std::string::npos &&
                b.json.find("board_angle") == std::string::npos,
            "and NEITHER KEY is in the bytes -- absent, never a nought");
    }

    // ---- an unpostable sector is still no body at all ---------------------------------
    for (const std::string &score : {"", "S21", "X5"})
    {
        say(TurnausClient::detectionBody(REF, placed(score, 0.5f, 90.0f)).json.empty(),
            "'" + score + "' still answers with no body, position or no position");
    }

    // ---- THE CONTROL: a scoring dart carries both keys, and the numbers are the ones --
    {
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, placed("T20", 0.6123f, 12.3456f));
        json j = json::parse(b.json);
        say(b.carries_position, "a dart that knows where it landed says so");
        say(j.contains("board_radius") && j.contains("board_angle"), "and both keys are in the body");
        say(std::fabs(j["board_radius"].get<double>() - 0.6123) < 1e-9 &&
                std::fabs(j["board_angle"].get<double>() - 12.3456) < 1e-9,
            "with the radius and the angle the geometry measured");
        say(b.json.find("\"board_radius\":0.6123") != std::string::npos &&
                b.json.find("\"board_angle\":12.3456") != std::string::npos,
            "written at the four places the columns keep, not as the float's binary truth "
            "(" + b.json + ")");
        say(b.position_withheld.empty(), "and nothing is withheld");
    }

    // ---- the float's binary truth, which is what makes the rounding load-bearing ------
    {
        json raw;
        raw["board_radius"] = (double)0.1f;
        say(raw.dump().find("0.10000000149011612") != std::string::npos,
            "the needle: a float promoted straight into a body prints its binary truth "
            "(" + raw.dump() + ")");
        say(bodyOf(placed("S1", 0.1f, 0.1f)).dump().find("0.1,") != std::string::npos ||
                bodyOf(placed("S1", 0.1f, 0.1f))["board_radius"].dump() == "0.1",
            "and the seam prints 0.1");
    }

    // ---- BOTH OR NEITHER, which is the refusal that costs a dart ----------------------
    {
        DetectorResult radius_only = dart("Bull");
        radius_only.board_radius_known = true;
        radius_only.board_radius = 0.0212f;
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, radius_only);
        say(!b.carries_position && b.json.find("board_") == std::string::npos,
            "a bull that knows its radius and has no measured wedge sends NEITHER key");
        say(b.position_withheld.find("radius") != std::string::npos && !b.position_out_of_bounds,
            "and says which half it had, at debug rather than as a fault (" + b.position_withheld + ")");

        DetectorResult angle_only = dart("S20");
        angle_only.board_angle_known = true;
        angle_only.board_angle = 3.5f;
        TurnausClient::DetectionBody c = TurnausClient::detectionBody(REF, angle_only);
        say(!c.carries_position && c.json.find("board_") == std::string::npos,
            "and an angle without a radius is the same refusal the other way round");
        say(c.position_withheld.find("angle") != std::string::npos,
            "named the other way round too (" + c.position_withheld + ")");
    }

    // ---- A MISS HAS NO PLACE, and the needle is proved before its absence means anything
    {
        DetectorResult miss = placed("MISS", 0.6123f, 12.3456f);
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, miss);
        json j = json::parse(b.json);
        say(j["sector"] == "None", "a MISS is still #821's None");

        // The needle exists: the SAME numbers on a scoring dart really do reach a body.
        DetectorResult scoring = placed("T20", 0.6123f, 12.3456f);
        say(TurnausClient::detectionBody(REF, scoring).carries_position,
            "the needle: those same numbers on a scoring dart DO reach the body");
        say(!b.carries_position, "so the miss dropping them is a refusal and not an empty result");
        say(b.json.find("0.6123") == std::string::npos && b.json.find("12.3456") == std::string::npos,
            "swept BY VALUE as well as by key: neither number is anywhere in the miss's bytes "
            "(" + b.json + ")");
        say(!b.position_withheld.empty() && b.position_withheld.find("miss") != std::string::npos,
            "and the withholding says so (" + b.position_withheld + ")");

        // None is the same sector by another name, and a board may already send it.
        say(!TurnausClient::detectionBody(REF, placed("None", 0.5f, 5.0f)).carries_position,
            "and a detection already spelled None is the same refusal");
    }

    // ---- 360 IS 0, which is the one value this footage could ever argue about ---------
    {
        // THE WHOLE INTERVAL, counted rather than sampled. `board_angle` is a float, and
        // (359.9999, 360) holds exactly THREE of them. Every one prints, through nlohmann,
        // as a decimal above the door's published bound -- so every one is a 422, and a
        // 422 is a dart. Rounded to the four places the column really keeps, ONE of them
        // (360 - 1 ulp, which is 359.99995 or more) becomes 360.0000 and leaves as 0; the
        // other two become 359.9999, which is a place. Both answers are admitted, and it
        // is worth knowing which is which rather than asserting the same thing three times.
        int refused_raw = 0;
        int sent_as_zero = 0;
        int sent_as_the_bound = 0;
        float f = std::nextafter(360.0f, 0.0f);
        for (int i = 0; i < 3; i++)
        {
            say((double)f > 359.9999 && (double)f < 360.0,
                "the float " + std::to_string(i + 1) + " below 360 really is in (359.9999, 360)");

            json raw;
            raw["board_angle"] = (double)f;
            DoorVerdict unrounded = theDoor("{\"board_radius\":0.5,\"board_angle\":" +
                                            raw["board_angle"].dump() + "}");
            if (!unrounded.accepted)
            {
                refused_raw++;
            }

            TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, placed("S20", 0.5f, f));
            json j = json::parse(b.json);
            say(b.carries_position && theDoor(b.json).accepted,
                "and the seam sends it as something the door admits (" + j["board_angle"].dump() + ")");
            if (j["board_angle"].dump() == "0.0")
            {
                sent_as_zero++;
            }
            else if (j["board_angle"].dump() == "359.9999")
            {
                sent_as_the_bound++;
            }
            f = std::nextafter(f, 0.0f);
        }
        say(refused_raw == 3,
            "the needle: ALL THREE floats between 359.9999 and 360 are refused by the door as "
            "published, unrounded -- the 422 this wrap closes is real (" +
                std::to_string(refused_raw) + " of 3)");
        say(sent_as_zero == 1,
            "exactly one of them rounds into 360.0000 and is sent as 0, because 360 degrees is "
            "0 degrees (" + std::to_string(sent_as_zero) + ")");
        say(sent_as_the_bound == 2,
            "and the other two round to 359.9999, which is a place the column holds (" +
                std::to_string(sent_as_the_bound) + ")");
        // A rounded negative nothing is a real double with a sign on it, and it prints
        // with the sign. It would validate; it reads like a fault.
        json tiny = bodyOf(placed("S20", -0.00001f, -0.00001f));
        say(tiny["board_radius"].dump() == "0.0" && tiny["board_angle"].dump() == "0.0",
            "a radius and an angle that round to nothing leave as 0.0 and never as -0.0 ("
                + tiny.dump() + ")");
    }

    // ---- the bounds themselves, at the places they are really kept --------------------
    {
        say(bodyOf(placed("S20", 1.2f, 359.9999f)).contains("board_radius"),
            "1.2 is a place and 359.9999 is a place: the published bounds are inclusive");
        say(bodyOf(placed("S20", 0.0f, 0.0f))["board_radius"].dump() == "0.0",
            "the bull centre is radius 0 and it is sent as 0, never as an absence");

        TurnausClient::DetectionBody over = TurnausClient::detectionBody(REF, placed("S20", 1.25f, 10.0f));
        say(!over.carries_position && over.position_out_of_bounds,
            "1.25 is off the far end, so the POSITION is withheld");
        say(over.json.find("\"sector\":\"S20\"") != std::string::npos,
            "and THE DART STILL GOES -- a 422 is not retried, so a refused body is a lost dart");
        say(over.position_withheld.find("1.2500") != std::string::npos,
            "the refusal names the number it refused (" + over.position_withheld + ")");

        // 1.20004 rounds INTO the bound at the precision the column keeps, so it is a
        // place; 1.20006 does not.
        say(bodyOf(placed("S20", 1.20004f, 10.0f)).contains("board_radius"),
            "1.20004 is 1.2000 at four places, which is a place the column holds");
        say(!TurnausClient::detectionBody(REF, placed("S20", 1.20006f, 10.0f)).carries_position,
            "1.20006 is 1.2001, which is not");
    }

    // ---- POSTED FOR EVER: the bytes are settled here and nowhere else -----------------
    {
        DetectorResult r = placed("D18", 0.9876f, 201.5f);
        std::string once = TurnausClient::detectionBody(REF, r).json;
        std::string twice = TurnausClient::detectionBody(REF, r).json;
        say(once == twice, "the same detection and the same reference build the same bytes");
        say(json::parse(once).contains("board_radius"),
            "and those bytes -- the ones the spool file keeps and every retry re-posts -- "
            "are the ones the position is in");
    }

    // ---- #1651: A CLOSE CALL'S OTHER CANDIDATE, in the grammar the door reads -----------
    //
    // #1559's rule, quoted as `CasualDetectionController` and `AutoscorerDetectionController`
    // spell it -- 'alternative' => ['sometimes', 'nullable', 'string', 'max:16'] -- and then
    // `Detection::otherThan`, which DROPS rather than refuses one `Sector::PATTERN` cannot
    // read or one naming the dart's own sector. A drop is silent, so an untranslated OUTER
    // would never be a 422 anybody saw: it would be a tap that never appears.
    {
        // The literal bytes an unflagged dart has always posted, written out by hand, so
        // "byte-identical to today" is a comparison with something this change cannot move.
        const std::string TODAY =
            "{\"board_angle\":12.3456,\"board_radius\":0.6123,\"bounced_out\":false,"
            "\"reference\":\"" + REF + "\",\"sector\":\"T20\"}";
        TurnausClient::DetectionBody plain = TurnausClient::detectionBody(REF, placed("T20", 0.6123f, 12.3456f));
        say(plain.json == TODAY && !plain.carries_alternative,
            "an unflagged dart posts exactly the bytes it posted before #1651 (" + plain.json + ")");

        // THE NEEDLE: the same dart, flagged, with the other candidate named.
        DetectorResult flagged = placed("T20", 0.6123f, 12.3456f);
        flagged.boundary_flagged = true;
        flagged.alternative_score = "S20";
        flagged.boundary_kind = "ring";
        flagged.boundary_mm = 0.4f;
        flagged.uncertainty_mm = 1.1f;
        TurnausClient::DetectionBody f = TurnausClient::detectionBody(REF, flagged);
        json fj = json::parse(f.json);
        say(f.carries_alternative && fj.contains("alternative") && fj["alternative"] == "S20",
            "a flagged dart posts its alternative (" + f.json + ")");
        json without = fj;
        without.erase("alternative");
        say(without.dump() == TODAY,
            "and nothing else about its body moved: take the key out and it is today's bytes");
        say(f.json.find("uncertainty") == std::string::npos && f.json.find("boundary") == std::string::npos,
            "the socket's measurements stay on the socket; the door is sent the candidate only");

        // The same needle unflagged: a name with no flag is not a close call. The socket
        // publishes null for it, and the body says nothing.
        DetectorResult named_unflagged = placed("T20", 0.6123f, 12.3456f);
        named_unflagged.alternative_score = "S20";
        TurnausClient::DetectionBody u = TurnausClient::detectionBody(REF, named_unflagged);
        say(u.json == TODAY && !u.carries_alternative,
            "an alternative the geometry named but did not flag is not sent (" + u.json + ")");

        // A flag with nothing named: what the string-vote path can never produce, held anyway.
        DetectorResult flag_unnamed = placed("T20", 0.6123f, 12.3456f);
        flag_unnamed.boundary_flagged = true;
        say(TurnausClient::detectionBody(REF, flag_unnamed).json == TODAY,
            "a flag with no candidate named sends no key and no null");

        // A string-vote dart: no position, no crossing, no alternative, by construction.
        TurnausClient::DetectionBody vote = TurnausClient::detectionBody(REF, dart("D16"));
        say(vote.json.find("alternative") == std::string::npos && !vote.carries_alternative,
            "a string-vote dart carries no alternative key (" + vote.json + ")");

        // THROUGH THE SAME SEAM AS THE SECTOR: the socket's words become #821's.
        struct Pair
        {
            std::string published, other, sent;
        };
        const Pair pairs[] = {
            {"S25", "BULL", ""},   // a published score the grammar cannot spell: no body at all
            {"OUTER", "BULL", "Bull"},
            {"BULL", "OUTER", "25"},
            {"S17", "OUTER", "25"},
            {"D20", "MISS", "None"},
            {"MISS", "D20", "D20"},
            {"T19", "S19", "S19"},
        };
        for (const Pair &p : pairs)
        {
            DetectorResult r = dart(p.published);
            r.boundary_flagged = true;
            r.alternative_score = p.other;
            TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, r);
            if (b.json.empty())
            {
                say(p.sent.empty(), "'" + p.published + "' is still no body, alternative or none");
                continue;
            }
            json j = json::parse(b.json);
            say(j.contains("alternative") && j["alternative"] == p.sent,
                p.published + " or " + p.other + " posts alternative " + p.sent + " (" + b.json + ")");
        }

        // Unspellable, or the dart's own sector once both are translated: no key.
        for (const Pair &p : {Pair{"T20", "X5", ""}, Pair{"T20", "S21", ""}, Pair{"OUTER", "25", ""},
                              Pair{"BULL", "Bull", ""}, Pair{"MISS", "None", ""},
                              Pair{"S20", "s20", ""}})
        {
            DetectorResult r = dart(p.published);
            r.boundary_flagged = true;
            r.alternative_score = p.other;
            TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, r);
            say(!b.json.empty() && b.json.find("alternative") == std::string::npos && !b.carries_alternative,
                p.published + " or " + p.other + " sends no alternative, because the door would drop it ("
                    + b.json + ")");
        }

        // What IS sent is always something `Detection::otherThan` keeps: readable by
        // Sector::PATTERN (quoted from Sector.php, as i1347_sector_check quotes it), a
        // string of at most 16, and not the dart's own place -- S20 and s20 are one place.
        const std::regex server_pattern("^(?:([SsDT])(20|1[0-9]|[1-9])|25|Bull|None)$");
        int dropped = 0;
        std::string first;
        const std::string words[] = {"S1", "s1", "D20", "T20", "S20", "BULL", "OUTER", "MISS",
                                     "25", "Bull", "None", "S0", "S21", "T", "END", "", "s20"};
        for (const std::string &published : words)
        {
            for (const std::string &other : words)
            {
                DetectorResult r = dart(published);
                r.boundary_flagged = true;
                r.alternative_score = other;
                TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, r);
                if (b.json.empty())
                {
                    continue;
                }
                json j = json::parse(b.json);
                if (!j.contains("alternative"))
                {
                    continue;
                }
                std::string a = j["alternative"].is_string() ? j["alternative"].get<std::string>() : "";
                std::string own = j["sector"].get<std::string>();
                for (std::string *w : {&a, &own})
                {
                    if (!w->empty() && (*w)[0] == 's')
                    {
                        (*w)[0] = 'S';
                    }
                }
                const bool kept = j["alternative"].is_string() && a.size() <= 16 &&
                                  std::regex_match(a, server_pattern) && a != own;
                if (!kept)
                {
                    dropped++;
                    if (first.empty())
                    {
                        first = b.json;
                    }
                }
            }
        }
        say(dropped == 0, "every alternative the seam sends is one #1559's door keeps rather than drops (" +
                              std::to_string(dropped) + " dropped" + (first.empty() ? "" : ", first: " + first) +
                              ")");
    }

    // ---- #1721: EVERY DART'S RANKED CANDIDATES, in the grammar the door reads ----------
    //
    // #1720's door: 'candidates' => ['sometimes', 'nullable', 'array', 'max:3'], each entry
    // dropped past the door when it is not a string Sector::PATTERN reads, a repeat, or the
    // dart's own place. A FOURTH is a 422, and deliver() drops a 422, so a list one too long
    // loses the dart. The seam translates through postableSector exactly as `sector` is.
    {
        const std::regex server_pattern("^(?:([SsDT])(20|1[0-9]|[1-9])|25|Bull|None)$");
        const std::string TODAY =
            "{\"board_angle\":12.3456,\"board_radius\":0.6123,\"bounced_out\":false,"
            "\"reference\":\"" + REF + "\",\"sector\":\"T20\"}";

        // No candidates: no key, today's bytes.
        TurnausClient::DetectionBody none = TurnausClient::detectionBody(REF, placed("T20", 0.6123f, 12.3456f));
        say(none.json == TODAY && none.candidates_sent == 0,
            "a dart with no candidates posts exactly today's bytes, no key and no [] (" + none.json + ")");

        // THE NEEDLE: present, in order, and nothing else about the body moved.
        DetectorResult r = placed("T20", 0.6123f, 12.3456f);
        r.candidates = {"S20", "T1", "T5"};
        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, r);
        json j = json::parse(b.json);
        say(j.contains("candidates") && j["candidates"] == json({"S20", "T1", "T5"}) && b.candidates_sent == 3,
            "a dart posts its candidates, present and in the order ranked (" + b.json + ")");
        json without = j;
        without.erase("candidates");
        say(without.dump() == TODAY, "take the key out and it is today's bytes");

        // AT MOST THREE: six good ones send the first three, best first.
        DetectorResult many = placed("S20", 0.5f, 3.0f);
        many.candidates = {"S1", "S5", "T20", "D20", "S18", "S12"};
        json mj = bodyOf(many);
        say(mj["candidates"] == json({"S1", "S5", "T20"}),
            "six ranked candidates send the best three and never a fourth (" + mj.dump() + ")");

        // TRANSLATED, through the seam `sector` goes through.
        DetectorResult words = dart("S17");
        words.candidates = {"BULL", "OUTER", "MISS"};
        say(bodyOf(words)["candidates"] == json({"Bull", "25", "None"}),
            "BULL, OUTER and MISS are sent as Bull, 25 and None (" + bodyOf(words).dump() + ")");

        // NEVER THE PUBLISHED SECTOR, compared by place (S20 and s20 are one twenty), and a
        // dropped entry does not use up one of the three.
        DetectorResult own = dart("S20");
        own.candidates = {"S20", "s20", "X5", "S21", "OUTER", "25", "T20", "D20", "S1"};
        say(bodyOf(own)["candidates"] == json({"25", "T20", "D20"}),
            "the dart's own place, untranslatable words and a repeat after translation are "
            "dropped before the cap, not after (" + bodyOf(own).dump() + ")");
        DetectorResult bull = dart("BULL");
        bull.candidates = {"Bull", "BULL", "OUTER"};
        say(bodyOf(bull)["candidates"] == json({"25"}), "a Bull is never offered as the Bull's own fix");
        DetectorResult miss = dart("MISS");
        miss.candidates = {"MISS", "None"};
        say(!bodyOf(miss).contains("candidates"), "a list that is all the dart's own place sends no key");

        // EVERY body the seam builds from any list of these words is one the door admits and
        // keeps whole: an array of at most 3, each readable, unique, and not the dart's place.
        const std::vector<std::string> vocabulary = {"S1", "s1", "D20", "T20", "S20", "BULL", "OUTER", "MISS",
                                                     "25", "Bull", "None", "S0", "S21", "T", "END", "", "s20", "T5"};
        int bad = 0, sent = 0;
        std::string first;
        for (const std::string &published : vocabulary)
        {
            for (size_t start = 0; start < vocabulary.size(); start++)
            {
                for (size_t len = 0; len <= 8; len++)
                {
                    DetectorResult d = dart(published);
                    for (size_t k = 0; k < len; k++)
                    {
                        d.candidates.push_back(vocabulary[(start + k * 5) % vocabulary.size()]);
                    }
                    TurnausClient::DetectionBody db = TurnausClient::detectionBody(REF, d);
                    if (db.json.empty())
                    {
                        continue;
                    }
                    json dj = json::parse(db.json);
                    if (!dj.contains("candidates"))
                    {
                        continue;
                    }
                    sent++;
                    std::string own_place = dj["sector"].get<std::string>();
                    if (!own_place.empty() && own_place[0] == 's')
                    {
                        own_place[0] = 'S';
                    }
                    bool ok = dj["candidates"].is_array() && dj["candidates"].size() >= 1 &&
                              dj["candidates"].size() <= 3 && theDoor(db.json).accepted &&
                              (int)dj["candidates"].size() == db.candidates_sent;
                    std::vector<std::string> seen;
                    for (const json &c : dj["candidates"])
                    {
                        std::string w = c.is_string() ? c.get<std::string>() : "";
                        if (!w.empty() && w[0] == 's')
                        {
                            w[0] = 'S';
                        }
                        ok = ok && std::regex_match(w, server_pattern) && w != own_place &&
                             std::find(seen.begin(), seen.end(), w) == seen.end();
                        seen.push_back(w);
                    }
                    if (!ok)
                    {
                        bad++;
                        if (first.empty())
                        {
                            first = db.json;
                        }
                    }
                }
            }
        }
        say(sent > 0 && bad == 0, "every candidates list the seam sends is admitted and kept whole by #1720's door (" +
                                      std::to_string(sent) + " sent, " + std::to_string(bad) + " bad" +
                                      (first.empty() ? "" : ", first: " + first) + ")");
    }

    // ---- #1721: THE RANKING, from what scoring already knew ------------------------------
    {
        using dart_candidates::Evidence;
        using dart_candidates::rank;
        auto list = [](const std::vector<std::string> &v)
        {
            std::string s;
            for (const std::string &w : v)
            {
                s += (s.empty() ? "" : ",") + w;
            }
            return s;
        };

        // A geometric T20 sitting 1 mm inside the treble's outer wire, near the middle of
        // the wedge: the single across that wire first, then the single inside (8 mm), then
        // the wedges (~17 mm of arc away).
        Evidence g;
        g.published = "T20";
        g.ring = "triple";
        g.segment = 20;
        g.wedge_read = true;
        g.radius_known = true;
        g.radius = 106.0f / 170.0f;
        g.angle_known = true;
        g.angle = 2.0f;
        say(list(rank(g)) == "S20,T1,T5", "a geometric T20 by its outer wire: S20, then the nearer wedge (" +
                                              list(rank(g)) + ")");
        // The same dart, flagged across a wedge wire, with a camera that read T1: the flag
        // first, the vote's runner-up next, then the nearest wire.
        Evidence gf = g;
        gf.alternative = "T5";
        gf.others = {{"T20", false}, {"T1", false}, {"T20", false}};
        say(list(rank(gf)) == "T5,T1,S20", "flag, then runner-up, then nearest wire (" + list(rank(gf)) + ")");

        // Runners-up: most cameras first, an asserted reading after every measured one.
        Evidence v;
        v.published = "S3";
        v.ring = "single";
        v.segment = 3;
        v.others = {{"S20", true}, {"S19", false}, {"S17", false}, {"S17", false}, {"S3", false}};
        say(list(rank(v)).rfind("S17,S19,S20,", 0) == 0,
            "runners-up by camera count, asserted last (" + list(rank(v)) + ")");

        // A string-vote dart with NO millimetres and an unread angle, wedge measured: the
        // treble, then both wedges anticlockwise first, then the double and the 25.
        Evidence s;
        s.published = "S11";
        s.ring = "single";
        s.segment = 11;
        s.wedge_read = true;
        say(list(rank(s)) == "T11,S8,S14,D11,OUTER",
            "a vote dart with no millimetres still gets its neighbours (" + list(rank(s)) + ")");

        // An ASSERTED 20 (#1487): ring neighbours only, ordered by the radius the ruler gave.
        Evidence a;
        a.published = "S20";
        a.ring = "single";
        a.segment = 20;
        a.wedge_read = false;
        a.radius_known = true;
        a.radius = 155.0f / 170.0f;
        a.angle_known = true;
        a.angle = 8.5f;
        say(list(rank(a)) == "D20,T20", "an asserted 20 gets the rings and never the 1 or the 5 (" +
                                            list(rank(a)) + ")");

        Evidence bl;
        bl.published = "BULL";
        bl.ring = "bull";
        say(list(rank(bl)) == "OUTER", "a BULL gets the 25");
        Evidence ob;
        ob.published = "OUTER";
        ob.ring = "outer";
        ob.wedge_read = true;
        ob.radius_known = true;
        ob.radius = 14.0f / 170.0f;
        ob.angle_known = true;
        ob.angle = 90.0f; // the 6
        say(list(rank(ob)) == "S6,BULL", "a 25 near its outer wire: the single it points into, then the BULL (" +
                                             list(rank(ob)) + ")");
        Evidence d;
        d.published = "D16";
        d.ring = "double";
        d.segment = 16;
        say(list(rank(d)).rfind("S16,MISS", 0) == 0, "a double gets its single and the MISS (" + list(rank(d)) + ")");
        Evidence m;
        m.published = "MISS";
        m.wedge_read = true;
        m.radius_known = true;
        m.radius = 172.0f / 170.0f;
        m.angle_known = true;
        m.angle = 0.0f;
        say(list(rank(m)) == "D20", "a measured MISS just past the double gets that double");
        Evidence nothing;
        nothing.published = "MISS";
        say(rank(nothing).empty(), "a MISS with no place and no other camera gets nothing, so sends no key");

        // End to end: the ranking into the body, before (no key) and after.
        struct Case
        {
            const char *name;
            Evidence e;
        };
        for (const Case &c : {Case{"geometric T20", gf}, Case{"vote S11, no mm", s}, Case{"asserted S20", a},
                              Case{"OUTER", ob}, Case{"D16", d}})
        {
            DetectorResult dr = dart(c.e.published);
            const std::string before = TurnausClient::detectionBody(REF, dr).json;
            dr.candidates = rank(c.e);
            const TurnausClient::DetectionBody after = TurnausClient::detectionBody(REF, dr);
            std::cout << "  BODY " << c.name << "\n    before " << before << "\n    after  " << after.json
                      << std::endl;
            say(after.candidates_sent >= 1 && after.candidates_sent <= 3 && theDoor(after.json).accepted,
                std::string(c.name) + ": the ranked list reaches the body, capped and admitted");
        }
    }

    // ---- EVERY body this seam can produce satisfies the door ---------------------------
    // #1347's closing argument: nothing it lets out can meet a 422, so a dart is never
    // lost between a green detector and a green server.
    {
        int refused = 0;
        std::string first;
        const float radii[] = {-1.0f, 0.0f, 0.00004f, 0.25f, 0.5f, 0.9999f, 1.0f, 1.19996f,
                               1.2f, 1.20004f, 1.20006f, 1.25f, 3.0f};
        const float angles[] = {-1.0f, 0.0f, 0.00004f, 9.0f, 179.99995f, 180.0f, 359.0f,
                                359.99985f, 359.9999f, 359.99995f,
                                std::nextafter(360.0f, 0.0f)};
        for (const std::string &score : {"S1", "T20", "D18", "BULL", "OUTER", "MISS", "25", "Bull", "None"})
        {
            for (float radius : radii)
            {
                for (float angle : angles)
                {
                    for (int known = 0; known < 4; known++)
                    {
                        DetectorResult r = dart(score);
                        r.board_radius_known = (known & 1) != 0;
                        r.board_angle_known = (known & 2) != 0;
                        r.board_radius = radius;
                        r.board_angle = angle;
                        TurnausClient::DetectionBody b = TurnausClient::detectionBody(REF, r);
                        if (b.json.empty())
                        {
                            continue;
                        }
                        DoorVerdict v = theDoor(b.json);
                        if (!v.accepted)
                        {
                            refused++;
                            if (first.empty())
                            {
                                first = b.json + " -- " + v.because;
                            }
                        }
                    }
                }
            }
        }
        say(refused == 0, "every body the seam can build is admitted by #1365's door ("
                              + std::to_string(refused) + " refused" +
                              (first.empty() ? "" : ", first: " + first) + ")");
    }

    std::cout << std::endl
              << "checks " << checks << "   failed " << failures.size() << std::endl;
    for (const std::string &f : failures)
    {
        std::cout << "  - " << f << std::endl;
    }
    return failures.empty() ? 0 : 1;
}

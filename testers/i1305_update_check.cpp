// #1305: what --check-update decides, measured. No camera, no console, no network.
//
// THE ONE THAT CARRIES THE SLICE is `tamper`. good.json and tampered.json differ by ONE
// BYTE of the signed payload -- v9.9.9 against v9.9.8 -- and carry the same signature, so
// the tampered one is a perfectly well-formed manifest naming a perfectly plausible
// version. A build that did not verify would read it and say an update is available.
// That is deliberate: a check that only asserts a GOOD manifest is accepted passes with
// the verification deleted, and would have proved nothing at all.
//
// The mutation that proves it is recorded at the bottom of this file, and it is one line.
//
//   testers/i1305_check.sh [worktree]

#include "update/update_check.hpp"
#include "update/update_channel.hpp"
#include "update/manifest.hpp"
#include "update/sha256.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool passed, const std::string &what)
{
    checks++;
    if (!passed)
    {
        failures++;
    }
    std::printf("%s %s\n", passed ? "ok  " : "FAIL", what.c_str());
}

static std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static bool says(const std::vector<console_prompt::Text> &lines, const std::string &needle)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].fi.find(needle) != std::string::npos || lines[i].en.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/** A fetch that answers whatever the test says, so a check needs no network at all. */
static update_check::Fetch answering(int status, const std::string &body, const std::string &error = "")
{
    return [status, body, error](const std::string &, const std::string &)
    {
        odhttp::Response response;
        response.status = status;
        response.body = body;
        response.transport_error = error;
        return response;
    };
}

int main(int argc, char **argv)
{
    const std::string fixtures = argc > 1 ? std::string(argv[1]) : std::string("testers/fixtures1305");
    const std::string work = argc > 2 ? std::string(argv[2]) : std::string("/tmp/i1305-work");

    // ---------------------------------------------------------------- the digest
    check(od_sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256: the FIPS 180-4 vector");
    check(od_sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256: the empty string");

    // ---------------------------------------------------------------- the anchor
    std::string anchor_hex = slurp(fixtures + "/anchor.hex");
    while (!anchor_hex.empty() && (anchor_hex.back() == '\n' || anchor_hex.back() == '\r'))
    {
        anchor_hex.pop_back();
    }
    std::vector<update_manifest::Anchor> anchors;
    anchors.push_back(update_manifest::anchorFromHex(anchor_hex));
    check(anchors[0].size() == 64, "anchor: 128 hexadecimal characters are 64 bytes");
    check(update_manifest::anchorFromHex("04" + anchor_hex).empty(), "anchor: the leading 04 is not part of it");
    check(update_manifest::anchorFromHex(std::string(128, 'z')).empty(), "anchor: not hexadecimal is not an anchor");

    const std::string good = slurp(fixtures + "/good.json");
    const std::string tampered = slurp(fixtures + "/tampered.json");
    const std::string stranger = slurp(fixtures + "/stranger.json");
    const std::string beta = slurp(fixtures + "/beta.json");
    const std::string garbage = slurp(fixtures + "/garbage.json");
    check(!good.empty() && !tampered.empty() && good != tampered, "fixtures: there are two manifests and they differ");

    // ---------------------------------------------------------------- the positive control
    update_manifest::Read accepted = update_manifest::read(good, anchors);
    check(accepted.ok, "good: an OpenSSL signature over these bytes verifies");
    check(accepted.version == "v9.9.9", "good: the version is read out of the verified payload");
    check(accepted.channel == "stable", "good: the channel is inside the signature");
    check(accepted.size == 39845120, "good: the size is read");
    check(accepted.url.rfind("https://", 0) == 0, "good: the url is https");

    // ---------------------------------------------------------------- THE CRITERION
    update_manifest::Read bent = update_manifest::read(tampered, anchors);
    check(!bent.ok, "tamper: one byte changed in the payload is refused");
    check(bent.refusal == update_manifest::Refusal::NotVerified,
          "tamper: refused BY NAME as a signature that does not verify");
    check(bent.version.empty(), "tamper: nothing was read out of a payload that was not vouched for");

    // The tamper is not a broken document. This is what makes the check above mean
    // something: with the verification deleted, the manifest below reads clean.
    {
        std::string payload_base64;
        size_t open_quote = tampered.find("\"payload\"");
        open_quote = tampered.find('"', tampered.find(':', open_quote) + 1);
        size_t close_quote = tampered.find('"', open_quote + 1);
        payload_base64 = tampered.substr(open_quote + 1, close_quote - open_quote - 1);
        std::string payload;
        check(update_manifest::detail::base64Decode(payload_base64, payload), "tamper: its payload is still base64");
        check(payload.find("\"version\":\"v9.9.8\"") != std::string::npos,
              "tamper: it is a well-formed manifest naming v9.9.8, which is why refusing it is a measurement");
    }

    update_manifest::Read outsider = update_manifest::read(stranger, anchors);
    check(!outsider.ok && outsider.refusal == update_manifest::Refusal::NotVerified,
          "stranger: a valid signature by a key this build does not hold is refused");

    // ---------------------------------------------------------------- two different sentences
    update_manifest::Read unvouched = update_manifest::read(good, std::vector<update_manifest::Anchor>());
    check(unvouched.refusal == update_manifest::Refusal::NoAnchor,
          "no key: 'nothing can be vouched for' is not 'this was not signed by us'");
    check(update_check::refusalText(update_manifest::Refusal::NoAnchor, "").en !=
              update_check::refusalText(update_manifest::Refusal::NotVerified, "").en,
          "no key: and the two are not the same sentence");

    update_manifest::Read wrong_channel = update_manifest::readForChannel(beta, anchors, "stable");
    check(!wrong_channel.ok && wrong_channel.refusal == update_manifest::Refusal::WrongChannel,
          "channel: a beta manifest served at the stable address is refused");
    check(wrong_channel.detail == "beta", "channel: and the refusal names the channel it was signed for");
    check(update_manifest::readForChannel(beta, anchors, "beta").ok, "channel: the same manifest is fine on beta");

    check(update_manifest::read(garbage, anchors).refusal == update_manifest::Refusal::NotAnEnvelope,
          "shape: something that is not an envelope is refused as one");
    check(update_manifest::read("{", anchors).refusal == update_manifest::Refusal::NotJson,
          "shape: something that is not JSON is refused as that");
    check(update_manifest::read("{\"payload\":\"!!\",\"signature\":\"!!\"}", anchors).refusal ==
              update_manifest::Refusal::NotBase64,
          "shape: base64 is read strictly");

    // ---------------------------------------------------------------- what is printed
    const std::string running = "v1.0.0";
    check(update_check::pathFor("stable") == "/updates/opendartboard/stable.json", "address: the route's own path");

    update_check::Answer available =
        update_check::ask("https://turnaus.example", "stable", running, anchors, answering(200, good));
    check(available.kind == update_check::Kind::Available, "published: a different version is an update");
    check(available.published_version == "v9.9.9", "published: the version is named");
    std::vector<console_prompt::Text> said = update_check::lines(available);
    check(says(said, running) && says(said, "v9.9.9"), "published: both versions are printed");
    check(says(said, "downloads nothing and installs nothing"), "published: and it says it did nothing");
    check(said.size() >= 2, "published: every line is said in Finnish and English");
    for (size_t i = 0; i < said.size(); i++)
    {
        check(!said[i].fi.empty() && !said[i].en.empty() && said[i].fi != said[i].en,
              "published: line " + std::to_string(i + 1) + " is said twice, in two languages");
    }

    update_check::Answer same =
        update_check::ask("https://turnaus.example", "stable", "v9.9.9", anchors, answering(200, good));
    check(same.kind == update_check::Kind::UpToDate, "published: the same version is no update");
    check(says(update_check::lines(same), "There is no update"), "published: and it says so");

    update_check::Answer refused =
        update_check::ask("https://turnaus.example", "stable", running, anchors, answering(200, tampered));
    check(refused.kind == update_check::Kind::Refused, "refusal: a tampered manifest does not become an answer");
    check(refused.published_version.empty(), "refusal: and no version is published from it");
    std::vector<console_prompt::Text> refusal_lines = update_check::lines(refused);
    check(says(refusal_lines, "SIGNATURE VERIFICATION FAILED"), "refusal: the tester is told verification failed");
    check(!says(refusal_lines, "There is no update"), "refusal: and is NOT told there was no update");
    check(says(refusal_lines, "The board keeps running"), "refusal: the board is still running");
    check(update_check::exitCode(refused) == 1, "refusal: and the run is a failure");

    update_check::Answer nothing_answered = update_check::ask("https://turnaus.example", "stable", running, anchors,
                                                              answering(0, "", "Connection refused"));
    check(nothing_answered.kind == update_check::Kind::Unreachable, "unreachable: nothing answered");
    check(says(update_check::lines(nothing_answered), "The board keeps running"),
          "unreachable: the board keeps running and says so");
    check(says(update_check::lines(nothing_answered), "Connection refused"), "unreachable: and says why");
    check(update_check::exitCode(nothing_answered) == 0, "unreachable: our downtime is not their downtime");

    update_check::Answer not_published =
        update_check::ask("https://turnaus.example", "stable", running, anchors, answering(404, "Not Found"));
    check(not_published.kind == update_check::Kind::NotPublished, "404: a deployment publishing nothing yet");
    check(says(update_check::lines(not_published), "The board keeps running"), "404: the board keeps running");
    check(update_check::exitCode(not_published) == 0, "404: and the run is not a failure");

    // ---------------------------------------------------------------- the channel
    const std::string credentials = work + "/cfg/credentials.json";
    const std::string channel_file = update_channel::fileBeside(credentials);
    check(channel_file == work + "/cfg/channel.json", "channel: kept beside the credential");
    check(update_channel::fileBeside("C:\\Users\\u\\AppData\\Roaming\\OpenDartboard\\credentials.json") ==
              "C:\\Users\\u\\AppData\\Roaming\\OpenDartboard\\channel.json",
          "channel: and beside it on Windows too");

    check(update_channel::load(work + "/cfg/nothing-here.json") == "stable", "channel: stable when nothing says");
    check(update_channel::save(channel_file, "beta"), "channel: beta is one of the two");
    check(update_channel::load(channel_file) == "beta", "channel: and survives being read back");
    check(!update_channel::save(channel_file, "nightly"), "channel: a third word is refused");
    check(update_channel::load(channel_file) == "beta", "channel: and the refusal changed nothing");
    {
        std::ofstream out(channel_file.c_str(), std::ios::binary | std::ios::trunc);
        out << "{\"version\":1,\"channel\":\"nightly\"}\n";
    }
    check(update_channel::load(channel_file) == "stable", "channel: a file naming a channel the route refuses reads stable");
    {
        std::ofstream out(channel_file.c_str(), std::ios::binary | std::ios::trunc);
        out << "not json at all";
    }
    check(update_channel::load(channel_file) == "stable", "channel: and so does one that will not parse");
    check(update_channel::isKnown("stable") && update_channel::isKnown("beta") && !update_channel::isKnown("edge"),
          "channel: the two the route constrains and no more");

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// THE MUTATION THAT PROVES THE REFUSAL (#1305's criterion, run by testers/i1305_check.sh
// --mutate). In src/update/manifest.hpp, replace
//
//     if (!verified)
//     {
//         return refuse(Refusal::NotVerified);
//     }
//
// with nothing -- delete the verification and read the payload whatever the signature
// says. Then `tamper: one byte changed in the payload is refused` goes red, because the
// tampered manifest reads clean and names v9.9.8; so do the three checks beside it and
// the two about what a refusal prints. A check that stayed green through that deletion
// would be measuring nothing.

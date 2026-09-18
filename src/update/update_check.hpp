#pragma once
// #1305: --check-update. The whole trust path, end to end, with nothing at risk.
//
// It fetches the manifest this install's channel publishes, verifies it the way ADR-0077
// says, compares the version it names against this build's own, and says the answer in
// Finnish and English. IT DOWNLOADS NO ARTEFACT AND REPLACES NO FILE, and there is
// nothing in this header that could: the only address it asks is the manifest's, the only
// thing it writes is the channel and only when somebody is setting it, and `url` and
// `sha256` are read out of the verified payload so that they can be PRINTED.
//
// WHICH ADDRESS. ADR-0077 §5: the one the detector already resolves -- --turnaus, then
// OD_TURNAUS_URL, then the base_url the pairing wrote, then the compiled-in default
// (#822, #1257). main.cpp has resolved it before this is called and hands it in. There is
// no update-only address and this file introduces none.
//
// WHAT HAPPENS WHEN IT CANNOT ASK. The board keeps running and says so. A deployment that
// publishes nothing answers 404 -- which is what a developer's http://localhost:8000
// answers too, and what production answers until Turnaus' own #1302 has been run -- and
// our downtime is not a club's downtime. That is why every sentence below that is not
// good news ends by saying the board is still running and nothing was fetched.
//
// A VERSION IS COMPARED BY DIFFERENCE AND NEVER BY ORDERING (ADR-0077). Nothing here
// parses a version string, so nothing here can disagree about what v1.10 means next to
// v1.9 -- and publishing a previous release stays the way a bad one is undone.

#include "manifest.hpp"
#include "update_channel.hpp"
#include "../communication/http_transport.hpp"
#include "../utils/console_prompt.hpp"

#include <functional>
#include <string>
#include <vector>

namespace update_check
{
    using console_prompt::Text;

    /** Where a channel's manifest is, on whatever Turnaus this board resolves to. */
    inline std::string pathFor(const std::string &channel) { return "/updates/opendartboard/" + channel + ".json"; }

    /** How a check ended. Five outcomes and each is a different thing to be told. */
    enum class Kind
    {
        Unreachable,  // nothing answered: DNS, connect, timeout, TLS, a build with none
        NotPublished, // something answered and it was not a manifest: 404 and every other status
        Refused,      // a manifest arrived and this build will not trust it
        UpToDate,     // verified, and it names the version already running
        Available,    // verified, and it names a different one
    };

    struct Answer
    {
        Kind kind = Kind::Unreachable;
        std::string running_version;
        std::string channel;
        std::string address;

        int status = 0;                 // what the address answered, 0 when nothing did
        std::string transport_error;    // never carries a request header, so never a token
        update_manifest::Refusal refusal = update_manifest::Refusal::None;
        std::string refusal_detail;

        std::string published_version; // only ever read out of a verified payload
        std::string published_url;
        std::string published_sha256;

        bool differs() const { return kind == Kind::Available; }
    };

    /** How the bytes are fetched. Handed in so a check can be run with no network at all. */
    typedef std::function<odhttp::Response(const std::string &address, const std::string &path)> Fetch;

    /** The real one: one GET, no credential, short timeouts. */
    inline odhttp::Response fetchOverHttp(const std::string &address, const std::string &path)
    {
        odhttp::Url url = odhttp::parseUrl(address);
        if (!url.valid)
        {
            odhttp::Response response;
            response.transport_error = "malformed base url";
            return response;
        }
        return odhttp::get(url, path, /*connect*/ 5, /*read*/ 10);
    }

    /**
     * Ask, verify, compare. Writes nothing, downloads nothing, and never throws.
     */
    inline Answer ask(const std::string &address, const std::string &channel, const std::string &running_version,
                      const std::vector<update_manifest::Anchor> &anchors, const Fetch &fetch)
    {
        Answer answer;
        answer.running_version = running_version;
        answer.channel = channel;
        answer.address = address;

        odhttp::Response response = fetch(address, pathFor(channel));
        answer.status = response.status;
        answer.transport_error = response.transport_error;
        if (!response.reached_a_server())
        {
            answer.kind = Kind::Unreachable;
            return answer;
        }
        if (response.status != 200)
        {
            answer.kind = Kind::NotPublished;
            return answer;
        }

        update_manifest::Read manifest = update_manifest::readForChannel(response.body, anchors, channel);
        if (!manifest.ok)
        {
            answer.kind = Kind::Refused;
            answer.refusal = manifest.refusal;
            answer.refusal_detail = manifest.detail;
            return answer;
        }

        answer.published_version = manifest.version;
        answer.published_url = manifest.url;
        answer.published_sha256 = manifest.sha256;
        answer.kind = manifest.version == running_version ? Kind::UpToDate : Kind::Available;
        return answer;
    }

    /**
     * Why a manifest was refused, in one sentence a tester can act on.
     *
     * The first of these is the one this slice exists for: a signature that does not
     * verify is said as a signature that does not verify. "There is no update" is a
     * different sentence, it is said somewhere else, and a board must never be able to
     * confuse the two -- a tampered manifest that reads as "you are up to date" is an
     * attacker's best outcome and a tester's worst report.
     */
    inline Text refusalText(update_manifest::Refusal refusal, const std::string &detail)
    {
        switch (refusal)
        {
        case update_manifest::Refusal::NotVerified:
            return {"ALLEKIRJOITUKSEN TARKISTUS EPÄONNISTUI: yksikään tämän ohjelman avaimista ei kelpuuta "
                    "päivitystiedostoa, joten sitä ei ole allekirjoittanut me - tai ei näiden tavujen yli.",
                    "SIGNATURE VERIFICATION FAILED: no key in this build verifies the manifest, so it was not signed "
                    "by us - or not signed over these bytes."};
        case update_manifest::Refusal::NoAnchor:
            return {"Tässä ohjelmaversiossa ei ole yhtään päivitysavainta, joten mitään ei voi varmentaa.",
                    "This build has no update key compiled in, so nothing can be vouched for."};
        case update_manifest::Refusal::NotJson:
            return {"Vastaus ei ole JSONia.", "What answered is not JSON."};
        case update_manifest::Refusal::NotAnEnvelope:
            return {"Vastaus ei ole allekirjoitettu kuori: päivitystiedosto on {\"payload\": \"<base64>\", "
                    "\"signature\": \"<base64>\"}.",
                    "What answered is not a signed envelope: a manifest is {\"payload\": \"<base64>\", "
                    "\"signature\": \"<base64>\"}."};
        case update_manifest::Refusal::NotBase64:
            return {"Kuoren payload tai allekirjoitus ei ole kelvollista base64:aa.",
                    "The envelope's payload or signature is not valid base64."};
        case update_manifest::Refusal::PayloadNotJson:
            return {"Allekirjoitus kelpaa, mutta sisältö ei ole JSONia.",
                    "Its signature verifies but its payload is not JSON."};
        case update_manifest::Refusal::PayloadNotAnObject:
            return {"Allekirjoitus kelpaa, mutta sisältö ei ole olio.",
                    "Its signature verifies but its payload is not an object."};
        case update_manifest::Refusal::FieldMissing:
            return {"Sisällössä ei ole kenttää " + detail + ".", "Its payload states no " + detail + "."};
        case update_manifest::Refusal::SizeMissing:
            return {"Sisällössä ei ole kokoa, eikä lataukselta jonka pituutta ei tiedetä voi kieltäytyä ennen kuin "
                    "se on tehty.",
                    "Its payload states no size, and a download with no expected length cannot be refused before it "
                    "is made."};
        case update_manifest::Refusal::DigestMalformed:
            return {"Sisällön sha256 ei ole 64 pientä heksamerkkiä.",
                    "Its sha256 is not 64 lowercase hexadecimal characters."};
        case update_manifest::Refusal::UrlNotHttps:
            return {"Sisällön url ei ole https, eikä taulua lähetetä hakemaan julkaisua salaamattomana.",
                    "Its url is not https, and a board may not be sent to fetch a release in the clear."};
        case update_manifest::Refusal::WrongChannel:
            return {"Päivitystiedosto on allekirjoitettu kanavalle " + detail + ", ei tälle kanavalle.",
                    "The manifest is signed for the " + detail + " channel, so it was signed for somewhere else."};
        case update_manifest::Refusal::None:
        default:
            return {"Päivitystiedostoa ei hyväksytty.", "The manifest was not accepted."};
        }
    }

    /** The last line of every answer that is not good news: nothing happened to this board. */
    inline Text stillRunning()
    {
        return {"Taulu jää käyntiin. Mitään ei ladattu eikä mitään korvattu.",
                "The board keeps running. Nothing was downloaded and nothing was replaced."};
    }

    /** Everything --check-update says, in order. Finnish line then English line, always. */
    inline std::vector<Text> lines(const Answer &answer)
    {
        std::vector<Text> said;
        said.push_back({"Tämä taulu käyttää versiota " + answer.running_version + " (kanava: " + answer.channel + ").",
                        "This board is running version " + answer.running_version + " (channel: " + answer.channel +
                            ")."});

        switch (answer.kind)
        {
        case Kind::Unreachable:
            said.push_back({"Julkaistua versiota ei saatu osoitteesta " + answer.address + ": " +
                                (answer.transport_error.empty() ? std::string("ei vastausta") : answer.transport_error) +
                                ".",
                            "The published version could not be read from " + answer.address + ": " +
                                (answer.transport_error.empty() ? std::string("nothing answered")
                                                                : answer.transport_error) +
                                "."});
            said.push_back(stillRunning());
            break;
        case Kind::NotPublished:
            said.push_back({answer.address + " ei julkaise kanavaa " + answer.channel + " (vastaus " +
                                std::to_string(answer.status) + "), joten julkaistua versiota ei ole tiedossa.",
                            answer.address + " publishes nothing on the " + answer.channel + " channel (it answered " +
                                std::to_string(answer.status) + "), so there is no published version to compare."});
            said.push_back(stillRunning());
            break;
        case Kind::Refused:
            said.push_back(refusalText(answer.refusal, answer.refusal_detail));
            said.push_back({"Tämä EI tarkoita, ettei päivitystä olisi: julkaistua versiota ei tiedetä, koska "
                            "päivitystiedostoon ei voi luottaa.",
                            "This does NOT mean there is no update: the published version is unknown, because the "
                            "manifest cannot be trusted."});
            said.push_back(stillRunning());
            break;
        case Kind::UpToDate:
            said.push_back({"Kanava " + answer.channel + " julkaisee version " + answer.published_version +
                                ", joka on sama kuin tässä taulussa. Päivitystä ei ole.",
                            "The " + answer.channel + " channel publishes version " + answer.published_version +
                                ", which is the same as this board's. There is no update."});
            break;
        case Kind::Available:
            said.push_back({"Kanava " + answer.channel + " julkaisee version " + answer.published_version +
                                ", joka on eri kuin tässä taulussa oleva " + answer.running_version +
                                ". Päivitys on saatavilla.",
                            "The " + answer.channel + " channel publishes version " + answer.published_version +
                                ", which differs from this board's " + answer.running_version +
                                ". An update is available."});
            said.push_back({"Julkaisu on osoitteessa " + answer.published_url + " (sha256 " + answer.published_sha256 +
                                ").",
                            "The release is at " + answer.published_url + " (sha256 " + answer.published_sha256 +
                                ")."});
            said.push_back({"Tämä komento ei lataa eikä asenna mitään.",
                            "This command downloads nothing and installs nothing."});
            break;
        }
        return said;
    }

    /** True when the answer is one an operator should be able to see in an exit code. */
    inline int exitCode(const Answer &answer)
    {
        // A refusal is a failure; everything else, including a board that could not reach
        // Turnaus, is not. Our downtime is not their downtime, and a scheduled check that
        // went red every night a deployment was restarted would be a check nobody reads.
        return answer.kind == Kind::Refused ? 1 : 0;
    }
}

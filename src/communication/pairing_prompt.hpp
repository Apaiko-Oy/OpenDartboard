#pragma once
// #1259: a person at the console types six digits and the board is paired and scoring.
//
// WHEN IT ASKS. Only in an interactive console (console_prompt::isInteractiveConsole, the
// one rule #1258 wrote) and only in two moments:
//
//   AT START, when no credential is stored and no pairing flag was given. The question is
//   asked before a camera opens: an unpaired board has no credential to beat with, so
//   nothing a Station could see is lost by asking first, and a code expires in ten minutes
//   while three cameras and a calibration take seconds. The answer is written where --pair
//   and --pair-contest write it, and the program goes straight on to the cameras (and
//   #1258's question, if one does not open) and to scoring. No restart.
//
//   WHILE RUNNING, when the credential it was scoring with is refused (a club revoked the
//   board, #1246) or its Casual Contest ended with no club binding underneath (#891). The
//   board keeps scoring locally while it asks; pushes resume once a new code is taken.
//
// ONE CODE, EITHER DOOR. Six digits, or it is refused here and nothing is sent. The club's
// door is tried first; only its one refusal (422 -- never minted, spent or expired, which
// #820 collapses on purpose) sends the same digits to the Casual Contest door. Two doors
// that do not read each other's tables cannot collide on a code, so trying the second
// after the first's refusal can pair the wrong board only if the same six digits are live
// at both at once. Anything that is not a refusal -- unreachable, 429, another status --
// is said and the question asked again, without spending a second attempt of the shared
// `api-pairing` allowance on the other door.
//
// NEVER LOGGED: the digits typed. Every answer a door gave is logged by status.

#include "turnaus_client.hpp"
#include "../utils/console_prompt.hpp"
#include "../utils/logging.hpp"
#include "../utils/signals.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace pairing_prompt
{
    using console_prompt::Text;
    using console_prompt::Verdict;
    using Redemption = TurnausClient::Redemption;

    inline std::string trimmed(const std::string &raw)
    {
        const char *space = " \t\r\n";
        size_t first = raw.find_first_not_of(space);
        if (first == std::string::npos)
        {
            return "";
        }
        size_t last = raw.find_last_not_of(space);
        return raw.substr(first, last - first + 1);
    }

    inline bool isSixDigits(const std::string &code)
    {
        return code.size() == 6 && code.find_first_not_of("0123456789") == std::string::npos;
    }

    inline Text question()
    {
        return {"Syötä taulun liitoskoodi (kuusi numeroa) ja paina Enter:",
                "Type the board's pairing code (six digits) and press Enter:"};
    }

    /** What to say when this build may not send a code to this address at all. */
    inline Text blockedText(TurnausClient::PairingBlock block, const std::string &address)
    {
        switch (block)
        {
        case TurnausClient::PairingBlock::BadAddress:
            return {"Taulua ei voi liittää: Turnauksen osoite " + address + " ei ole kelvollinen.",
                    "The board cannot be paired: the Turnaus address " + address + " is not one this build can use."};
        case TurnausClient::PairingBlock::NoTls:
            return {"Taulua ei voi liittää: tässä versiossa ei ole salattua yhteyttä osoitteeseen " + address + ".",
                    "The board cannot be paired: this build has no TLS transport for " + address + "."};
        case TurnausClient::PairingBlock::Plaintext:
            return {"Taulua ei voi liittää: " + address +
                        " on salaamaton osoite. Salli se vain testissä (--allow-plaintext tai OD_ALLOW_PLAINTEXT=1).",
                    "The board cannot be paired: " + address +
                        " is not encrypted. Allow it only in a lab (--allow-plaintext or OD_ALLOW_PLAINTEXT=1)."};
        case TurnausClient::PairingBlock::NoConfigDir:
        case TurnausClient::PairingBlock::None:
        default:
            return {"Taulua ei voi liittää: tunnukselle ei ole tallennuspaikkaa.",
                    "The board cannot be paired: there is nowhere to keep a credential."};
        }
    }

    /** What to say about an answer that is not a refusal and not a pairing. */
    inline Text failureText(const Redemption &r, const std::string &address)
    {
        switch (r.kind)
        {
        case Redemption::Kind::Unreachable:
            return {"Turnausta ei tavoitettu osoitteesta " + address + " (" + r.detail +
                        "). Tarkista verkkoyhteys ja yritä uudelleen.",
                    "Could not reach Turnaus at " + address + " (" + r.detail +
                        "). Check the network connection and try again."};
        case Redemption::Kind::RateLimited:
            if (r.retry_after_s > 0)
            {
                const std::string wait = std::to_string(r.retry_after_s);
                return {"Liian monta yritystä. Odota " + wait + " sekuntia ja yritä sitten uudelleen.",
                        "Too many attempts. Wait " + wait + " seconds, then try again."};
            }
            return {"Liian monta yritystä. Odota hetki ja yritä sitten uudelleen.",
                    "Too many attempts. Wait a moment, then try again."};
        case Redemption::Kind::NotKept:
            return {"Koodi hyväksyttiin, mutta tunnusta ei voitu tallentaa (katso loki). Pyydä uusi koodi.",
                    "The code was accepted, but the credential could not be kept (see the log). Ask for a new code."};
        case Redemption::Kind::Unexpected:
        default:
            return {"Turnaus (" + address + ") vastasi odottamattomasti: HTTP " + std::to_string(r.status) +
                        ". Yritä uudelleen.",
                    "Turnaus at " + address + " answered unexpectedly: HTTP " + std::to_string(r.status) +
                        ". Try again."};
        }
    }

    inline Text refusedAtBothDoors()
    {
        return {"Koodi ei kelpaa. Koodit vanhenevat kymmenessä minuutissa ja kelpaavat vain kerran: pyydä uusi koodi.",
                "The code was not accepted. Codes expire in ten minutes and are used once: ask for a new code."};
    }

    inline void logAnswer(const char *door, const Redemption &r)
    {
        std::string what;
        switch (r.kind)
        {
        case Redemption::Kind::Paired: what = "paired (HTTP 201)"; break;
        case Redemption::Kind::Refused: what = "refused (HTTP 422)"; break;
        case Redemption::Kind::Unreachable: what = "unreachable: " + r.detail; break;
        case Redemption::Kind::RateLimited:
            what = "rate-limited (HTTP 429, Retry-After " +
                   (r.retry_after_s >= 0 ? std::to_string(r.retry_after_s) + "s" : std::string("not named")) + ")";
            break;
        case Redemption::Kind::Unexpected: what = "answered HTTP " + std::to_string(r.status); break;
        case Redemption::Kind::Blocked: what = "not sent: the address may not be paired with"; break;
        case Redemption::Kind::NotKept: what = "accepted, but the credential was not kept"; break;
        }
        log_info(std::string("TURNAUS: a typed pairing code at the ") + door + " door: " + what);
    }

    /**
     * Ask until a code pairs this board or input ends. True when paired; the credential is
     * written and the client holds it. False when input ended, or when this address can
     * never be paired with from here (said once, not asked).
     */
    inline bool askAndPair(console_prompt::Console &console, TurnausClient &client, const std::string &address)
    {
        const TurnausClient::PairingBlock block = client.pairingBlock();
        if (block != TurnausClient::PairingBlock::None)
        {
            console.say(blockedText(block, address));
            return false;
        }

        TurnausClient::Door paired_at = TurnausClient::Door::Organisation;
        auto judge = [&](const std::string &raw) -> Verdict
        {
            const std::string code = trimmed(raw);
            if (!isSixDigits(code))
            {
                return Verdict::refuse({"Liitoskoodi on kuusi numeroa. Yritä uudelleen.",
                                        "A pairing code is six digits. Try again."});
            }

            Redemption club = client.redeem(TurnausClient::Door::Organisation, code);
            logAnswer("club", club);
            if (club.kind == Redemption::Kind::Paired)
            {
                paired_at = TurnausClient::Door::Organisation;
                return Verdict::accept();
            }
            if (club.kind != Redemption::Kind::Refused)
            {
                return Verdict::refuse(failureText(club, address));
            }

            // The club's one refusal, and only that, sends the digits to the second door.
            Redemption casual = client.redeem(TurnausClient::Door::Contest, code);
            logAnswer("Casual Contest", casual);
            if (casual.kind == Redemption::Kind::Paired)
            {
                paired_at = TurnausClient::Door::Contest;
                return Verdict::accept();
            }
            if (casual.kind == Redemption::Kind::Refused)
            {
                return Verdict::refuse(refusedAtBothDoors());
            }
            return Verdict::refuse(failureText(casual, address));
        };

        std::string answer;
        if (!console_prompt::askUntil(console, question(), judge, answer))
        {
            return false;
        }
        if (paired_at == TurnausClient::Door::Contest)
        {
            const std::string contest = std::to_string(client.contestId());
            console.say({"Taulu on liitetty Casual-peliin " + contest + ".",
                         "The board is paired to Casual Contest " + contest + "."});
        }
        else
        {
            console.say({"Taulu on liitetty seuraan (laite " + client.deviceId() + ").",
                         "The board is paired to its club (device " + client.deviceId() + ")."});
        }
        return true;
    }

    /** At start: no credential, no pairing flag, an interactive console. */
    inline void pairAtStart(console_prompt::Console &console, TurnausClient &client, const std::string &address)
    {
        console.say({"Tätä taulua ei ole liitetty Turnaukseen (" + address + ").",
                     "This board is not paired with Turnaus (" + address + ")."});
        if (!askAndPair(console, client, address))
        {
            console.say({"Taulua ei liitetty. Se laskee pisteet, mutta ei lähetä mitään Turnaukseen.",
                         "The board was not paired. It scores, but sends nothing to Turnaus."});
            log_info("TURNAUS: no pairing code was taken at the console");
        }
    }

    /**
     * The console while scoring runs on another thread. It waits for a line in slices, so
     * a shutdown (the cycle budget, SIGINT, SIGTERM) is never held up by a question nobody
     * is answering: once `leaving` is set or a signal is recorded, input counts as ended.
     */
    class WatchfulConsole : public console_prompt::StdConsole
    {
    public:
        explicit WatchfulConsole(const std::atomic<bool> &leaving) : leaving_(leaving) {}

        bool leavingNow() const { return leaving_.load() || signals::shutdownRequested() != 0; }

        bool readLine(std::string &line) override
        {
            for (;;)
            {
                if (leavingNow())
                {
                    return false;
                }
#ifdef _WIN32
                HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
                if (::WaitForSingleObject(in, 200) != WAIT_OBJECT_0)
                {
                    continue;
                }
                // Signalled for any input record, a mouse move or a focus change included.
                // Only a key press means somebody is typing a line; anything else is read
                // off and discarded so the wait does not spin on it.
                DWORD count = 0;
                if (!::GetNumberOfConsoleInputEvents(in, &count) || count == 0)
                {
                    continue;
                }
                std::vector<INPUT_RECORD> records(count);
                DWORD peeked = 0;
                ::PeekConsoleInputW(in, records.data(), count, &peeked);
                bool typing = false;
                for (DWORD i = 0; i < peeked; i++)
                {
                    if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown)
                    {
                        typing = true;
                        break;
                    }
                }
                if (!typing)
                {
                    DWORD discarded = 0;
                    ::ReadConsoleInputW(in, records.data(), peeked, &discarded);
                    continue;
                }
                return StdConsole::readLine(line);
#else
                if (std::cin.rdbuf()->in_avail() > 0)
                {
                    return StdConsole::readLine(line);
                }
                struct pollfd input = {STDIN_FILENO, POLLIN, 0};
                int ready = ::poll(&input, 1, 200);
                if (ready > 0)
                {
                    return StdConsole::readLine(line);
                }
                if (ready < 0 && errno != EINTR)
                {
                    return false;
                }
#endif
            }
        }

    private:
        const std::atomic<bool> &leaving_;
    };

    /**
     * Runs beside the Scorer in an interactive detector and nowhere else. When the client
     * reports that a running board lost its credential, the threads are quiesced, the board
     * says it was unpaired, asks, and resumes pushing with whatever it is paired to now.
     */
    inline void watchForUnpairing(TurnausClient &client, const std::string &address, const std::atomic<bool> &leaving)
    {
        WatchfulConsole console(leaving);
        while (!console.leavingNow())
        {
            if (client.takeUnpairing())
            {
                client.quiesce();
                console.say({"Turnaus ei enää hyväksy tämän taulun tunnusta: taulu on irrotettu.",
                             "Turnaus no longer accepts this board's credential: the board was unpaired."});
                console.say({"Taulu laskee edelleen, mutta mitään ei lähetetä Turnaukseen ennen uutta liitosta.",
                             "The board keeps scoring, but nothing reaches Turnaus until it is paired again."});
                if (askAndPair(console, client, address))
                {
                    client.resume();
                }
                else if (!console.leavingNow())
                {
                    log_info("TURNAUS: no new pairing code was taken at the console; nothing further is pushed");
                    return;
                }
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

// #1787: the log upload's offset bookkeeping, pure -- the same bytes are never posted
// twice, and a failed post re-posts from the same offset.
//
// `LogUploadLedger` holds one number: how many leading bytes of the log the server has
// acknowledged. It moves only on a success for the chunk that began exactly there, so a
// lost answer (the server took the chunk, the board never heard) re-posts the chunk and
// the server's 409 -- which names its length -- moves the board past it without a second
// copy. `sendable` cuts a chunk at its last newline so a half-written line waits for the
// next END, and `logChunkBody` is the JSON one post carries, held here to its fields.
//
// THE MUTATION, with the prediction stated first: a ledger told "posted" for a chunk that
// did not begin at its offset (the stale retry, the chunk after a gap) must move nothing
// -- and the same sequence run through a ledger that DID move (the count model this
// replaces: posted_ += length unconditionally) is shown to lose bytes, so the rule is
// seen to be load-bearing on every run (#1463).
//
//   g++ -std=c++17 -I src -I src/utils -I build/_deps/nlohmann_json-src/include \
//       -I build/_deps/httplib-src testers/i1787_upload_check.cpp \
//       src/communication/turnaus_client.cpp -o upload_check \
//       $(pkg-config --cflags --libs opencv4) -lpthread

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

/**
 * A model of #1786's route: it holds the bytes it has, appends a chunk only at its own
 * length, and answers 409 with that length otherwise. `lose_answer` is the lost 202.
 */
struct Server
{
    std::string held;
    int appended = 0;
    int refused = 0;
    /** Returns the status; on 409 `length` is what the answer names. */
    int post(uint64_t offset, const std::string &text, uint64_t &length)
    {
        if (offset != held.size())
        {
            refused++;
            length = held.size();
            return 409;
        }
        held += text;
        appended++;
        return 202;
    }
};

/**
 * One END's upload against `file`, the way TurnausClient::uploadLog walks it: read from
 * the ledger's offset, cut at a line, post, move on a 202, resync on a 409. `lose` makes
 * the next 202 go missing (the board hears nothing and moves nothing).
 */
static void endOfRound(LogUploadLedger &ledger, Server &server, const std::string &file, bool &lose, bool final = false)
{
    for (int i = 0; i < 16; i++)
    {
        const LogUploadLedger::Chunk chunk = ledger.next(file.size());
        if (chunk.length == 0)
        {
            return;
        }
        std::string bytes = file.substr((size_t)chunk.offset, (size_t)chunk.length);
        const uint64_t send = LogUploadLedger::sendable(bytes, final);
        if (send == 0)
        {
            return;
        }
        bytes.resize((size_t)send);
        uint64_t length = 0;
        const int status = server.post(chunk.offset, bytes, length);
        if (status == 202)
        {
            if (lose)
            {
                lose = false; // the server appended; the answer never came
                return;
            }
            ledger.posted(chunk.offset, send);
            continue;
        }
        if (status == 409)
        {
            if (length == ledger.postedBytes())
            {
                return;
            }
            ledger.resync(length);
            continue;
        }
        return;
    }
}

int main()
{
    // ---- the ledger's arithmetic ------------------------------------------------------------
    {
        LogUploadLedger ledger;
        say(ledger.postedBytes() == 0 && ledger.next(0).length == 0, "a new ledger has posted nothing and has nothing to read");
        LogUploadLedger::Chunk c = ledger.next(1000);
        say(c.offset == 0 && c.length == 1000, "a 1000-byte file is one chunk from nought");
        say(ledger.posted(0, 1000) && ledger.postedBytes() == 1000, "a success for that chunk moves the offset to 1000");
        say(!ledger.posted(0, 1000) && ledger.postedBytes() == 1000,
            "the SAME chunk acknowledged again (a retry whose first answer was lost) moves nothing");
        say(!ledger.posted(2000, 10) && ledger.postedBytes() == 1000,
            "a chunk past a gap moves nothing: the bytes between were never acknowledged");
        say(!ledger.posted(1000, 0), "an empty chunk moves nothing");
        say(ledger.next(1000).length == 0, "and the file at 1000 bytes is caught up");
        c = ledger.next(1000 + 3 * LogUploadLedger::kChunkCap);
        say(c.offset == 1000 && c.length == LogUploadLedger::kChunkCap, "a file far ahead is read a cap at a time");
        ledger.resync(400);
        say(ledger.postedBytes() == 400 && ledger.next(1000).offset == 400,
            "a 409 naming 400 moves the offset back to 400: the server lost its copy, the board re-posts from there");
        ledger.resync(5000);
        say(ledger.postedBytes() == 5000, "and one naming 5000 moves it forward: a restart on a file the server mostly holds");
    }

    // ---- chunks end on a line ---------------------------------------------------------------
    {
        say(LogUploadLedger::sendable("", false) == 0, "nothing to send from nothing");
        say(LogUploadLedger::sendable("line one\nline two\n", false) == 18, "two whole lines send whole");
        say(LogUploadLedger::sendable("line one\nline tw", false) == 9, "a torn second line waits: only the first is sent");
        say(LogUploadLedger::sendable("line tw", false) == 0, "a lone partial line sends nothing now");
        say(LogUploadLedger::sendable("line tw", true) == 7, "and everything at shutdown, when there is no next END");
        const std::string cap(LogUploadLedger::kChunkCap, 'x');
        say(LogUploadLedger::sendable(cap, false) == LogUploadLedger::kChunkCap,
            "a chunk that fills the cap with no newline sends whole rather than wedging");
    }

    // ---- the JSON one post carries ---------------------------------------------------------
    {
        json j = json::parse(TurnausClient::logChunkBody("darts-log.txt", 1234, "[12:00:00.000][INFO][SCORER] - SCORE: T20\n", false));
        say(j["file"] == "darts-log.txt" && j["offset"] == 1234 && j["final"] == false,
            "a chunk names its file, its offset and that it is not the last");
        say(j["text"] == "[12:00:00.000][INFO][SCORER] - SCORE: T20\n", "and carries the bytes as they are");
        json last = json::parse(TurnausClient::logChunkBody("darts-log.txt", 0, "tail", true));
        say(last["final"] == true, "the shutdown chunk says it is the last");
        // A byte that is not UTF-8 is replaced rather than refusing the whole chunk.
        std::string torn = "ok\n";
        torn += (char)0xC3;
        torn += "\n";
        const std::string body = TurnausClient::logChunkBody("f", 0, torn, false);
        say(!body.empty() && json::parse(body)["text"].get<std::string>().find("ok\n") == 0,
            "a torn multi-byte character is replaced and the chunk still posts");
    }

    // ---- THE SAME BYTES NEVER TWICE, through a model of the route --------------------------
    {
        LogUploadLedger ledger;
        Server server;
        bool lose = false;
        std::string file;

        file += "[a] visit 1 dart 1\n[a] visit 1 dart 2\n[a] visit 1 dart 3\n[a] END\n";
        endOfRound(ledger, server, file, lose);
        say(server.held == file && ledger.postedBytes() == file.size(), "END 1: the lines since the start are posted once");

        file += "[b] visit 2 dart 1\n[b] visit 2 dart 2\n[b] END\n";
        const size_t at_end_2 = file.size();
        file += "[c] visit 3 dart 1 (a line still being writ"; // torn: another thread is mid-line
        endOfRound(ledger, server, file, lose);
        say(server.held == file.substr(0, at_end_2) && ledger.postedBytes() == at_end_2,
            "END 2: the whole lines are posted, the torn tail waits for the next END");

        file += "ten)\n[c] visit 3 dart 2\n[c] END\n";
        lose = true; // the server appends; the board never hears
        endOfRound(ledger, server, file, lose);
        say(server.held == file && ledger.postedBytes() == at_end_2,
            "END 3, answer lost: the server has the chunk and the board still thinks it is owed");

        file += "[d] visit 4 dart 1\n[d] END\n";
        const int appended_before = server.appended;
        endOfRound(ledger, server, file, lose);
        say(server.refused == 1 && server.appended == appended_before + 1,
            "END 4: the retry from the OLD offset is refused (409) and the next chunk is appended -- "
            "the lost chunk's bytes were not stored twice (" + std::to_string(server.refused) + " refused, " +
                std::to_string(server.appended - appended_before) + " appended)");
        say(server.held == file && ledger.postedBytes() == file.size(),
            "and the server's copy is exactly the file, byte for byte");

        // A restart: a new process, a new ledger at nought, the same file and the same server.
        LogUploadLedger restarted;
        file += "[e] visit 5 dart 1\n[e] END\n";
        endOfRound(restarted, server, file, lose);
        say(server.refused == 2 && server.held == file && restarted.postedBytes() == file.size(),
            "a restarted board posts from nought, is told the server's length once, and continues from there "
            "with nothing stored twice");

        // Shutdown: the torn tail goes.
        file += "[f] the last line, no newline";
        endOfRound(restarted, server, file, lose, true);
        say(server.held == file, "at shutdown the tail is posted without waiting for its newline");
        say(server.held.size() == file.size(), "the server holds every byte once: " + std::to_string(server.held.size()) +
                                                   " of " + std::to_string(file.size()));
    }

    // ---- THE MUTATION PROOF ------------------------------------------------------------------
    //
    // PREDICTION, stated before the run: the count model -- an offset that moves on every
    // post whatever the answer, the shape the shipped spool cursor had before #1351 --
    // loses the whole log once the server's copy is gone (a server restored from a backup,
    // a file deleted on the desk): its offset runs ahead of a server that holds nothing,
    // every later chunk is refused, and the server ends with 0 of the file's 15 bytes. The
    // ledger as written moves on an acknowledgement for its own offset and on nothing else,
    // takes the 409's length as the new offset, and the server ends with 15 of 15.
    {
        struct CountModel
        {
            uint64_t posted = 0;
            void sent(uint64_t length) { posted += length; } // the mutation: moves on a 409 too
        };
        const std::string chunk1 = "1111\n", chunk2 = "2222\n", chunk3 = "3333\n";
        Server mutated_server, real_server;
        CountModel mutated;
        LogUploadLedger real;
        bool lose = false;
        uint64_t length = 0;
        std::string file = chunk1;

        mutated_server.post(0, chunk1, length);
        mutated.sent(chunk1.size());
        endOfRound(real, real_server, file, lose);
        say(mutated.posted == 5 && real.postedBytes() == 5 && mutated_server.held == chunk1 && real_server.held == chunk1,
            "the needle's premise: both models and both servers agree at 5 bytes");

        // The servers lose their copy.
        mutated_server.held.clear();
        real_server.held.clear();

        file += chunk2;
        const int m = mutated_server.post(mutated.posted, chunk2, length);
        mutated.sent(chunk2.size()); // 409, and the count model moves anyway
        endOfRound(real, real_server, file, lose); // 409 names 0: the ledger goes back and re-posts the file
        say(m == 409, "the needle: the chunk at the old offset is refused by a server that holds nothing");

        file += chunk3;
        const int m3 = mutated_server.post(mutated.posted, chunk3, length);
        mutated.sent(chunk3.size());
        endOfRound(real, real_server, file, lose);

        say(m3 == 409 && mutated_server.held.empty() && mutated.posted == file.size(),
            "the count model: its offset reached " + std::to_string(mutated.posted) + " while the server holds " +
                std::to_string(mutated_server.held.size()) + " of " + std::to_string(file.size()) +
                " bytes -- the log is lost, as predicted");
        say(real_server.held == file && real.postedBytes() == file.size(),
            "the ledger as written: the server holds " + std::to_string(real_server.held.size()) + " of " +
                std::to_string(file.size()) + " bytes, byte for byte");
    }

    std::cout << std::endl
              << "checks " << checks << "   failed " << failures.size() << std::endl;
    for (const std::string &f : failures)
    {
        std::cout << "  - " << f << std::endl;
    }
    return failures.empty() ? 0 : 1;
}

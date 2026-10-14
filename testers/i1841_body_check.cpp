// #1841: which 409 is "the Contest is over", pure.
//
// The two doors answer 409 for two different things. The autoscorer door's is "this board
// stands at no Station" ({"message": "This board stands at no Station."}, docs/api.md).
// The Contest door's, for a Contest that was given up or discarded, is
// CasualDetectionController::boardOf()'s ValidationException: Laravel's shape,
// {"message": "<localised>", "errors": {"board": ["<localised>"]}}, the sentence being
// casual_detection.error.discarded or .evening_over in the board's language. Until #1841
// the client read both with the autoscorer door's sentence, and on 2026-10-13 a Raspberry
// Pi 5 beat into a given-up Contest 22 for twenty-five minutes saying "no Station".
//
// TurnausClient::contestOverBody is the one seam that tells them apart, and this holds it
// to the shapes: the contract is `errors.board` PRESENT and never the sentence, so the
// Finnish and the English answers read the same, and every way a body can fail to be that
// shape -- no body, not JSON, a JSON that is not an object, `errors` missing, `errors` that
// is not an object, `errors` without `board` -- answers false, because a 409 promoted to
// "over" on a guess drops the binding and counts its owed darts abandoned. The log route's
// own 409 ({"data": {"length": N}}, the offset answer uploadLog reads for itself) is held
// here too, so a future refactor that routed it through deliver() could not end an evening.
//
// No client, no socket, no stub: the function is static. The mutation proof -- every 409
// read as "no Station" -- is i1841_check.sh --mutate's, over the call sites, and the
// end-to-end cases are i1841_contest_check.py's.
//
//   unit_check.sh 1841 runs this alone; i1841_contest_check.py's `pure` case runs it too.

#include "communication/turnaus_client.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void check(bool passed, const std::string &what)
{
    std::cout << (passed ? "ok   " : "FAIL ") << what << std::endl;
    if (!passed)
    {
        failures++;
    }
}

int main()
{
    // ---- the Contest door, over: both sentences, both languages, the shape the same ----
    // The Finnish sentence as Laravel's encoder ships it: every a-umlaut a ä escape, so
    // this file stays ASCII and the parser is the one that reads the language.
    check(TurnausClient::contestOverBody(
              "{\"message\":\"T\\u00e4m\\u00e4 vapaa peli on hyl\\u00e4tty, eik\\u00e4 t\\u00e4m\\u00e4 taulu ole en\\u00e4\\u00e4 sill\\u00e4.\","
              "\"errors\":{\"board\":[\"T\\u00e4m\\u00e4 vapaa peli on hyl\\u00e4tty, eik\\u00e4 t\\u00e4m\\u00e4 taulu ole en\\u00e4\\u00e4 sill\\u00e4.\"]}}"),
          "discarded, in Finnish: errors.board is there, so the Contest is over");
    check(TurnausClient::contestOverBody(
              "{\"message\":\"This game has been given up, so this board is no longer on it.\","
              "\"errors\":{\"board\":[\"This game has been given up, so this board is no longer on it.\"]}}"),
          "evening over, in English: the same shape, the same answer");
    check(TurnausClient::contestOverBody("{\"errors\":{\"board\":[]}}"),
          "errors.board present and empty still says over: presence is the contract");
    check(TurnausClient::contestOverBody("{\"errors\":{\"board\":\"one string\"},\"message\":\"x\"}"),
          "errors.board as a bare string is still errors.board");
    check(TurnausClient::contestOverBody("{\"errors\":{\"pin\":[\"x\"],\"board\":[\"y\"]}}"),
          "errors.board beside another key is still over");

    // ---- the autoscorer door's 409, and everything that is not the shape ----
    check(!TurnausClient::contestOverBody("{\"message\":\"This board stands at no Station.\"}"),
          "the autoscorer door's no-Station body has no errors: not over");
    check(!TurnausClient::contestOverBody("{\"message\":\"This board stands at no Station.\",\"errors\":{}}"),
          "errors without board: not over");
    check(!TurnausClient::contestOverBody("{\"message\":\"x\",\"errors\":{\"pin\":[\"casual_board.error.code_not_redeemable\"]}}"),
          "errors.pin (the pairing door's refusal) is not errors.board");
    check(!TurnausClient::contestOverBody("{\"errors\":[\"board\"]}"),
          "errors as an array is not the shape");
    check(!TurnausClient::contestOverBody("{\"errors\":\"board\"}"),
          "errors as a string is not the shape");
    check(!TurnausClient::contestOverBody("{\"data\":{\"length\":48213}}"),
          "the log route's own 409 (an offset answer) is not over");
    check(!TurnausClient::contestOverBody(""), "no body at all: not over");
    check(!TurnausClient::contestOverBody("Conflict"), "a body that is not JSON: not over");
    check(!TurnausClient::contestOverBody("{\"errors\":{\"board\":[\"x\"]"), "a truncated body: not over");
    check(!TurnausClient::contestOverBody("[{\"errors\":{\"board\":[\"x\"]}}]"),
          "a JSON array wrapping the shape is not the shape");
    check(!TurnausClient::contestOverBody("\"errors.board\""), "a JSON string: not over");
    check(!TurnausClient::contestOverBody("null"), "JSON null: not over");

    std::cout << (failures == 0 ? "I1841 body PASS" : "I1841 body FAIL") << " failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

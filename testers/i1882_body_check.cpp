// turnaus#1882: the heartbeat states the version this build prints, on every beat.
//
// The pure half of 1882-beat-version: the exact bytes `beat_body::body` builds, for a beat
// with a camera census and for one without, held against what
// `App\Autoscoring\StatedVersion` and `CameraReport` accept. Compiled with
// -DAPP_VERSION="..." like the detector is, so "the version is the compiled APP_VERSION"
// is asserted of the macro itself and not of a literal copied into this file.
//
//   g++ -std=c++17 -O1 -I src -DAPP_VERSION='"1882.0.0-check"' \
//       -o i1882_body_check testers/i1882_body_check.cpp
//
// Every assertion is labelled by what can turn it red; testers/i1882_check.sh mutates
// beat_body.hpp on a scratch copy and predicts which labels go red.

#include "communication/beat_body.hpp"
#include <iostream>
#include <string>

#ifndef APP_VERSION
#error "compile with -DAPP_VERSION=\"...\", the way the detector is built"
#endif

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
        ++failures;
}

static void same(const std::string &got, const std::string &want, const std::string &what)
{
    say(got == want, what + (got == want ? "" : "\n       got:  " + got + "\n       want: " + want));
}

/** Characters as PHP's mb_strlen counts them in valid UTF-8: every byte not 10xxxxxx. */
static size_t characters(const std::string &s)
{
    size_t n = 0;
    for (char c : s)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

int main()
{
    const std::string compiled = APP_VERSION;
    beat_body::Census census{3, 2, 1};

    // ---- body: the two shapes a beat has -------------------------------------------
    same(beat_body::body("READY", compiled, nullptr),
         "{\"condition\":\"READY\",\"version\":\"" + compiled + "\"}",
         "body: a beat with no census states condition and the compiled APP_VERSION, nothing else");
    same(beat_body::body("READY", compiled, &census),
         "{\"condition\":\"READY\",\"version\":\"" + compiled +
             "\",\"cameras\":{\"fitted\":3,\"scoring\":2,\"dark\":1}}",
         "body: a beat with a census states the version too, beside the three members");
    same(beat_body::body("ERROR", "", nullptr), "{\"condition\":\"ERROR\",\"version\":\"\"}",
         "body: an unset version is sent empty, which Turnaus reads as 'has not said'");

    // ---- cut: never longer than StatedVersion::LONGEST ------------------------------
    const std::string sixty_four(64, 'v');
    same(beat_body::statedVersion(sixty_four), sixty_four, "cut: 64 characters are kept whole");
    same(beat_body::statedVersion(sixty_four + "-and-then-some"), sixty_four,
         "cut: a longer build string is cut to its first 64 characters, not sent whole");
    // 63 ASCII then a two-byte character then more: the cut lands after the 64th
    // CHARACTER, so the two-byte one is kept whole and nothing of what follows is.
    const std::string wide = std::string(63, 'v') + "\xc3\xa4" + "-tail";
    const std::string cut_wide = beat_body::statedVersion(wide);
    same(cut_wide, std::string(63, 'v') + "\xc3\xa4", "cut: counted in characters, and never inside one");
    say(characters(beat_body::statedVersion(std::string(80, 'x') + "\xe2\x82\xac")) <= 64,
        "cut: at most 64 characters whatever the string");
    same(beat_body::statedVersion("v1\xff" "2"), "v1?2", "cut: a byte that begins no character is sent as '?'");
    same(beat_body::statedVersion("v1\xc3"), "v1?", "cut: a sequence the string ends inside is sent as '?'");

    // ---- escape: the body is JSON whatever the tag says -----------------------------
    same(beat_body::body("READY", "v\"1\\2\n", nullptr),
         "{\"condition\":\"READY\",\"version\":\"v\\\"1\\\\2\\u000a\"}",
         "escape: a quote, a backslash and a control character are JSON-escaped");
    // Escaping happens AFTER the cut: 64 quotes are 64 characters to Turnaus, and 128
    // bytes on the wire.
    const std::string quotes(70, '"');
    std::string want_quotes = "\"";
    for (int i = 0; i < 64; ++i)
        want_quotes += "\\\"";
    want_quotes += "\"";
    same(beat_body::jsonString(beat_body::statedVersion(quotes)), want_quotes,
         "escape: the cut counts what Turnaus decodes, not the escaped bytes");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

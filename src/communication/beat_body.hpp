#pragma once
// turnaus#1882: the heartbeat's body, and the version it states. Its own header, with
// nothing but <string> under it, so testers/i1882_body_check.cpp holds the exact bytes
// without the HTTP client's dependencies -- push_answer.hpp's reason (#1793).

#include <cstddef>
#include <cstdio>
#include <string>

namespace beat_body
{
    /**
     * The longest version Turnaus keeps: `App\Autoscoring\StatedVersion::LONGEST`, and
     * its rule is `max:64`, counted in CHARACTERS (`mb_strlen`). A longer string is a 422,
     * and a 422 on a beat costs the board its CONDITION as well as its version -- the
     * whole body is refused, not the one field. So a longer build string is cut here and
     * never sent whole.
     */
    constexpr std::size_t kLongestVersion = 64;

    /**
     * What a beat says the board is running: `--version`'s string, cut to at most
     * kLongestVersion characters.
     *
     * Cut on a character boundary, never inside a UTF-8 sequence: half a character is not
     * valid UTF-8, a body that is not valid UTF-8 is not JSON to the server, and a body
     * that is not JSON loses the condition -- the same cost as a long string, reached by
     * the cure for it. A byte that begins no valid sequence is written as '?' for the same
     * reason. Not trimmed: Turnaus trims, and the string sent is the one --version prints.
     */
    inline std::string statedVersion(const std::string &printed)
    {
        std::string out;
        std::size_t characters = 0;
        std::size_t i = 0;
        while (i < printed.size() && characters < kLongestVersion)
        {
            const unsigned char lead = static_cast<unsigned char>(printed[i]);
            // 0xC0, 0xC1 and anything above 0xF4 begin no valid sequence at all.
            const std::size_t width = lead < 0x80                 ? 1
                                      : lead < 0xC2 || lead > 0xF4 ? 0
                                      : lead < 0xE0                ? 2
                                      : lead < 0xF0                ? 3
                                                                   : 4;
            bool whole = width != 0 && i + width <= printed.size();
            for (std::size_t k = 1; whole && k < width; ++k)
            {
                whole = (static_cast<unsigned char>(printed[i + k]) & 0xC0) == 0x80;
            }
            if (whole)
            {
                out.append(printed, i, width);
                i += width;
            }
            else
            {
                out += '?';
                i += 1;
            }
            ++characters;
        }
        return out;
    }

    /** A JSON string literal, quotes included, for text that is already valid UTF-8. */
    inline std::string jsonString(const std::string &text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (u < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", u);
                out += escaped;
            }
            else
                out += c;
        }
        return out + "\"";
    }

    /** The three numbers of `App\Autoscoring\CameraReport`, and nothing else. */
    struct Census
    {
        int fitted = 0;
        int scoring = 0;
        int dark = 0;
    };

    /**
     * The whole body: `condition`, `version` on every beat, and `cameras` only when the
     * board has counted them (null here means it has not -- OMITTED, never three noughts;
     * #1474). `condition_word` is one of board_sight's words and needs no escaping.
     */
    inline std::string body(const char *condition_word, const std::string &version, const Census *cameras)
    {
        std::string out = std::string("{\"condition\":\"") + condition_word + "\"";
        out += ",\"version\":" + jsonString(statedVersion(version));
        if (cameras != nullptr)
        {
            out += ",\"cameras\":{\"fitted\":" + std::to_string(cameras->fitted) +
                   ",\"scoring\":" + std::to_string(cameras->scoring) +
                   ",\"dark\":" + std::to_string(cameras->dark) + "}";
        }
        return out + "}";
    }
}

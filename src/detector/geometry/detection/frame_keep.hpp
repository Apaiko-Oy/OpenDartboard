#pragma once
// #1787: the last N published darts' settled frames, kept so a corrected dart's pictures
// can be handed to Turnaus on request -- and nothing else ever leaves this buffer.
//
// WHAT IS KEPT. The window that called a dart averaged each camera's frames while the
// board settled (dart_processing.cpp, `window_frames`): one grey 8-bit picture per camera,
// the same pictures the vote and the entry solve were read from. At the vote that advances
// the board, dart_processing deposits them here with the window's ordinal; when the Turnaus
// client mints the dart's `reference` (TurnausClient::offer) it commits the deposit under
// that reference, and only if the ordinal is the one the DetectorResult names -- so a
// window whose dart never published is never attached to the next dart that does. The
// I1512 / I1681 / I1773 census lines the scoring prints about that window ride beside the
// pictures, taken off the log as they are written (logging::lineTap), matched by their
// `window=N`; they are present only under the census pin (OD_GEO_SCORE=on), as they are
// in the log, and absent otherwise.
//
// WHAT IT COSTS, AND WHEN. Unset, `OD_KEEP_FRAMES` costs nothing: deposit() reads one
// static bool and returns, no clone, no tap, no mutex. `OD_KEEP_FRAMES=on` keeps the last
// 10 darts; `OD_KEEP_FRAMES=<N>` keeps N. A dart is its cameras' settled frames at their
// own size: at 1280x720 and three cameras that is 3 x 921,600 = 2,764,800 bytes RAW, and
// the ring of ten is 27.6 MB -- measured by testers/i1787_frames_check.cpp, which prints
// the raw and the PNG figure on every run, and written up in docs/rig.md. The frames are
// kept raw and encoded to PNG only when they are asked for, on the client's push thread,
// so the scoring thread pays one clone per camera per published dart and never an encode.
//
// WHAT LEAVES. answer(reference) is the only reader. It hands back PNGs (one per camera,
// camera numbered from 1 as every log line numbers them), the window ordinal and the
// census lines, or says the dart is gone -- never kept, or already pushed out by the ten
// after it. Nothing here is written to disk, and a restart starts empty.

#include <opencv2/opencv.hpp>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "../../../utils/logging.hpp"

namespace frame_keep
{
    /** One kept dart. */
    struct Kept
    {
        std::string reference;
        long window = -1;
        std::vector<cv::Mat> frames;     // per camera, in camera order; an empty Mat where that camera brought none
        std::vector<std::string> census; // the I1512/I1681/I1773 lines printed about this window
    };

    /** What answer() hands over. */
    struct Answer
    {
        bool found = false;
        long window = -1;
        std::vector<std::vector<unsigned char>> pngs; // one per camera, empty where the camera brought no frame
        std::vector<std::string> census;
        size_t raw_bytes = 0; // what the kept frames occupy, for the figure in docs/rig.md
    };

    namespace detail
    {
        struct State
        {
            std::mutex mutex;
            bool enabled = false;
            size_t capacity = 10;
            // The deposit waiting for its reference: the window the board last advanced on.
            bool pending = false;
            Kept pending_dart;
            std::deque<Kept> ring; // oldest first
        };

        inline State &state()
        {
            static State *s = new State(); // never destroyed: the tap can run during exit
            return *s;
        }

        /** The `window=N` a census line names, or -1. */
        inline long windowOf(const std::string &line)
        {
            const size_t at = line.find("window=");
            if (at == std::string::npos)
            {
                return -1;
            }
            return std::atol(line.c_str() + at + 7);
        }

        inline bool isCensusLine(const std::string &line)
        {
            return line.rfind("I1512", 0) == 0 || line.rfind("I1681", 0) == 0 || line.rfind("I1773", 0) == 0;
        }

        /** logging::lineTap: a census line about the pending window is kept beside its frames. */
        inline void tap(const std::string &clean_line)
        {
            if (!isCensusLine(clean_line))
            {
                return;
            }
            State &s = state();
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.pending && windowOf(clean_line) == s.pending_dart.window)
            {
                s.pending_dart.census.push_back(clean_line);
            }
        }

        /** The ring of the last N, from OD_KEEP_FRAMES. */
        inline size_t capacityFromEnv(const char *value)
        {
            if (value == nullptr || *value == '\0')
            {
                return 0;
            }
            if (std::strcmp(value, "on") == 0 || std::strcmp(value, "1") == 0)
            {
                return 10;
            }
            const long n = std::atol(value);
            return n > 0 ? (size_t)n : 0;
        }
    } // namespace detail

    /**
     * Read OD_KEEP_FRAMES once. A tester can call configure() itself to run both answers
     * in one process; the pipeline reaches this through the first deposit().
     */
    inline void configure(size_t capacity)
    {
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.capacity = capacity;
        s.enabled = capacity > 0;
        s.pending = false;
        s.pending_dart = Kept();
        s.ring.clear();
        logging::lineTap = s.enabled ? &detail::tap : nullptr;
    }

    inline bool enabled()
    {
        static const bool configured = []
        {
            configure(detail::capacityFromEnv(std::getenv("OD_KEEP_FRAMES")));
            return true;
        }();
        (void)configured;
        return detail::state().enabled;
    }

    inline size_t capacity() { return detail::state().capacity; }

    /**
     * dart_processing: the board advanced on this window, and these are its cameras'
     * settled frames. One clone per camera, nothing else; a no-op unless enabled.
     */
    inline void deposit(long window, const std::vector<cv::Mat> &settled_frames)
    {
        if (!enabled())
        {
            return;
        }
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending_dart = Kept();
        s.pending_dart.window = window;
        s.pending_dart.frames.reserve(settled_frames.size());
        for (const cv::Mat &m : settled_frames)
        {
            s.pending_dart.frames.push_back(m.empty() ? cv::Mat() : m.clone());
        }
        s.pending = true;
    }

    /**
     * TurnausClient::offer: the dart published under `reference` came from `window`.
     * The deposit becomes a kept dart only when it is that window's; otherwise it is
     * dropped, so a window whose dart was refused never rides under the next reference.
     * True when something was kept.
     */
    inline bool commit(const std::string &reference, long window)
    {
        if (!enabled())
        {
            return false;
        }
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.pending)
        {
            return false;
        }
        const bool matches = window >= 0 && s.pending_dart.window == window;
        if (matches)
        {
            s.pending_dart.reference = reference;
            s.ring.push_back(std::move(s.pending_dart));
            while (s.ring.size() > s.capacity)
            {
                s.ring.pop_front();
            }
        }
        s.pending = false;
        s.pending_dart = Kept();
        return matches;
    }

    /** How many darts are kept right now. */
    inline size_t kept()
    {
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.ring.size();
    }

    /**
     * The one reader. PNG-encodes under the lock, which holds deposit() for the encode's
     * length -- tens of milliseconds, on the client's push thread, once per request that
     * Turnaus makes about a corrected dart; a scoring thread that arrives meanwhile waits
     * for one clone's worth of time, not a network's.
     */
    inline Answer answer(const std::string &reference)
    {
        Answer out;
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const Kept &k : s.ring)
        {
            if (k.reference != reference)
            {
                continue;
            }
            out.found = true;
            out.window = k.window;
            out.census = k.census;
            for (const cv::Mat &m : k.frames)
            {
                std::vector<unsigned char> png;
                if (!m.empty())
                {
                    out.raw_bytes += m.total() * m.elemSize();
                    cv::imencode(".png", m, png);
                }
                out.pngs.push_back(std::move(png));
            }
            return out;
        }
        return out;
    }

    /** Base64, for the JSON the answer travels in. Standard alphabet, padded. */
    inline std::string base64(const std::vector<unsigned char> &bytes)
    {
        static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        size_t i = 0;
        while (i + 2 < bytes.size())
        {
            const unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
            out.push_back(alphabet[(v >> 18) & 63]);
            out.push_back(alphabet[(v >> 12) & 63]);
            out.push_back(alphabet[(v >> 6) & 63]);
            out.push_back(alphabet[v & 63]);
            i += 3;
        }
        if (i + 1 == bytes.size())
        {
            const unsigned v = bytes[i] << 16;
            out.push_back(alphabet[(v >> 18) & 63]);
            out.push_back(alphabet[(v >> 12) & 63]);
            out += "==";
        }
        else if (i + 2 == bytes.size())
        {
            const unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8);
            out.push_back(alphabet[(v >> 18) & 63]);
            out.push_back(alphabet[(v >> 12) & 63]);
            out.push_back(alphabet[(v >> 6) & 63]);
            out.push_back('=');
        }
        return out;
    }
} // namespace frame_keep

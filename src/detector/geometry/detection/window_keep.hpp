#pragma once
// turnaus#1991: every published dart's WINDOW, kept on disk so the dart can be read again
// offline -- by a later build, against the thrower's correction.
//
// WHY frame_keep.hpp IS NOT THIS. #1787 keeps one averaged settled picture per camera for
// the last ten darts, in RAM, and hands them over only when Turnaus asks about a dart
// somebody corrected (41 of 2026-10-15's 3,090). A settled picture alone cannot be read
// again: what dart_processing.cpp does with it is DIFFERENCE it, twice per camera --
//
//   settled  -  the CLEAN reference   (#1518: the last scene the vote reconciled CLEAN, or
//                                      the calibration background until there was one)
//                                     -> is the board occupied at all, and by how much
//   settled  -  the WORKING background (the settled picture of the visit's previous called
//                                      dart; none on a clean board)
//                                     -> the fresh figure: the tip, the axis fit's pixels,
//                                        and the reference the shadow test reads (#1554)
//
// -- and then decides on four numbers that are not in any picture: the state each camera
// held before the window, its cumulative board change one window ago (#1518's reversion,
// #1648), the tips it already reported this visit (#1535), and its fitted board. The solve
// and the vote then read the calibration. So a replayable window is, per camera: the
// settled picture, the clean reference and the working background AS THEY WERE WHEN THE
// WINDOW CLOSED, those scalars, and the calibration the start measured. That is what this
// writes, and nothing less re-runs the axis fit.
//
// THE SWITCH. `OD_KEEP_WINDOWS=<directory>` turns it on; unset (or empty) it costs one
// static bool per window and writes nothing. `OD_KEEP_WINDOWS_MB=<n>` bounds the directory
// (default 4096); when a new dart would pass the bound the OLDEST darts go first.
//
// WHAT IS ON DISK.
//
//   <dir>/pics/<16 hex>.png          one grey 8-bit picture, named by its content
//   <dir>/darts/<reference>.json     one published dart: per camera the three pictures'
//                                    names and the scalars; the board state before; the
//                                    OD_* switches the process ran under; the calibration
//   <dir>/calibration/<16 hex>.dat   cache/geometry_calibration.dat as this start wrote it
//
// Pictures are named by content because a visit repeats them: dart 2's working background
// IS dart 1's settled picture, and the clean reference moves only when the board
// reconciles CLEAN. A three-dart visit is 12 distinct pictures, not 27 (measured by
// testers/i1991_keep_check.cpp on every run; the bytes are in docs/rig.md).
//
// THE BOUND, AND THE ONE RULE IT MUST KEEP. A picture is shared, so "oldest first" cannot
// mean the oldest file: a clean reference written at 19:00 is still the reference of a
// dart thrown at 19:05. Every file carries the ordinal of the LAST dart that used it; the
// evictor removes the lowest ordinal first, and at one ordinal a dart's account before the
// pictures. So a dart's account on disk always has every picture it names -- the check
// asserts exactly that after every eviction, and its two mutations break it.
//
// WHO PAYS. The scoring thread pays one clone per picture per PUBLISHED dart (nine at
// three cameras) in deposit(); the hash, the PNG encodes and the writes run on a writer
// thread of this header's own. If the writer falls more than `queue_limit` darts behind,
// the newest dart is DROPPED and said so at WARNING -- a board never waits on its disk.
// A dart committed in the last moment before exit may not reach the disk.
//
// A reference is minted by TurnausClient::offer, as for frame_keep, so a board with no
// Turnaus client keeps nothing: there is no name to keep a dart under, and no truth to
// grade it against.

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "../../../utils/logging.hpp"

#if !defined(_WIN32)
extern char **environ;
#endif

namespace window_keep
{
    /** One camera's share of a window: what the per-camera stage read, before the vote moved any of it. */
    struct Camera
    {
        cv::Mat settled; // the window's averaged frame
        cv::Mat clean;   // the reference the cumulative diff was cut against
        cv::Mat fresh;   // the working background the fresh diff was cut against; empty on a clean board
        int previous_state = 0;        // DartBoardState before the window, as its integer
        int previous_board_change = 0; // the cumulative figure one window ago
        int frames = 0;                // frames this camera brought to the average
        bool board_known = false;
        cv::RotatedRect board;
        std::vector<cv::Point2f> reported_tips; // tips already reported this visit
    };

    struct Window
    {
        long window = -1;
        int board_state_before = 0;
        std::vector<Camera> cameras;
    };

    // ---- the ledger: which file goes first (pure, no disk) -----------------------------
    struct Entry
    {
        std::string name;    // relative to the directory: darts/<ref>.json, pics/<hash>.png
        uint64_t bytes = 0;
        uint64_t ordinal = 0; // the last dart that used it
        bool account = false; // a dart's json (goes before pictures of the same ordinal)
    };

    class Ledger
    {
    public:
        void put(const std::string &name, uint64_t bytes, uint64_t ordinal, bool account)
        {
            auto it = entries_.find(name);
            if (it != entries_.end())
            {
                total_ -= it->second.bytes;
            }
            Entry e;
            e.name = name;
            e.bytes = bytes;
            e.ordinal = ordinal;
            e.account = account;
            entries_[name] = e;
            total_ += bytes;
        }
        bool has(const std::string &name) const { return entries_.count(name) != 0; }
        /** A picture used again belongs to the dart that used it last. */
        void touch(const std::string &name, uint64_t ordinal)
        {
            auto it = entries_.find(name);
            if (it != entries_.end() && it->second.ordinal < ordinal)
            {
                it->second.ordinal = ordinal;
            }
        }
        uint64_t total() const { return total_; }
        size_t size() const { return entries_.size(); }

        /**
         * The names to remove so the total fits `bound`, oldest first; nothing of ordinal
         * `keep` or later is ever named, so the dart being written is never its own victim.
         */
        std::vector<std::string> evict(uint64_t bound, uint64_t keep)
        {
            std::vector<const Entry *> order;
            for (const auto &kv : entries_)
            {
                order.push_back(&kv.second);
            }
            std::sort(order.begin(), order.end(), [](const Entry *a, const Entry *b)
                      {
                          if (a->ordinal != b->ordinal)
                          {
                              return a->ordinal < b->ordinal;
                          }
                          if (a->account != b->account)
                          {
                              return a->account; // the account before its pictures
                          }
                          return a->name < b->name; });
            std::vector<std::string> out;
            uint64_t total = total_;
            for (const Entry *e : order)
            {
                if (total <= bound || e->ordinal >= keep)
                {
                    break;
                }
                total -= e->bytes;
                out.push_back(e->name);
            }
            for (const std::string &name : out)
            {
                total_ -= entries_[name].bytes;
                entries_.erase(name);
            }
            return out;
        }

    private:
        std::map<std::string, Entry> entries_;
        uint64_t total_ = 0;
    };

    // ---- small pure helpers --------------------------------------------------------------
    inline uint64_t fnv1a(const unsigned char *data, size_t n, uint64_t h = 1469598103934665603ULL)
    {
        for (size_t i = 0; i < n; i++)
        {
            h ^= data[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    inline std::string hex16(uint64_t v)
    {
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
        return buf;
    }

    /** A picture's name: its size and every byte of it. Empty for an empty picture. */
    inline std::string pictureName(const cv::Mat &m)
    {
        if (m.empty())
        {
            return "";
        }
        const cv::Mat c = m.isContinuous() ? m : m.clone();
        uint64_t h = fnv1a((const unsigned char *)&c.rows, sizeof(c.rows));
        h = fnv1a((const unsigned char *)&c.cols, sizeof(c.cols), h);
        h = fnv1a(c.data, c.total() * c.elemSize(), h);
        return hex16(h);
    }

    inline std::string jsonEscape(const std::string &s)
    {
        std::string out;
        for (char ch : s)
        {
            if (ch == '"' || ch == '\\')
            {
                out.push_back('\\');
                out.push_back(ch);
            }
            else if ((unsigned char)ch < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            }
            else
            {
                out.push_back(ch);
            }
        }
        return out;
    }

    /** A switch whose VALUE is a credential is named and not quoted. */
    inline bool namesASecret(const std::string &name)
    {
        for (const char *word : {"TOKEN", "SECRET", "KEY", "PASS", "CRED"})
        {
            if (name.find(word) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    /** The OD_* environment this process runs under, sorted: what a replay must set to read the dart the same way. */
    inline std::map<std::string, std::string> switches()
    {
        std::map<std::string, std::string> out;
#if !defined(_WIN32)
        for (char **e = environ; e != nullptr && *e != nullptr; e++)
        {
            const char *eq = std::strchr(*e, '=');
            if (eq == nullptr || std::strncmp(*e, "OD_", 3) != 0)
            {
                continue;
            }
            const std::string name(*e, eq - *e);
            out[name] = namesASecret(name) ? "(withheld)" : std::string(eq + 1);
        }
#endif
        return out;
    }

    /** The account of one dart, as the json on disk. Picture names are passed in, per camera, settled/clean/fresh. */
    inline std::string account(const std::string &reference, const Window &w,
                               const std::vector<std::vector<std::string>> &names,
                               const std::string &calibration, const std::map<std::string, std::string> &env)
    {
        std::ostringstream o;
        o.precision(9);
        o << "{\"v\":1,\"reference\":\"" << jsonEscape(reference) << "\",\"window\":" << w.window
          << ",\"board_state_before\":" << w.board_state_before << ",\"calibration\":";
        if (calibration.empty())
        {
            o << "null";
        }
        else
        {
            o << "\"" << calibration << "\"";
        }
        o << ",\"cameras\":[";
        for (size_t i = 0; i < w.cameras.size(); i++)
        {
            const Camera &c = w.cameras[i];
            const auto name = [&](size_t k)
            {
                return names[i][k].empty() ? std::string("null") : "\"" + names[i][k] + "\"";
            };
            o << (i ? "," : "") << "{\"camera\":" << (i + 1) << ",\"settled\":" << name(0) << ",\"clean\":" << name(1)
              << ",\"fresh\":" << name(2) << ",\"previous_state\":" << c.previous_state
              << ",\"previous_board_change\":" << c.previous_board_change << ",\"frames\":" << c.frames
              << ",\"board\":";
            if (c.board_known)
            {
                o << "{\"cx\":" << c.board.center.x << ",\"cy\":" << c.board.center.y << ",\"w\":" << c.board.size.width
                  << ",\"h\":" << c.board.size.height << ",\"angle\":" << c.board.angle << "}";
            }
            else
            {
                o << "null";
            }
            o << ",\"reported_tips\":[";
            for (size_t t = 0; t < c.reported_tips.size(); t++)
            {
                o << (t ? "," : "") << "[" << c.reported_tips[t].x << "," << c.reported_tips[t].y << "]";
            }
            o << "]}";
        }
        o << "],\"switches\":{";
        bool first = true;
        for (const auto &kv : env)
        {
            o << (first ? "" : ",") << "\"" << jsonEscape(kv.first) << "\":\"" << jsonEscape(kv.second) << "\"";
            first = false;
        }
        o << "}}\n";
        return o.str();
    }

    /** Every picture name an account on disk mentions: the 16-hex strings after settled/clean/fresh. */
    inline std::vector<std::string> picturesNamedBy(const std::string &json)
    {
        std::vector<std::string> out;
        for (const char *key : {"\"settled\":\"", "\"clean\":\"", "\"fresh\":\""})
        {
            size_t at = 0;
            const size_t len = std::strlen(key);
            while ((at = json.find(key, at)) != std::string::npos)
            {
                at += len;
                if (at + 16 <= json.size())
                {
                    out.push_back(json.substr(at, 16));
                }
            }
        }
        return out;
    }

    namespace detail
    {
        namespace fs = std::filesystem;

        struct Job
        {
            std::string reference;
            Window window;
        };

        struct State
        {
            std::mutex mutex; // the pending slot and the queue
            std::condition_variable wake;
            std::condition_variable idle;
            bool enabled = false;
            std::string dir;
            uint64_t bound = 4096ULL * 1024 * 1024;
            size_t queue_limit = 8;
            bool pending = false;
            Window pending_window;
            std::deque<Job> queue;
            bool writing = false;
            bool thread_started = false;
            // The writer's own (touched only on the writer thread, or under configure()).
            Ledger ledger;
            uint64_t ordinal = 0;
            std::string calibration;
            bool calibration_looked = false;
            uint64_t written = 0;
            uint64_t dropped = 0;
            uint64_t evicted = 0;
        };

        inline State &state()
        {
            static State *s = new State(); // never destroyed: the writer can run during exit
            return *s;
        }

        inline bool writeFile(const fs::path &path, const void *data, size_t n)
        {
            const fs::path tmp = path.string() + ".part"; // a reader never meets half a file
            {
                std::ofstream f(tmp, std::ios::binary);
                if (!f)
                {
                    return false;
                }
                f.write((const char *)data, (std::streamsize)n);
                if (!f.good())
                {
                    return false;
                }
            }
            std::error_code ec;
            fs::rename(tmp, path, ec);
            return !ec;
        }

        /** What is already in the directory, so a restart keeps the bound and the order. */
        inline void adopt(State &s)
        {
            s.ledger = Ledger();
            s.ordinal = 0;
            std::error_code ec;
            fs::create_directories(fs::path(s.dir) / "pics", ec);
            fs::create_directories(fs::path(s.dir) / "darts", ec);
            fs::create_directories(fs::path(s.dir) / "calibration", ec);
            struct Seen
            {
                std::string name;
                fs::file_time_type at;
                uint64_t bytes;
            };
            std::vector<Seen> accounts;
            for (const auto &e : fs::directory_iterator(fs::path(s.dir) / "darts", ec))
            {
                if (e.path().extension() == ".json")
                {
                    accounts.push_back({e.path().filename().string(), e.last_write_time(ec), (uint64_t)e.file_size(ec)});
                }
            }
            std::sort(accounts.begin(), accounts.end(), [](const Seen &a, const Seen &b)
                      { return a.at != b.at ? a.at < b.at : a.name < b.name; });
            std::map<std::string, uint64_t> last_use;
            for (const Seen &a : accounts)
            {
                s.ordinal++;
                s.ledger.put("darts/" + a.name, a.bytes, s.ordinal, true);
                std::ifstream f(fs::path(s.dir) / "darts" / a.name, std::ios::binary);
                std::stringstream text;
                text << f.rdbuf();
                for (const std::string &pic : picturesNamedBy(text.str()))
                {
                    last_use[pic] = s.ordinal;
                }
            }
            for (const auto &e : fs::directory_iterator(fs::path(s.dir) / "pics", ec))
            {
                if (e.path().extension() != ".png")
                {
                    continue;
                }
                const std::string stem = e.path().stem().string();
                // A picture no account names is a write the last run did not finish: ordinal 0, first to go.
                s.ledger.put("pics/" + stem + ".png", (uint64_t)e.file_size(ec), last_use.count(stem) ? last_use[stem] : 0, false);
            }
        }

        /** This start's calibration, copied beside the darts once it exists. */
        inline void lookForCalibration(State &s)
        {
            if (s.calibration_looked)
            {
                return;
            }
            std::ifstream f("cache/geometry_calibration.dat", std::ios::binary);
            if (!f)
            {
                return; // not written yet (or a tester's bare directory): the account says null
            }
            std::stringstream bytes;
            bytes << f.rdbuf();
            const std::string data = bytes.str();
            s.calibration_looked = true;
            if (data.empty())
            {
                return;
            }
            const std::string name = hex16(fnv1a((const unsigned char *)data.data(), data.size()));
            const fs::path to = fs::path(s.dir) / "calibration" / (name + ".dat");
            std::error_code ec;
            if (fs::exists(to, ec) || writeFile(to, data.data(), data.size()))
            {
                s.calibration = name; // a few hundred bytes a start; never evicted
            }
        }

        inline void writeOne(State &s, Job &job)
        {
            const uint64_t ordinal = ++s.ordinal;
            lookForCalibration(s);
            std::vector<std::vector<std::string>> names;
            bool ok = true;
            for (const Camera &c : job.window.cameras)
            {
                std::vector<std::string> mine;
                for (const cv::Mat *m : {&c.settled, &c.clean, &c.fresh})
                {
                    const std::string name = pictureName(*m);
                    mine.push_back(name);
                    if (name.empty())
                    {
                        continue;
                    }
                    const std::string rel = "pics/" + name + ".png";
                    if (s.ledger.has(rel))
                    {
                        s.ledger.touch(rel, ordinal);
                        continue;
                    }
                    std::vector<unsigned char> png;
                    cv::imencode(".png", *m, png);
                    if (writeFile(fs::path(s.dir) / rel, png.data(), png.size()))
                    {
                        s.ledger.put(rel, png.size(), ordinal, false);
                    }
                    else
                    {
                        ok = false;
                    }
                }
                names.push_back(mine);
            }
            if (ok)
            {
                const std::string text = account(job.reference, job.window, names, s.calibration, switches());
                const std::string rel = "darts/" + job.reference + ".json";
                ok = writeFile(fs::path(s.dir) / rel, text.data(), text.size());
                if (ok)
                {
                    s.ledger.put(rel, text.size(), ordinal, true);
                    s.written++;
                }
            }
            if (!ok)
            {
                log_warning("I1991 KEEP: the window of " + job.reference + " could not be written under " + s.dir +
                            " -- the dart is published and is not in the corpus (turnaus#1991)");
            }
            for (const std::string &name : s.ledger.evict(s.bound, ordinal))
            {
                std::error_code ec;
                fs::remove(fs::path(s.dir) / name, ec);
                s.evicted++;
            }
        }

        inline void writer()
        {
            State &s = state();
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(s.mutex);
                    s.writing = false;
                    s.idle.notify_all();
                    s.wake.wait(lock, [&]
                                { return !s.queue.empty(); });
                    job = std::move(s.queue.front());
                    s.queue.pop_front();
                    s.writing = true;
                }
                try
                {
                    writeOne(s, job);
                }
                catch (const std::exception &e)
                {
                    log_warning(std::string("I1991 KEEP: a window was not written: ") + e.what());
                }
            }
        }
    } // namespace detail

    /**
     * Turn the keeping on for `dir` (empty turns it off) with a bound in bytes. The pipeline
     * reaches this once through enabled(); a tester calls it to run several answers in one process.
     */
    inline void configure(const std::string &dir, uint64_t bound_bytes)
    {
        detail::State &s = detail::state();
        std::unique_lock<std::mutex> lock(s.mutex);
        s.idle.wait(lock, [&]
                    { return s.queue.empty() && !s.writing; });
        s.enabled = !dir.empty();
        s.dir = dir;
        s.bound = bound_bytes;
        s.pending = false;
        s.pending_window = Window();
        s.calibration.clear();
        s.calibration_looked = false;
        s.written = s.dropped = s.evicted = 0;
        if (s.enabled)
        {
            detail::adopt(s);
            if (!s.thread_started)
            {
                s.thread_started = true;
                std::thread(detail::writer).detach();
            }
        }
    }

    inline bool enabled()
    {
        static const bool configured = []
        {
            const char *dir = std::getenv("OD_KEEP_WINDOWS");
            const char *mb = std::getenv("OD_KEEP_WINDOWS_MB");
            const long megabytes = mb != nullptr && std::atol(mb) > 0 ? std::atol(mb) : 4096;
            const std::string where = dir != nullptr ? dir : "";
            if (!where.empty())
            {
                configure(where, (uint64_t)megabytes * 1024 * 1024);
                log_info("I1991 KEEP: every published dart's window is kept under " + where + ", bounded at " +
                         std::to_string(megabytes) + " MB, oldest first (OD_KEEP_WINDOWS, turnaus#1991)");
            }
            return true;
        }();
        (void)configured;
        return detail::state().enabled;
    }

    /** dart_processing: the board advanced on this window. One clone per picture; a no-op unless enabled. */
    inline void deposit(const Window &window)
    {
        if (!enabled())
        {
            return;
        }
        Window copy = window;
        for (Camera &c : copy.cameras)
        {
            c.settled = c.settled.empty() ? cv::Mat() : c.settled.clone();
            c.clean = c.clean.empty() ? cv::Mat() : c.clean.clone();
            c.fresh = c.fresh.empty() ? cv::Mat() : c.fresh.clone();
        }
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending_window = std::move(copy);
        s.pending = true;
    }

    /**
     * TurnausClient::offer: the dart published under `reference` came from `window`. As
     * frame_keep::commit -- the deposit is kept only when it is that window's. True when the
     * window was handed to the writer.
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
        const bool matches = window >= 0 && s.pending_window.window == window;
        bool handed = false;
        if (matches)
        {
            if (s.queue.size() >= s.queue_limit)
            {
                s.dropped++;
                log_warning("I1991 KEEP: the writer is " + std::to_string(s.queue.size()) + " darts behind, so the window of " +
                            reference + " is dropped rather than waited for (turnaus#1991)");
            }
            else
            {
                detail::Job job;
                job.reference = reference;
                job.window = std::move(s.pending_window);
                s.queue.push_back(std::move(job));
                s.wake.notify_one();
                handed = true;
            }
        }
        s.pending = false;
        s.pending_window = Window();
        return handed;
    }

    /** Wait until the writer has nothing left (a tester, and shutdown). */
    inline void flush()
    {
        detail::State &s = detail::state();
        std::unique_lock<std::mutex> lock(s.mutex);
        s.idle.wait(lock, [&]
                    { return s.queue.empty() && !s.writing; });
    }

    struct Figures
    {
        uint64_t written = 0, dropped = 0, evicted = 0, bytes = 0, files = 0;
    };

    /** What the writer has done since configure(); call after flush(). */
    inline Figures figures()
    {
        detail::State &s = detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        Figures f;
        f.written = s.written;
        f.dropped = s.dropped;
        f.evicted = s.evicted;
        f.bytes = s.ledger.total();
        f.files = s.ledger.size();
        return f;
    }
} // namespace window_keep

#pragma once
// Capture-skew instrument (issue #813). Not upstream. Records one line per capture
// cycle when OD_SKEW_LOG names a file. Off by default: with the variable unset every
// hook is a load of a null pointer. It was written against upstream's captureFrames;
// behind #802's seam the Frame already carries the three instants it used to take
// itself, so the recorder is a writer over a vector of frames and nothing else moved.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>

namespace camera
{
    namespace skew
    {
        // What CAP_PROP_POS_MSEC is on, per the backend that was opened.
        // "stream" is a position in a file and is not a clock at all.
        inline const char *&clockKind()
        {
            static const char *kind = "unknown";
            return kind;
        }

        inline long long hostMicros()
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        struct Sink
        {
            FILE *fp = nullptr;
            std::vector<char> buf;
            long long cycles = 0;
            long long budget = 0;
            long long first_us = -1;
            long long last_us = -1;
            bool stopped = false;
            bool active = false;

            Sink()
            {
                const char *budget_env = getenv("OD_SKEW_CYCLES");
                budget = budget_env ? atoll(budget_env) : 0;
                if (budget > 0)
                    active = true;

                const char *path = getenv("OD_SKEW_LOG");
                if (!path || !*path)
                    return;
                fp = fopen(path, "w");
                if (!fp)
                    return;
                active = true;
                buf.resize(1 << 20);
                setvbuf(fp, buf.data(), _IOFBF, buf.size());
                fprintf(fp, "# opendartboard capture-skew instrument v1\n");
                fprintf(fp, "cycle,cameras,valid,clock,pos_ms,anchor_us,ret_us\n");
            }

            ~Sink()
            {
                if (fp)
                {
                    fflush(fp);
                    fclose(fp);
                    fp = nullptr;
                }
                if (active && cycles > 1 && first_us >= 0)
                {
                    // The loop's own duration, so the cost of a cycle can be
                    // stated without the startup and calibration in it.
                    fprintf(stderr, "SKEWLOOP cycles=%lld loop_us=%lld per_cycle_us=%lld\n",
                            cycles, last_us - first_us, (last_us - first_us) / (cycles - 1));
                    fflush(stderr);
                }
            }
        };

        inline Sink &sink()
        {
            static Sink s;
            return s;
        }

        // True while the calibration path is averaging frames; those cycles
        // are not capture cycles of the scoring loop and are not recorded.
        inline bool &suppressed()
        {
            static bool flag = false;
            return flag;
        }

        inline void record(size_t cameras, size_t valid,
                           const std::vector<double> &pos_ms,
                           const std::vector<long long> &anchor_us,
                           const std::vector<long long> &ret_us)
        {
            Sink &s = sink();
            if (!s.active || suppressed())
                return;
            s.cycles++;
            long long newest = -1;
            for (size_t i = 0; i < cameras; i++)
                if (ret_us[i] > newest)
                    newest = ret_us[i];
            if (newest >= 0)
            {
                if (s.first_us < 0)
                    s.first_us = newest;
                s.last_us = newest;
            }
            if (s.fp)
            {
            fprintf(s.fp, "%lld,%zu,%zu,%s,", s.cycles, cameras, valid, clockKind());
            for (size_t i = 0; i < cameras; i++)
                fprintf(s.fp, "%s%.3f", i ? "|" : "", pos_ms[i]);
            fputc(',', s.fp);
            for (size_t i = 0; i < cameras; i++)
                fprintf(s.fp, "%s%lld", i ? "|" : "", anchor_us[i]);
            fputc(',', s.fp);
            for (size_t i = 0; i < cameras; i++)
                fprintf(s.fp, "%s%lld", i ? "|" : "", ret_us[i]);
            fputc('\n', s.fp);
            if ((s.cycles % 200) == 0)
                fflush(s.fp);
            }
            if (s.budget > 0 && s.cycles >= s.budget && !s.stopped)
            {
                s.stopped = true;
                if (s.fp)
                    fflush(s.fp);
                raise(SIGINT); // through the program's own handler, so it stops cleanly
            }
        }
    }
}

#pragma once
// #1721: WHERE ELSE THIS DART MAY BE, best first, for the marking page's fix row.
//
// The maintainer asked on 2026-10-01 for a ranked list of the next most likely sectors
// with EVERY published dart, so the row a scorer taps to correct a dart always has
// something on it -- not only on the close calls #1556 flags. Everything here is built
// from what scoring already knows about the dart; nothing is measured anew and nothing
// it returns can change what publishes (`score` is decided before this is asked, and
// this function is handed it, never the other way round).
//
// THE RANKING, in this order, each source deduplicated against everything before it and
// against the published score:
//
//   1. THE FLAGGED ALTERNATIVE (#1556 / #1707). The solver measured a one-sigma crossing
//      of a wire and named the other side by re-scoring there (`alternativeAcross`), or
//      the rim fallback named the ring across the nearer wire. A measured "it may be
//      this instead" outranks everything else here.
//   2. THE VOTE'S RUNNERS-UP. What every OTHER voting camera read, most cameras first,
//      ties by the lowest camera index; a reading whose wedge was ASSERTED (#1346's
//      default-to-20) goes after every reading that was not, because the constant is
//      not an observation of the wedge. On a geometric publish these are still the
//      cameras' own readings, so a vote that disagreed with the solve is offered here.
//   3. THE GEOMETRIC NEIGHBOURS: the ring on each side of the published ring and the
//      wedge on each side of the published wedge, nearest wire first, by the board
//      millimetres from the dart to that wire -- radially for a ring wire, along the
//      arc (radius x sin) for a wedge wire. A neighbour whose distance cannot be
//      measured (no radius, or no angle) follows every one that can, in a fixed order:
//      rings before wedges, inner ring before outer, anticlockwise wedge before
//      clockwise -- except a single with no radius, whose treble comes first, then its
//      two wedges, then the double and the 25.
//
// WHAT EACH KIND OF DART GETS from source 3:
//   - a geometric publish: all four, ordered by the solved radius and angle;
//   - a string-vote publish: the same four, ordered by the published camera's own
//     rulers where they answered (a ring is decided by ellipse containment and the
//     radius by a separate ruler, #1628's caveat, so a radius that disagrees with the
//     ring is clamped to "on the wire" rather than trusted to say which side);
//   - an ASSERTED 20 (#1487): the RING neighbours only. The ring was measured by the
//     ellipses; the wedge was not, so the 1 and the 5 beside it are no more likely than
//     any other wedge and offering them would dress a constant up as a measurement;
//   - a BULL: the 25. A 25: the BULL, and the single the dart's angle points into when
//     a measured angle exists;
//   - a double: the single inside it and None (the MISS) across the outer wire;
//   - a MISS that measured a place just past the double, with a read wedge: that
//     double. A MISS with no place gets runners-up only, which is usually nothing.
//
// The list is longer than the door takes. It is returned whole, in the socket's own
// vocabulary (S20, BULL, OUTER, MISS), and `TurnausClient::detectionBody` is the seam
// that translates each entry through `postableSector`, drops what the grammar cannot
// spell or what is the dart's own place, and keeps the first three (#1720's door
// refuses a fourth with a 422, which would cost the whole dart).
//
// Pure, header-only and std-only, so the seam tester holds it without the detector.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace dart_candidates
{
    /** One camera's vote, as the ranking needs it. */
    struct Reading
    {
        std::string score; // the camera's score, socket vocabulary
        bool asserted = false; // its wedge was #1346's default-to-20
    };

    /** Everything the ranking reads about the published dart. */
    struct Evidence
    {
        std::string published; // the published score, socket vocabulary
        std::string ring;      // single, double, triple, bull, outer; empty for a miss
        int segment = -1;      // 1..20; -1 where none
        bool wedge_read = false; // the wedge is a measurement (not asserted, not absent)
        bool radius_known = false;
        float radius = -1.0f;  // 0 at the bull, 1 at the outer edge of the double
        bool angle_known = false;
        float angle = -1.0f;   // degrees clockwise from the middle of the 20
        std::string alternative; // #1556/#1707's flagged other candidate; empty if none
        std::vector<Reading> others; // every OTHER voting camera's reading, camera order
    };

    // DartboardSpec's radii, millimetres (perspective_processing.hpp), restated so this
    // header needs nothing but std.
    inline constexpr float kBullMm = 6.35f;
    inline constexpr float kOuterBullMm = 15.9f;
    inline constexpr float kInnerTrebleMm = 99.0f;
    inline constexpr float kOuterTrebleMm = 107.0f;
    inline constexpr float kInnerDoubleMm = 162.0f;
    inline constexpr float kOuterDoubleMm = 170.0f;

    inline const int *sequence()
    {
        static const int s[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};
        return s;
    }

    inline int wedgeIndex(int segment)
    {
        for (int i = 0; i < 20; i++)
        {
            if (sequence()[i] == segment)
            {
                return i;
            }
        }
        return -1;
    }

    /** The wedge an angle points into; -1 for an angle that is not one. */
    inline int segmentAt(float angle)
    {
        if (!std::isfinite(angle) || angle < 0.0f)
        {
            return -1;
        }
        int i = (int)std::floor(std::fmod(angle + 9.0f, 360.0f) / 18.0f);
        return sequence()[((i % 20) + 20) % 20];
    }

    inline std::string scoreOf(char ring, int segment)
    {
        return std::string(1, ring) + std::to_string(segment);
    }

    /** A neighbour across one wire, with the millimetres to that wire (-1: unknown). */
    struct Neighbour
    {
        std::string score;
        float mm = -1.0f;
        int order = 0; // the fixed tie order for an unmeasured distance
    };

    inline std::vector<Neighbour> geometricNeighbours(const Evidence &e)
    {
        std::vector<Neighbour> out;
        const float r = e.radius_known && e.radius >= 0.0f ? e.radius * kOuterDoubleMm : -1.0f;
        auto across = [&](float boundary_mm, bool inward) -> float
        {
            if (r < 0.0f)
            {
                return -1.0f;
            }
            // Clamped at the wire: a ruler that puts the dart on the far side of the
            // published ring's own wire says the wire is close, not which side it is.
            return std::max(0.0f, inward ? r - boundary_mm : boundary_mm - r);
        };

        const bool wedge_known = e.wedge_read && e.segment >= 1 && e.segment <= 20;

        if (e.ring == "bull")
        {
            out.push_back({"OUTER", across(kBullMm, false), 1});
        }
        else if (e.ring == "outer")
        {
            out.push_back({"BULL", across(kBullMm, true), 0});
            const int s = e.wedge_read && e.angle_known ? segmentAt(e.angle) : -1;
            if (s > 0)
            {
                out.push_back({scoreOf('S', s), across(kOuterBullMm, false), 1});
            }
        }
        else if (e.ring == "single" || e.ring == "triple" || e.ring == "double")
        {
            const int seg = e.segment;
            if (seg >= 1 && seg <= 20)
            {
                if (e.ring == "triple")
                {
                    out.push_back({scoreOf('S', seg), across(kInnerTrebleMm, true), 0});
                    out.push_back({scoreOf('S', seg), across(kOuterTrebleMm, false), 1});
                }
                else if (e.ring == "double")
                {
                    out.push_back({scoreOf('S', seg), across(kInnerDoubleMm, true), 0});
                    out.push_back({"MISS", across(kOuterDoubleMm, false), 1});
                }
                else if (r >= 0.0f && r < (kInnerTrebleMm + kOuterTrebleMm) / 2.0f)
                {
                    // The inner single: the 25 inside it, the treble outside it.
                    out.push_back({"OUTER", across(kOuterBullMm, true), 0});
                    out.push_back({scoreOf('T', seg), across(kInnerTrebleMm, false), 1});
                }
                else if (r >= 0.0f)
                {
                    // The outer single: the treble inside it, the double outside it.
                    out.push_back({scoreOf('T', seg), across(kOuterTrebleMm, true), 0});
                    out.push_back({scoreOf('D', seg), across(kInnerDoubleMm, false), 1});
                }
                else
                {
                    // A single with no radius: the treble borders both single bands, so it
                    // comes first; then the two wedges (orders 10 and 11 below), because a
                    // misread wedge is the commoner failure of a lone reading (#1628); the
                    // double and the 25 last.
                    out.push_back({scoreOf('T', seg), -1.0f, 0});
                    out.push_back({scoreOf('D', seg), -1.0f, 12});
                    out.push_back({"OUTER", -1.0f, 13});
                }
            }

            if (wedge_known)
            {
                const int i = wedgeIndex(e.segment);
                const char ring = e.ring == "triple" ? 'T' : (e.ring == "double" ? 'D' : 'S');
                const int ccw = sequence()[(i + 19) % 20];
                const int cw = sequence()[(i + 1) % 20];
                float ccw_mm = -1.0f, cw_mm = -1.0f;
                if (e.angle_known && e.angle >= 0.0f && r >= 0.0f)
                {
                    // Offset from the middle of the published wedge, in [-9, 9]; outside
                    // it (a ruler that disagrees with the segment) is clamped to the wire.
                    float o = std::fmod(e.angle - i * 18.0f + 540.0f, 360.0f) - 180.0f;
                    o = std::max(-9.0f, std::min(9.0f, o));
                    const float rad = (float)(3.14159265358979323846 / 180.0);
                    ccw_mm = r * std::sin((9.0f + o) * rad);
                    cw_mm = r * std::sin((9.0f - o) * rad);
                }
                out.push_back({scoreOf(ring, ccw), ccw_mm, 10});
                out.push_back({scoreOf(ring, cw), cw_mm, 11});
            }
        }
        else if (e.published == "MISS" && e.wedge_read && e.angle_known && r > kOuterDoubleMm)
        {
            // A MISS that measured a place past the double, with a read wedge.
            const int s = segmentAt(e.angle);
            if (s > 0)
            {
                out.push_back({scoreOf('D', s), r - kOuterDoubleMm, 0});
            }
        }

        std::stable_sort(out.begin(), out.end(), [](const Neighbour &a, const Neighbour &b)
                         {
                             const bool ak = a.mm >= 0.0f, bk = b.mm >= 0.0f;
                             if (ak != bk)
                             {
                                 return ak; // measured before unmeasured
                             }
                             if (ak && a.mm != b.mm)
                             {
                                 return a.mm < b.mm;
                             }
                             return a.order < b.order;
                         });
        return out;
    }

    /** The vote's runners-up, most cameras first, asserted readings last. */
    inline std::vector<std::string> runnersUp(const Evidence &e)
    {
        struct Group
        {
            std::string score;
            int cameras = 0;
            int first = 0;
            bool asserted = true;
        };
        std::vector<Group> groups;
        for (int i = 0; i < (int)e.others.size(); i++)
        {
            const Reading &rd = e.others[i];
            if (rd.score.empty() || rd.score == e.published)
            {
                continue;
            }
            auto it = std::find_if(groups.begin(), groups.end(),
                                   [&](const Group &g) { return g.score == rd.score; });
            if (it == groups.end())
            {
                groups.push_back({rd.score, 0, i, true});
                it = groups.end() - 1;
            }
            it->cameras++;
            it->asserted = it->asserted && rd.asserted;
        }
        std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b)
                         {
                             if (a.asserted != b.asserted)
                             {
                                 return !a.asserted;
                             }
                             if (a.cameras != b.cameras)
                             {
                                 return a.cameras > b.cameras;
                             }
                             return a.first < b.first;
                         });
        std::vector<std::string> out;
        for (const Group &g : groups)
        {
            out.push_back(g.score);
        }
        return out;
    }

    /**
     * The whole ranked list, socket vocabulary, never the published score, no repeats.
     * Uncapped: the door's limit and grammar are the seam's (`detectionBody`).
     */
    inline std::vector<std::string> rank(const Evidence &e)
    {
        std::vector<std::string> out;
        auto add = [&](const std::string &s)
        {
            if (!s.empty() && s != "END" && s != e.published &&
                std::find(out.begin(), out.end(), s) == out.end())
            {
                out.push_back(s);
            }
        };
        add(e.alternative);
        for (const std::string &s : runnersUp(e))
        {
            add(s);
        }
        for (const Neighbour &n : geometricNeighbours(e))
        {
            add(n.score);
        }
        return out;
    }
} // namespace dart_candidates

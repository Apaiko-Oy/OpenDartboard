#include "ring_identity.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace cv;
using namespace std;

namespace ring_identity
{
    namespace
    {
        /** A number a reader can compare, rather than to_string's six decimals. */
        string decimals(double value, int places)
        {
            ostringstream out;
            out << fixed << setprecision(places) << value;
            return out.str();
        }

        /**
         * How far out a ray looks, in spans. It is the top of the treble band --
         * 1.589 * 1.2605 = 2.003 -- and a hair over it, so that a reading ABOVE the band
         * can be seen to be above it rather than being clipped into it by the search's
         * own ceiling. A reading that wants more than this is an Unknown either way.
         */
        const double kSearchCeiling = 2.1;

        /**
         * 720 rays: half a degree apart. The statistic is a percentile over rays, so the
         * count decides its resolution and nothing else -- at 720 the 90th percentile is
         * the 72nd ray from the outside, which no single arc of a twenty-segment ring can
         * carry on its own.
         */
        const int kRays = 720;

        /**
         * The percentile taken. Not the maximum, which one stray blob owns, and not the
         * median, which a ring broken into arcs drags inward: measured over both fixtures
         * the median reach sits at 0.70 to 0.84 of a span that IS the doubles ring, purely
         * because a ray between two arcs finds its last colour further in.
         *
         * At the 90th the twelve readings are 0.921 to 0.970 and 1.559 to 1.760 and the
         * two clusters sit on 1.0 and 1.589 with a factor of 1.61 between them.
         */
        const double kReachPercentile = 0.90;

        /**
         * A ring is a thing that goes all the way round. Below half the rays carrying any
         * colour at all, a percentile over rays is a statement about an arc, and this
         * module says Unknown rather than a number about a quarter of a circle. Both
         * fixtures answer on 706 to 720 rays of 720 -- 98% and up -- so nothing here is
         * near it.
         */
        const double kRaysNeeded = 0.5;
    }

    bool identityNotAsked()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_RING");
            return e && string(e) == "span";
        }();
        return v;
    }

    Sighting identify(const Mat &colourMask, const Point2f &spanCentre, double span)
    {
        Sighting sighting;
        if (identityNotAsked() || colourMask.empty() || span <= 0.0)
        {
            return sighting;
        }

        Mat gray;
        if (colourMask.channels() == 1)
        {
            gray = colourMask;
        }
        else
        {
            cvtColor(colourMask, gray, COLOR_BGR2GRAY);
        }

        vector<double> reaches;
        reaches.reserve(kRays);
        sighting.rays_asked = kRays;

        for (int k = 0; k < kRays; k++)
        {
            const double theta = 2.0 * kPi * k / kRays;
            const double dx = cos(theta), dy = sin(theta);
            for (int r = (int)lround(span * kSearchCeiling); r >= 1; r--)
            {
                const int x = cvRound(spanCentre.x + dx * r);
                const int y = cvRound(spanCentre.y + dy * r);
                if (x < 0 || y < 0 || x >= gray.cols || y >= gray.rows)
                {
                    continue;
                }
                if (gray.at<uchar>(y, x))
                {
                    reaches.push_back(r / span);
                    break;
                }
            }
        }

        sighting.rays_answered = (int)reaches.size();
        if (reaches.size() < (size_t)(kRaysNeeded * kRays))
        {
            return sighting;
        }

        sort(reaches.begin(), reaches.end());
        const size_t at = (size_t)lround(kReachPercentile * (reaches.size() - 1));
        sighting.reach = reaches[at];

        const Spec spec;
        const double doubles = 1.0;
        const double trebles = spec.boardRadiusOfTrebleSpan();
        const double band = spec.band();

        if (sighting.reach >= doubles / band && sighting.reach < doubles * band)
        {
            sighting.ring = Ring::Doubles;
        }
        else if (sighting.reach >= trebles / band && sighting.reach < trebles * band)
        {
            sighting.ring = Ring::Trebles;
        }
        return sighting;
    }

    string sentence(const Sighting &s)
    {
        const Spec spec;
        const double trebles = spec.boardRadiusOfTrebleSpan();
        const double band = spec.band();

        if (identityNotAsked())
        {
            return "OD_RING=span, so no ring identity is stated and the span is taken for the board "
                   "the way it was before #1423";
        }
        if (s.rays_answered < (int)(kRaysNeeded * s.rays_asked))
        {
            return "the ring cannot be named: only " + to_string(s.rays_answered) + " of " +
                   to_string(s.rays_asked) + " rays from the middle of the span found any colour at all, "
                   "and a ring identity needs at least " + to_string((int)(kRaysNeeded * s.rays_asked)) +
                   " -- below that a percentile over rays is a statement about an arc";
        }

        const string reading = "colour reaches " + decimals(s.reach, 3) + " spans (the 90th of " +
                               to_string(s.rays_answered) + " rays that found any)";

        switch (s.ring)
        {
        case Ring::Doubles:
            return "the span is the DOUBLES ring: " + reading + ", and nothing coloured lies outside it, "
                   "so the span is the board -- board radius = span x 1.000";
        case Ring::Trebles:
            return "the span is the TREBLE ring: " + reading + ", so a second coloured ring lies outside "
                   "the one that was measured -- board radius = span x " + decimals(trebles, 3);
        default:
            break;
        }
        return "the ring cannot be named: " + reading + ", which is in neither band -- the doubles band is " +
               decimals(1.0 / band, 3) + " to " + decimals(band, 3) + " spans and the treble band is " +
               decimals(trebles / band, 3) + " to " + decimals(trebles * band, 3) +
               ". Falling back to the treble reading, board radius = span x " + decimals(trebles, 3) +
               ", which is what every stage below already assumed; on a span that really is the "
               "doubles ring that over-measures the board by 59%";
    }
}

// turnaus#1992: OD_RING_CENSUS's reading, held pure -- ring_census.hpp against a board drawn
// through a known map, with nothing but OpenCV.
//
// The board is drawn where the MAP IS WRONG in two known ways: the treble band 2 mm nearer
// the bull than its 99..107 mm all the way round, and the doubles band where it should be
// on average but pushed 3 mm out toward one side and 3 mm in at the other. Blurred, so an
// edge is a slope and not a step. Each label is one assertion, and i1992_ring_check.sh's
// mutations predict them by name:
//
//   parse:    unset, empty, `off`, `1` and `ON` are off; `on` is on
//   default:  with OD_RING_CENSUS unset the process reads off
//   treble:   both treble edges read -2.0 mm all round and under 0.15 mm turning
//   doubles:  both doubles edges read 0.0 mm all round and 3.0 mm turning
//   width:    the treble band reads 8 mm wide (outer minus inner, all round)
//   dim:      the same board at a third of the brightness reads the same treble
//   story:    the line names the treble's inner edge as inward and the doubles ring as
//             +0.0 mm all round, 3.0 mm turning
//   blank:    a black frame reads no ring, and the line says none could be read
//
//   compiled by i1992_ring_check.sh against the tree and against mutated scratch copies.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "detector/geometry/calibration/ring_census.hpp"

using namespace ring_census;

static int failures = 0;

static void say(bool ok, const std::string &label, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << label << " " << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string num(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%+.2f", v);
    return buf;
}

// board mm -> image px: a tilted view with real perspective, the bull at (660, 300)
static const cv::Matx33d kH(1.38, -0.21, 660.0, 0.12, 0.92, 300.0, 2.1e-4, -5.2e-4, 1.0);

static cv::Mat drawBoard(double brightness)
{
    const cv::Matx33d Hinv = kH.inv();
    cv::Mat img(720, 1280, CV_8UC3);
    for (int y = 0; y < img.rows; y++)
    {
        for (int x = 0; x < img.cols; x++)
        {
            const cv::Vec3d q = Hinv * cv::Vec3d(x, y, 1.0);
            const double X = q[0] / q[2], Y = q[1] / q[2];
            const double mm = std::sqrt(X * X + Y * Y), th = std::atan2(Y, X);
            const bool even = ((int)std::floor((th + CV_PI) / (CV_PI / 10.0)) % 2) == 0;
            const double push = 3.0 * std::cos(th - 1.0);
            const bool treble = mm >= 97.0 && mm < 105.0;
            const bool doubles = mm >= 162.0 + push && mm < 170.0 + push;
            cv::Vec3d c = (mm < 170.0 + push && !even) ? cv::Vec3d(200, 220, 230) : cv::Vec3d(25, 25, 25);
            if (treble || doubles)
            {
                c = even ? cv::Vec3d(30, 30, 200) : cv::Vec3d(30, 160, 30);
            }
            img.at<cv::Vec3b>(y, x) = cv::Vec3b((uchar)(c[0] * brightness), (uchar)(c[1] * brightness),
                                                (uchar)(c[2] * brightness));
        }
    }
    cv::GaussianBlur(img, img, cv::Size(0, 0), 1.5);
    return img;
}

int main()
{
    say(!onFrom(nullptr) && !onFrom("") && !onFrom("off") && !onFrom("1") && !onFrom("ON") && onFrom("on"),
        "parse:", "unset, empty, off, 1 and ON are off; on is on");
    say(!isOn(), "default:", "OD_RING_CENSUS unset reads off");

    const double ringMm[4] = {99.0, 107.0, 162.0, 170.0};
    const cv::Point2f bull(660.f, 300.f);
    const Census c = read(drawBoard(1.0), kH.inv(), 1.0, bull, ringMm);
    const Reading &ti = c.ring[0], &to = c.ring[1], &di = c.ring[2], &d_o = c.ring[3];

    say(ti.read && to.read && std::fabs(ti.allRoundMm + 2.0) < 0.15 && std::fabs(to.allRoundMm + 2.0) < 0.15 &&
            ti.turningMm < 0.15 && to.turningMm < 0.15 && ti.rays >= 600 && to.rays >= 600,
        "treble:", "inner " + num(ti.allRoundMm) + " outer " + num(to.allRoundMm) + " mm all round, turning " +
                       num(ti.turningMm) + " / " + num(to.turningMm) + ", " + std::to_string(ti.rays) + " rays");
    say(di.read && d_o.read && std::fabs(di.allRoundMm) < 0.15 && std::fabs(d_o.allRoundMm) < 0.15 &&
            std::fabs(di.turningMm - 3.0) < 0.2 && std::fabs(d_o.turningMm - 3.0) < 0.2,
        "doubles:", "inner " + num(di.allRoundMm) + " outer " + num(d_o.allRoundMm) + " mm all round, turning " +
                        num(di.turningMm) + " / " + num(d_o.turningMm));
    say(ti.read && to.read && std::fabs((to.allRoundMm - ti.allRoundMm)) < 0.2,
        "width:", "the treble band reads " + num(8.0 + to.allRoundMm - ti.allRoundMm) + " mm wide");

    const Census dim = read(drawBoard(0.33), kH.inv(), 1.0, bull, ringMm);
    say(dim.ring[0].read && dim.ring[1].read && std::fabs(dim.ring[0].allRoundMm + 2.0) < 0.2 &&
            std::fabs(dim.ring[1].allRoundMm + 2.0) < 0.2,
        "dim:", "at a third of the brightness: inner " + num(dim.ring[0].allRoundMm) + " outer " +
                    num(dim.ring[1].allRoundMm) + " mm all round");

    const std::string line = story(c);
    say(line.find("the treble ring's inner edge -") != std::string::npos &&
            line.find("the doubles ring +0.0 mm all round, 3.0 mm turning with the angle") != std::string::npos,
        "story:", line);

    const Census none = read(cv::Mat(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0)), kH.inv(), 1.0, bull, ringMm);
    say(!none.ring[0].read && !none.ring[1].read && !none.ring[2].read && !none.ring[3].read &&
            story(none).find("none could be read") != std::string::npos,
        "blank:", story(none));

    std::cout << "I1992 ring check: " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}

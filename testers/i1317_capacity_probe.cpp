// #1317: why an AddressSanitizer build did not find this, which #845 was asked and this
// issue asks again.
//
// The old fill was `for (int i = 0; i < 20; i++) result.wireEndpoints[i] = colorWires[i];`
// over a vector holding however many wires were detected. That is a read past the end of
// the vector for every index from its size to nineteen -- undefined behaviour whatever the
// count -- but AddressSanitizer does not see an object's end, it sees an ALLOCATION's end,
// and std::vector's allocation is its capacity. `finalWires` is built by push_back from
// empty, so its capacity is the next power of two at or above the count: 32 for anything
// from 17 to 32, and 16 for anything from 9 to 16.
//
// So the nine-wire board the maintainer measured reads four Point2f outside the allocation
// and ASan fires; the nineteen-wire board this repository can reproduce reads one Point2f
// past the end of the object and INSIDE the allocation, and ASan says nothing. That is the
// whole reason a sanitizer build could complete calibration on a partial detection and
// report nothing, and it is measured here rather than argued.
//
//   i1317_capacity_probe <detected-wires>     # exits 0; ASan reports if it can see it
#include <opencv2/opencv.hpp>
#include <iostream>
#include <array>
#include <vector>

int main(int argc, char **argv)
{
    const int detected = argc > 1 ? atoi(argv[1]) : 9;

    // Built exactly as findWiresByEnsemble builds it: push_back from empty, then sorted.
    std::vector<cv::Point2f> found;
    for (int i = 0; i < detected; i++)
    {
        found.push_back(cv::Point2f((float)i, (float)i));
    }
    std::sort(found.begin(), found.end(),
              [](const cv::Point2f &a, const cv::Point2f &b) { return a.x < b.x; });

    std::cout << "detected=" << detected << " size=" << found.size()
              << " capacity=" << found.capacity()
              << " reads_outside_allocation=" << (20 > (int)found.capacity() ? 20 - (int)found.capacity() : 0)
              << std::endl;
    std::cout.flush();

    // The copy as it was written before #1317.
    std::array<cv::Point2f, 20> endpoints;
    for (int i = 0; i < 20; i++)
    {
        endpoints[i] = found[i];
    }

    // Consume it so nothing is optimised away.
    double sum = 0.0;
    for (const cv::Point2f &p : endpoints)
    {
        sum += p.x;
    }
    std::cout << "copied 20 endpoints, sum=" << sum << std::endl;
    return 0;
}

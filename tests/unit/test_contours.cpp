#include <amrvis/render2d/Contours.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool nearlyEqual(double a, double b, double tolerance = 1.0e-5)
{
    return std::fabs(a - b) <= tolerance;
}

// 4x4 plane with the analytic field value(x, y) = x + 2y.
amrvis::ScalarPlane makePlane()
{
    amrvis::ScalarPlane plane;
    plane.width = 4;
    plane.height = 4;
    plane.values.resize(16);
    plane.valid.assign(16, 1);
    plane.sourceLevel.assign(16, 0);
    for (int y = 0; y < plane.height; ++y) {
        for (int x = 0; x < plane.width; ++x) {
            plane.values[static_cast<std::size_t>(x + y * plane.width)]
                = static_cast<float>(x + 2 * y);
        }
    }
    return plane;
}

bool onLine(const amrvis::ContourSegment& segment, double contour)
{
    return nearlyEqual(segment.x0 + 2.0 * segment.y0, contour)
        && nearlyEqual(segment.x1 + 2.0 * segment.y1, contour);
}

} // namespace

int main()
{
    const auto values = amrvis::contourValues(0.0, 1.0, 10);
    require(values.size() == 10, "contourValues returned the wrong count");
    require(nearlyEqual(values.front(), 0.05), "first contour value mismatch");
    require(nearlyEqual(values.back(), 0.95), "last contour value mismatch");
    require(nearlyEqual(values[5] - values[4], 0.1), "contour spacing mismatch");

    bool threw = false;
    try {
        (void)amrvis::contourValues(0.0, 1.0, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "contourValues accepted a zero count");
    threw = false;
    try {
        (void)amrvis::contourValues(1.0, 1.0, 4);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "contourValues accepted an empty range");

    // Cells whose value range brackets 2.5: (0,0), (1,0), (2,0), (0,1).
    const auto plane = makePlane();
    const auto segments = amrvis::generateContours(plane, {2.5});
    require(segments.size() == 4, "analytic plane produced the wrong segment count");
    for (const auto& segment : segments) {
        require(onLine(segment, 2.5), "segment endpoint does not lie on the contour line");
        require(nearlyEqual(segment.value, 2.5), "segment value field mismatch");
    }

    // Invalidating corner (1, 1) must suppress the four cells touching it,
    // leaving only cell (2, 0).
    auto masked = makePlane();
    masked.valid[static_cast<std::size_t>(1 + 1 * masked.width)] = 0;
    const auto maskedSegments = amrvis::generateContours(masked, {2.5});
    require(maskedSegments.size() == 1, "invalid cell was not skipped");
    const auto& survivor = maskedSegments.front();
    require(onLine(survivor, 2.5), "surviving segment does not lie on the contour line");
    require(survivor.x0 >= 2.0F && survivor.x1 >= 2.0F
            && survivor.y0 <= 1.0F && survivor.y1 <= 1.0F,
        "surviving segment lies outside cell (2, 0)");

    const auto outside = amrvis::generateContours(plane, {100.0});
    require(outside.empty(), "out-of-range contour produced segments");

    // Cells (1, 2) and (2, 1) bracket 7.5; both touch corner (2, 2).
    const auto nonFinite = amrvis::generateContours(plane, {7.5});
    require(nonFinite.size() == 2, "two-cell contour produced the wrong count");
    auto withNaN = makePlane();
    withNaN.values[static_cast<std::size_t>(2 + 2 * withNaN.width)] = std::nanf("");
    const auto nanSegments = amrvis::generateContours(withNaN, {7.5});
    require(nanSegments.empty(), "non-finite corner was not skipped");

    return 0;
}

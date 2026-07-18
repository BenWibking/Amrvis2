#pragma once

#include <amrvis/core/Result.hpp>

#include <vector>

namespace amrvis {

struct ContourSegment {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    double value = 0.0;
};

// Legacy Amrvis contour placement: count lines at
// value_i = minimum + (0.5 + i) / count * (maximum - minimum).
[[nodiscard]] std::vector<double> contourValues(double minimum, double maximum, int count);

// Marching squares over the plane's corner samples. A cell is the quad formed
// by samples (i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1), so segment
// coordinates are plane pixel coordinates (x = column, y = row) spanning
// 0 .. width - 1 and 0 .. height - 1, with row 0 at the bottom of the plane.
// Cells with an invalid (valid == 0) or non-finite corner are skipped.
// Saddle cells (two diagonally opposite corners above the contour value) are
// resolved by connecting the left-right and top-bottom edge crossings,
// mirroring legacy AmrPicture::DrawContour.
[[nodiscard]] std::vector<ContourSegment> generateContours(
    const ScalarPlane& plane, const std::vector<double>& values);

} // namespace amrvis

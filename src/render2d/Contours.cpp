#include <amrvis/render2d/Contours.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace amrvis {

std::vector<double> contourValues(double minimum, double maximum, int count)
{
    if (count < 1) {
        throw std::invalid_argument("contour count must be positive");
    }
    if (!(minimum < maximum)) {
        throw std::invalid_argument("contour range must have positive extent");
    }
    std::vector<double> values(static_cast<std::size_t>(count));
    const double span = maximum - minimum;
    for (int i = 0; i < count; ++i) {
        values[static_cast<std::size_t>(i)] =
            minimum + (0.5 + static_cast<double>(i)) / count * span;
    }
    return values;
}

namespace {

bool between(double a, double value, double b) noexcept
{
    return (a <= value && value <= b) || (a >= value && value >= b);
}

struct CellCrossing {
    bool left = false;
    bool right = false;
    bool bottom = false;
    bool top = false;
    float xLeft = 0.0F;
    float yLeft = 0.0F;
    float xRight = 0.0F;
    float yRight = 0.0F;
    float xBottom = 0.0F;
    float yBottom = 0.0F;
    float xTop = 0.0F;
    float yTop = 0.0F;
};

void contourCell(
    const ScalarPlane& plane, int i, int j, double value,
    std::vector<ContourSegment>& segments)
{
    const auto rowStride = static_cast<std::size_t>(plane.width);
    const auto bottomLeft = static_cast<std::size_t>(i) + static_cast<std::size_t>(j) * rowStride;
    const auto bottomRight = bottomLeft + 1;
    const auto topLeft = bottomLeft + rowStride;
    const auto topRight = topLeft + 1;

    if (plane.valid[bottomLeft] == 0 || plane.valid[bottomRight] == 0
        || plane.valid[topLeft] == 0 || plane.valid[topRight] == 0) {
        return;
    }
    const double bl = plane.values[bottomLeft];
    const double br = plane.values[bottomRight];
    const double tl = plane.values[topLeft];
    const double tr = plane.values[topRight];
    if (!std::isfinite(bl) || !std::isfinite(br) || !std::isfinite(tl) || !std::isfinite(tr)) {
        return;
    }

    const auto x0 = static_cast<float>(i);
    const auto x1 = static_cast<float>(i + 1);
    const auto y0 = static_cast<float>(j);
    const auto y1 = static_cast<float>(j + 1);

    CellCrossing crossing;
    crossing.left = between(bl, value, tl);
    crossing.right = between(br, value, tr);
    crossing.bottom = between(bl, value, br);
    crossing.top = between(tl, value, tr);

    // Linear interpolation along each crossed edge. The degenerate fallbacks
    // (equal corner values) mirror legacy AmrPicture::DrawContour.
    if (crossing.left) {
        const double t = (tl != bl) ? (value - bl) / (tl - bl) : 0.0;
        crossing.xLeft = x0;
        crossing.yLeft = static_cast<float>(static_cast<double>(y0) + t);
    }
    if (crossing.right) {
        const double t = (tr != br) ? (value - br) / (tr - br) : 0.0;
        crossing.xRight = x1;
        crossing.yRight = static_cast<float>(static_cast<double>(y0) + t);
    }
    if (crossing.bottom) {
        const double t = (br != bl) ? (value - bl) / (br - bl) : 1.0;
        crossing.xBottom = static_cast<float>(static_cast<double>(x0) + t);
        crossing.yBottom = y0;
    }
    if (crossing.top) {
        const double t = (tr != tl) ? (value - tl) / (tr - tl) : 1.0;
        crossing.xTop = static_cast<float>(static_cast<double>(x0) + t);
        crossing.yTop = y1;
    }

    const auto emit = [&](float ax, float ay, float bx, float by) {
        segments.push_back(ContourSegment{ax, ay, bx, by, value});
    };

    if (crossing.left && crossing.right && crossing.bottom && crossing.top) {
        // Saddle point: connect left-right and top-bottom, mirroring legacy.
        emit(crossing.xLeft, crossing.yLeft, crossing.xRight, crossing.yRight);
        emit(crossing.xTop, crossing.yTop, crossing.xBottom, crossing.yBottom);
    } else if (crossing.top && crossing.bottom) {
        emit(crossing.xTop, crossing.yTop, crossing.xBottom, crossing.yBottom);
    } else if (crossing.left) {
        if (crossing.right) {
            emit(crossing.xLeft, crossing.yLeft, crossing.xRight, crossing.yRight);
        } else if (crossing.top) {
            emit(crossing.xLeft, crossing.yLeft, crossing.xTop, crossing.yTop);
        } else if (crossing.bottom) {
            emit(crossing.xLeft, crossing.yLeft, crossing.xBottom, crossing.yBottom);
        }
    } else if (crossing.right) {
        if (crossing.top) {
            emit(crossing.xRight, crossing.yRight, crossing.xTop, crossing.yTop);
        } else if (crossing.bottom) {
            emit(crossing.xRight, crossing.yRight, crossing.xBottom, crossing.yBottom);
        }
    }
}

} // namespace

std::vector<ContourSegment> generateContours(
    const ScalarPlane& plane, const std::vector<double>& values)
{
    if (plane.width < 0 || plane.height < 0) {
        throw std::invalid_argument("scalar plane dimensions must not be negative");
    }
    const auto pixelCount = static_cast<std::size_t>(plane.width)
        * static_cast<std::size_t>(plane.height);
    if (plane.values.size() != pixelCount || plane.valid.size() != pixelCount) {
        throw std::invalid_argument("scalar plane storage does not match its dimensions");
    }

    std::vector<ContourSegment> segments;
    if (plane.width < 2 || plane.height < 2) {
        return segments;
    }
    for (const double value : values) {
        if (!std::isfinite(value)) {
            continue;
        }
        for (int j = 0; j + 1 < plane.height; ++j) {
            for (int i = 0; i + 1 < plane.width; ++i) {
                contourCell(plane, i, j, value, segments);
            }
        }
    }
    return segments;
}

} // namespace amrvis

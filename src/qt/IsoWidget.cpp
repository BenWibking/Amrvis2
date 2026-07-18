#include "IsoWidget.hpp"

#include <amrvis/render2d/Palette.hpp>

#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace amrvis::qt {
namespace {

// Fixed view direction: rotate about z, then about x, orthographic.
constexpr double pi = 3.14159265358979323846;
constexpr double azimuth = 30.0 * pi / 180.0;
constexpr double elevation = 30.0 * pi / 180.0;

// Cube corner indexing: bit 0 = x side, bit 1 = y side, bit 2 = z side.
constexpr std::array<std::array<int, 2>, 12> boxEdges{{
    {{0, 1}}, {{2, 3}}, {{4, 5}}, {{6, 7}},
    {{0, 2}}, {{1, 3}}, {{4, 6}}, {{5, 7}},
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
}};

} // namespace

IsoWidget::IsoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 150);
}

void IsoWidget::setGeometry(const DatasetMetadata& metadata)
{
    m_hasGeometry = metadata.dimension == 3;
    m_domain = metadata.physicalDomain;
    m_levels.clear();
    if (m_hasGeometry) {
        m_levels.reserve(metadata.levels.size());
        for (const auto& level : metadata.levels) {
            m_levels.push_back({level.level, level.domain, level.cellSize,
                level.boxes});
        }
    }
    update();
}

void IsoWidget::setSlicePositions(double x, double y, double z)
{
    m_slicePositions = {x, y, z};
    update();
}

void IsoWidget::setColorPalette(const Palette* palette)
{
    m_palette = palette;
    update();
}

void IsoWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);
    if (!m_hasGeometry) {
        painter.setPen(QColor(0x88, 0x88, 0x88));
        painter.drawText(rect(), Qt::AlignCenter, tr("3-D overview"));
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);

    Projection projection;
    constexpr double margin = 12.0;
    projection.centerX = static_cast<double>(width()) / 2.0;
    projection.centerY = static_cast<double>(height()) / 2.0;
    // Normalized coordinates span [-1, 1] after the two rotations.
    projection.scale = std::max(
        std::min(projection.centerX, projection.centerY) - margin, 1.0);

    // Translucent slice planes first so the wireframes stay readable.
    for (int axis = 0; axis < 3; ++axis) {
        drawSlicePlane(painter, projection, axis);
    }
    for (const auto& level : m_levels) {
        const QPen pen(levelOutlineColor(level.level), 1);
        for (const auto& box : level.boxes) {
            drawBox(painter, projection, physicalBox(level, box), pen);
        }
    }
    drawBox(painter, projection, m_domain, QPen(Qt::white, 1));
}

QPointF IsoWidget::project(const Projection& projection,
    double x, double y, double z) const
{
    const auto extent = std::max({m_domain.upper[0] - m_domain.lower[0],
        m_domain.upper[1] - m_domain.lower[1],
        m_domain.upper[2] - m_domain.lower[2]});
    const auto safeExtent = extent > 0.0 ? extent : 1.0;
    const auto nx = (x - 0.5 * (m_domain.lower[0] + m_domain.upper[0]))
        / safeExtent;
    const auto ny = (y - 0.5 * (m_domain.lower[1] + m_domain.upper[1]))
        / safeExtent;
    const auto nz = (z - 0.5 * (m_domain.lower[2] + m_domain.upper[2]))
        / safeExtent;
    const auto cosAz = std::cos(azimuth);
    const auto sinAz = std::sin(azimuth);
    const auto x1 = nx * cosAz - ny * sinAz;
    const auto y1 = nx * sinAz + ny * cosAz;
    const auto y2 = y1 * std::cos(elevation) - nz * std::sin(elevation);
    return QPointF(projection.centerX + projection.scale * x1,
        projection.centerY - projection.scale * y2);
}

void IsoWidget::drawBox(QPainter& painter, const Projection& projection,
    const RealBox& box, const QPen& pen) const
{
    std::array<QPointF, 8> corners;
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        const auto x = (corner & 1U) != 0U ? box.upper[0] : box.lower[0];
        const auto y = (corner & 2U) != 0U ? box.upper[1] : box.lower[1];
        const auto z = (corner & 4U) != 0U ? box.upper[2] : box.lower[2];
        corners[corner] = project(projection, x, y, z);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const auto& edge : boxEdges) {
        painter.drawLine(corners[static_cast<std::size_t>(edge[0])],
            corners[static_cast<std::size_t>(edge[1])]);
    }
}

void IsoWidget::drawSlicePlane(QPainter& painter, const Projection& projection,
    int axis) const
{
    const auto index = static_cast<std::size_t>(axis);
    const auto a = static_cast<std::size_t>((axis + 1) % 3);
    const auto b = static_cast<std::size_t>((axis + 2) % 3);
    const auto position = std::clamp(m_slicePositions[index],
        m_domain.lower[index], m_domain.upper[index]);
    QPolygonF polygon;
    for (unsigned int corner = 0; corner < 4; ++corner) {
        std::array<double, 3> point{};
        point[index] = position;
        point[a] = (corner & 1U) != 0U ? m_domain.upper[a] : m_domain.lower[a];
        point[b] = (corner & 2U) != 0U ? m_domain.upper[b] : m_domain.lower[b];
        polygon << project(projection, point[0], point[1], point[2]);
    }
    auto fill = slicePlaneColor(axis);
    fill.setAlpha(96);
    painter.setPen(QPen(slicePlaneColor(axis), 1));
    painter.setBrush(fill);
    painter.drawPolygon(polygon);
    painter.setBrush(Qt::NoBrush);
}

RealBox IsoWidget::physicalBox(const LevelBoxes& level, const IntBox& box) const
{
    RealBox physical;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        physical.lower[axis] = m_domain.lower[axis]
            + static_cast<double>(static_cast<std::int64_t>(box.lower[axis])
                - level.domain.lower[axis]) * level.cellSize[axis];
        physical.upper[axis] = m_domain.lower[axis]
            + static_cast<double>(static_cast<std::int64_t>(box.upper[axis])
                - level.domain.lower[axis] + 1) * level.cellSize[axis];
    }
    return physical;
}

QColor IsoWidget::levelOutlineColor(int level) const
{
    // Same rule as the 2-D grid-box overlays: coarse white, finer levels
    // spread across the palette.
    if (level <= 0 || m_palette == nullptr) {
        return QColor(Qt::white);
    }
    const auto finest = std::max(static_cast<int>(m_levels.size()) - 1, level);
    return QColor::fromRgb(static_cast<QRgb>(
        m_palette->levelColor(level, finest)));
}

QColor IsoWidget::slicePlaneColor(int axis) const
{
    // The same palette slots the 2-D views use for their crosshair guides:
    // x -> 65, y -> 220, z -> 255.
    constexpr std::array<int, 3> paletteSlots{65, 220, 255};
    if (m_palette == nullptr) {
        return QColor(Qt::white);
    }
    return QColor::fromRgba(static_cast<QRgb>(
        m_palette->slotArgb(paletteSlots[static_cast<std::size_t>(axis)])));
}

} // namespace amrvis::qt

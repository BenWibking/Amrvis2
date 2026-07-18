#pragma once

#include <amrvis/core/Metadata.hpp>

#include <QColor>
#include <QWidget>

#include <array>
#include <vector>

class QPainter;
class QPaintEvent;

namespace amrvis {
class Palette;
}

namespace amrvis::qt {

// The bottom-right quadrant of the 3-D layout: a fixed isometric-ish
// orthographic wireframe of the physical domain and the per-level grid
// boxes, with the three current slice planes drawn as translucent quads.
// Pure QPainter math (rotate about z, then about x); no OpenGL and no user
// interaction, mirroring the legacy Amrvis iso view.
class IsoWidget final : public QWidget {
    Q_OBJECT

public:
    explicit IsoWidget(QWidget* parent = nullptr);

    using QWidget::setGeometry;
    // Replaces the domain and per-level boxes; a non-3-D metadata clears the
    // widget back to its placeholder.
    void setGeometry(const DatasetMetadata& metadata);
    void setSlicePositions(double x, double y, double z);
    void setColorPalette(const Palette* palette);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct LevelBoxes {
        int level = 0;
        IntBox domain;
        Real3 cellSize;
        std::vector<IntBox> boxes;
    };
    struct Projection {
        double centerX = 0.0;
        double centerY = 0.0;
        double scale = 1.0;
    };

    [[nodiscard]] QPointF project(const Projection& projection,
        double x, double y, double z) const;
    void drawBox(QPainter& painter, const Projection& projection,
        const RealBox& box, const QPen& pen) const;
    void drawSlicePlane(QPainter& painter, const Projection& projection,
        int axis) const;
    [[nodiscard]] RealBox physicalBox(const LevelBoxes& level,
        const IntBox& box) const;
    [[nodiscard]] QColor levelOutlineColor(int level) const;
    [[nodiscard]] QColor slicePlaneColor(int axis) const;

    RealBox m_domain{};
    std::vector<LevelBoxes> m_levels;
    std::array<double, 3> m_slicePositions{0.0, 0.0, 0.0};
    const Palette* m_palette = nullptr;
    bool m_hasGeometry = false;
};

} // namespace amrvis::qt

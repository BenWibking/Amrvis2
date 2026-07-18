#pragma once

#include <QGraphicsView>
#include <QColor>
#include <QImage>
#include <QLineF>
#include <QPoint>
#include <QRectF>

#include <optional>
#include <vector>

class QGraphicsLineItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

namespace amrvis::qt {

struct GridBoxOverlay {
    QRectF rectangle;
    QColor color;
};

struct OverlaySegment {
    QLineF line;
    QColor color;
    float width = 1.0F;
};

class ImageView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setGridBoxes(const std::vector<GridBoxOverlay>& boxes);
    void setOverlaySegments(const std::vector<OverlaySegment>& segments);
    // Crosshair guides spanning the whole image, used by the 3-D slice views
    // to mark where the other two slice planes intersect this one. The lines
    // are in scene coordinates; a nullopt line hides that guide. They layer
    // at z 1.5, between the grid boxes (z 1) and the overlay segments (z 2).
    void setCrosshairs(const std::optional<QLineF>& vertical,
        const std::optional<QLineF>& horizontal, const QColor& verticalColor,
        const QColor& horizontalColor);
    // Cosmetic red rectangle marking the cell picked in the dataset window;
    // std::nullopt clears it, and setImage/setPlaceholder drop it too. It
    // layers at z 4, above the overlay segments.
    void setCellHighlight(const std::optional<QRectF>& sceneRect);
    void setPlaceholder(const QString& text);
    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] const QImage& image() const noexcept;
    void fitToWindow();
    void setFixedScale(int factor);
    void zoomToRect(const QRectF& sceneRect);
    // When enabled (the 3-D slice views), a middle/right drag released
    // without Shift or Control emits sliceMoveRequested instead of
    // linePlotRequested; with either modifier held it stays a line plot.
    void setSliceMoveEnabled(bool enabled) noexcept;

signals:
    void probeMoved(int x, int y);
    void probeClicked(int x, int y);
    void rubberBandSelected(const QRectF& sceneRect);
    void linePlotRequested(int imageX, int imageY, Qt::MouseButton button);
    void sliceMoveRequested(int imageX, int imageY, Qt::MouseButton button);
    void fitRequested();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void fitImage();
    void updateLineGuide(const QPoint& viewPosition);
    void clearLineGuide();
    void applyCrosshairs();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    QImage m_image;
    std::vector<QGraphicsRectItem*> m_gridItems;
    std::vector<QGraphicsLineItem*> m_overlayItems;
    std::optional<QLineF> m_crosshairVertical;
    std::optional<QLineF> m_crosshairHorizontal;
    QColor m_crosshairVerticalColor;
    QColor m_crosshairHorizontalColor;
    QGraphicsLineItem* m_crosshairVerticalItem = nullptr;
    QGraphicsLineItem* m_crosshairHorizontalItem = nullptr;
    QGraphicsRectItem* m_cellHighlightItem = nullptr;
    QPoint m_pressPosition;
    Qt::MouseButton m_lineDragButton = Qt::NoButton;
    QPoint m_linePressPosition;
    QGraphicsLineItem* m_lineGuide = nullptr;
    bool m_sliceMoveEnabled = false;
    bool m_fitOnResize = true;
};

} // namespace amrvis::qt

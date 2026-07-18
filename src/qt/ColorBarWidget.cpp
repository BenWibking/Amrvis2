#include "ColorBarWidget.hpp"
#include "NumberFormat.hpp"
#include "Theme.hpp"

#include <amrvis/render2d/Palette.hpp>

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace amrvis::qt {

namespace {

// Legacy Amrvis TOTALPALWIDTH: the whole color bar panel is 150 px wide.
constexpr int totalWidth = 150;
constexpr int margin = 8;
constexpr int titleHeight = 24;
constexpr int barWidth = 24;
constexpr int labelGap = 6;
constexpr int labelCount = 8;

} // namespace

ColorBarWidget::ColorBarWidget(QWidget* parent)
    : QWidget(parent)
    , m_numberFormat(defaultNumberFormat())
{
    setFixedWidth(totalWidth);
    setMinimumHeight(280);
}

void ColorBarWidget::setPalette(const amrvis::Palette* palette)
{
    m_palette = palette;
    update();
}

void ColorBarWidget::setFieldRange(QString fieldName, double minimum, double maximum)
{
    m_fieldName = std::move(fieldName);
    m_minimum = minimum;
    m_maximum = maximum;
    m_hasRange = true;
    update();
}

void ColorBarWidget::setNumberFormat(QString format)
{
    m_numberFormat = std::move(format);
    update();
}

void ColorBarWidget::clearRange()
{
    m_fieldName.clear();
    m_hasRange = false;
    update();
}

void ColorBarWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), viewportBackground());
    painter.setPen(Qt::white);

    if (!m_hasRange) {
        painter.drawText(rect().adjusted(margin, margin, -margin, -margin),
            Qt::AlignCenter | Qt::TextWordWrap, tr("No scalar range"));
        return;
    }

    const QRect bar(margin, margin + titleHeight, barWidth,
        std::max(1, height() - 2 * margin - titleHeight));
    const auto title = painter.fontMetrics().elidedText(
        m_fieldName, Qt::ElideRight, width() - 2 * margin);
    painter.drawText(QRect(margin, margin, width() - 2 * margin, titleHeight),
        Qt::AlignLeft | Qt::AlignVCenter, title);

    const auto& palette = m_palette != nullptr
        ? *m_palette : builtinPalette(BuiltinPalette::Rainbow);
    const auto rows = std::max(1, bar.height() - 1);
    for (int row = 0; row < bar.height(); ++row) {
        const auto normalized = 1.0
            - static_cast<double>(row) / static_cast<double>(rows);
        painter.setPen(QColor::fromRgb(static_cast<QRgb>(palette.argb(normalized))));
        painter.drawLine(bar.left(), bar.top() + row,
            bar.left() + bar.width() - 1, bar.top() + row);
    }
    painter.setPen(Qt::white);
    painter.drawRect(bar.adjusted(0, 0, -1, -1));

    const auto labelHeight = painter.fontMetrics().height();
    const auto labelLeft = bar.left() + bar.width() + labelGap;
    for (int label = 0; label < labelCount; ++label) {
        const auto fraction = static_cast<double>(label)
            / static_cast<double>(labelCount - 1);
        const auto value = m_maximum + fraction * (m_minimum - m_maximum);
        const auto center = bar.top()
            + static_cast<int>(std::lround(fraction * static_cast<double>(rows)));
        const auto top = std::clamp(center - labelHeight / 2, bar.top(),
            std::max(bar.top(), bar.top() + bar.height() - labelHeight));
        painter.drawText(QRect(labelLeft, top, width() - labelLeft - margin,
                labelHeight), Qt::AlignLeft | Qt::AlignVCenter,
            formatNumber(value, m_numberFormat));
    }
}

} // namespace amrvis::qt

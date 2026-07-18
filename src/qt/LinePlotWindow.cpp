#include "LinePlotWindow.hpp"
#include "NumberFormat.hpp"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QRect>
#include <QRubberBand>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace amrvis::qt {
namespace {

const std::array<QColor, 8>& curveColors()
{
    static const std::array<QColor, 8> colors{
        QColor(Qt::red), QColor(Qt::green), QColor(Qt::cyan), QColor(Qt::yellow),
        QColor(Qt::magenta), QColor(Qt::white), QColor(Qt::gray),
        QColor(0x66, 0xB2, 0xFF)};
    return colors;
}

constexpr std::array<const char*, 3> axisLetters{"x", "y", "z"};

QString levelPolicyText(const LinePlotCurve& curve)
{
    return curve.composition == CompositionPolicy::FinestAvailable
        ? QStringLiteral("Finest")
        : QStringLiteral("L%1").arg(curve.maximumLevel);
}

std::size_t sampleCount(const LinePlotCurve& curve)
{
    return std::min({curve.line.positions.size(), curve.line.values.size(),
        curve.line.valid.size()});
}

} // namespace

LinePlotWidget::LinePlotWidget(QWidget* parent)
    : QWidget(parent)
    , m_numberFormat(defaultNumberFormat())
{
    setMinimumSize(420, 300);
}

void LinePlotWidget::setCurves(const std::vector<LinePlotCurve>* curves)
{
    m_curves = curves;
    update();
}

void LinePlotWidget::setNumberFormat(QString format)
{
    m_numberFormat = std::move(format);
    update();
}

void LinePlotWidget::resetZoom()
{
    m_zoom.reset();
    update();
}

QRect LinePlotWidget::plotRect() const
{
    constexpr int leftMargin = 64;
    constexpr int rightMargin = 12;
    constexpr int topMargin = 12;
    constexpr int bottomMargin = 36;
    return QRect(leftMargin, topMargin,
        std::max(width() - leftMargin - rightMargin, 16),
        std::max(height() - topMargin - bottomMargin, 16));
}

std::optional<QRectF> LinePlotWidget::automaticRange() const
{
    if (m_curves == nullptr) {
        return std::nullopt;
    }
    auto xMinimum = std::numeric_limits<double>::infinity();
    auto xMaximum = -std::numeric_limits<double>::infinity();
    auto yMinimum = std::numeric_limits<double>::infinity();
    auto yMaximum = -std::numeric_limits<double>::infinity();
    auto any = false;
    for (const auto& curve : *m_curves) {
        if (!curve.visible) {
            continue;
        }
        const auto count = sampleCount(curve);
        for (std::size_t sample = 0; sample < count; ++sample) {
            if (curve.line.valid[sample] == 0) {
                continue;
            }
            const auto value = static_cast<double>(curve.line.values[sample]);
            if (!std::isfinite(value)) {
                continue;
            }
            any = true;
            xMinimum = std::min(xMinimum, curve.line.positions[sample]);
            xMaximum = std::max(xMaximum, curve.line.positions[sample]);
            yMinimum = std::min(yMinimum, value);
            yMaximum = std::max(yMaximum, value);
        }
    }
    if (!any) {
        return std::nullopt;
    }
    if (xMinimum == xMaximum) {
        const auto padding = std::max(std::abs(xMinimum), 1.0) * 1.0e-6;
        xMinimum -= padding;
        xMaximum += padding;
    }
    if (yMinimum == yMaximum) {
        const auto padding = std::max(std::abs(yMinimum), 1.0) * 1.0e-6;
        yMinimum -= padding;
        yMaximum += padding;
    }
    return QRectF(QPointF(xMinimum, yMinimum), QPointF(xMaximum, yMaximum));
}

std::optional<QRectF> LinePlotWidget::displayedRange() const
{
    if (m_zoom.has_value()) {
        return m_zoom;
    }
    return automaticRange();
}

void LinePlotWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    const auto range = displayedRange();
    if (!range.has_value()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter,
            tr("Middle-drag for a line along X, right-drag for a line along Y"));
        return;
    }
    const auto plot = plotRect();
    // Normalized data rect: left/right are x min/max, top/bottom y min/max.
    const auto xMinimum = range->left();
    const auto xMaximum = range->right();
    const auto yMinimum = range->top();
    const auto yMaximum = range->bottom();
    const auto mapX = [&](double value) {
        return plot.left() + (value - xMinimum) / (xMaximum - xMinimum) * plot.width();
    };
    const auto mapY = [&](double value) {
        return plot.bottom() - (value - yMinimum) / (yMaximum - yMinimum) * plot.height();
    };

    const QPen gridPen(QColor(96, 96, 96));
    constexpr int tickCount = 5;
    for (int tick = 0; tick < tickCount; ++tick) {
        const auto fraction = static_cast<double>(tick) / (tickCount - 1);
        const auto xValue = xMinimum + fraction * (xMaximum - xMinimum);
        const auto yValue = yMinimum + fraction * (yMaximum - yMinimum);
        const auto x = mapX(xValue);
        const auto y = mapY(yValue);
        painter.setPen(gridPen);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(Qt::white);
        painter.drawText(QRectF(x - 40.0, plot.bottom() + 4.0, 80.0, 16.0),
            Qt::AlignHCenter | Qt::AlignTop, formatNumber(xValue, m_numberFormat));
        painter.drawText(QRectF(0.0, y - 8.0, plot.left() - 6.0, 16.0),
            Qt::AlignRight | Qt::AlignVCenter,
            formatNumber(yValue, m_numberFormat));
    }
    painter.setPen(Qt::white);
    painter.drawRect(plot);

    if (m_curves == nullptr) {
        return;
    }
    painter.save();
    painter.setClipRect(plot);
    for (const auto& curve : *m_curves) {
        if (!curve.visible) {
            continue;
        }
        painter.setPen(QPen(curve.color));
        const auto count = sampleCount(curve);
        QPolygonF run;
        const auto flushRun = [&] {
            if (run.size() == 1) {
                painter.drawPoint(run.first());
            } else if (run.size() > 1) {
                painter.drawPolyline(run);
            }
            run.clear();
        };
        for (std::size_t sample = 0; sample < count; ++sample) {
            if (curve.line.valid[sample] == 0) {
                flushRun();
                continue;
            }
            run.append(QPointF(mapX(curve.line.positions[sample]),
                mapY(static_cast<double>(curve.line.values[sample]))));
        }
        flushRun();
    }
    painter.restore();
}

void LinePlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton
        && plotRect().contains(event->position().toPoint())) {
        m_pressPosition = event->position().toPoint();
        m_dragging = true;
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        resetZoom();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void LinePlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        if (m_rubberBand == nullptr) {
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
        m_rubberBand->setGeometry(
            QRect(m_pressPosition, event->position().toPoint()).normalized());
        m_rubberBand->show();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void LinePlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_rubberBand != nullptr) {
            m_rubberBand->hide();
            m_rubberBand->deleteLater();
            m_rubberBand = nullptr;
        }
        const auto dragged = QRect(m_pressPosition, event->position().toPoint())
            .normalized().intersected(plotRect());
        const auto base = displayedRange();
        if (base.has_value() && dragged.width() >= 4 && dragged.height() >= 4) {
            const auto plot = plotRect();
            const auto xMinimum = base->left()
                + static_cast<double>(dragged.left() - plot.left()) / plot.width()
                    * base->width();
            const auto xMaximum = base->left()
                + static_cast<double>(dragged.right() - plot.left()) / plot.width()
                    * base->width();
            const auto yMaximum = base->top()
                + static_cast<double>(plot.bottom() - dragged.top()) / plot.height()
                    * base->height();
            const auto yMinimum = base->top()
                + static_cast<double>(plot.bottom() - dragged.bottom()) / plot.height()
                    * base->height();
            m_zoom = QRectF(QPointF(xMinimum, yMinimum), QPointF(xMaximum, yMaximum))
                .normalized();
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

LinePlotWindow::LinePlotWindow(const QString& datasetName, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Line Plot — %1").arg(datasetName));
    resize(780, 480);

    m_plot = new LinePlotWidget(this);
    m_plot->setCurves(&m_curves);

    m_legend = new QListWidget(this);
    m_legend->setMaximumWidth(260);

    auto* clearButton = new QPushButton(tr("Clear"), this);
    auto* removeButton = new QPushButton(tr("Remove Selected"), this);
    auto* exportButton = new QPushButton(tr("Export ASCII..."), this);
    auto* zoomButton = new QPushButton(tr("Reset Zoom"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);

    auto* sideLayout = new QVBoxLayout;
    sideLayout->addWidget(m_legend);
    sideLayout->addWidget(clearButton);
    sideLayout->addWidget(removeButton);
    sideLayout->addWidget(exportButton);
    sideLayout->addWidget(zoomButton);
    sideLayout->addStretch();
    sideLayout->addWidget(closeButton);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_plot, 1);
    layout->addLayout(sideLayout);

    connect(m_legend, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const auto row = m_legend->row(item);
        if (row < 0 || static_cast<std::size_t>(row) >= m_curves.size()) {
            return;
        }
        m_curves[static_cast<std::size_t>(row)].visible
            = item->checkState() == Qt::Checked;
        m_plot->update();
    });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearCurves(); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { exportAscii(); });
    connect(zoomButton, &QPushButton::clicked, m_plot, &LinePlotWidget::resetZoom);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
}

void LinePlotWindow::setNumberFormat(QString format)
{
    m_plot->setNumberFormat(std::move(format));
}

void LinePlotWindow::addCurve(LinePlotCurve curve)
{
    curve.color = curveColors()[m_addedCurves % curveColors().size()];
    ++m_addedCurves;
    curve.visible = true;
    m_curves.push_back(std::move(curve));
    const auto& added = m_curves.back();
    QPixmap swatch(14, 14);
    swatch.fill(added.color);
    auto* item = new QListWidgetItem(QIcon(swatch), curveDescription(added), m_legend);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    m_plot->update();
}

QString LinePlotWindow::curveDescription(const LinePlotCurve& curve) const
{
    const auto fixed = static_cast<std::size_t>(curve.primaryFixedAxis);
    return tr("%1 %2=%3 [%4]")
        .arg(QString::fromStdString(curve.fieldName))
        .arg(QLatin1String(axisLetters[fixed]))
        .arg(curve.fixedCoordinates[fixed], 0, 'g', 6)
        .arg(levelPolicyText(curve));
}

void LinePlotWindow::clearCurves()
{
    m_curves.clear();
    m_legend->clear();
    m_plot->resetZoom();
    m_plot->update();
}

void LinePlotWindow::removeSelected()
{
    std::vector<int> rows;
    const auto selected = m_legend->selectedItems();
    rows.reserve(static_cast<std::size_t>(selected.size()));
    for (const auto* item : selected) {
        rows.push_back(m_legend->row(item));
    }
    std::sort(rows.rbegin(), rows.rend());
    for (const auto row : rows) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_curves.size()) {
            continue;
        }
        m_curves.erase(m_curves.begin() + row);
        delete m_legend->takeItem(row);
    }
    m_plot->update();
}

void LinePlotWindow::exportAscii()
{
    if (m_curves.empty()) {
        QMessageBox::information(this, tr("No curves"),
            tr("There are no line plot curves to export."));
        return;
    }
    const auto filename = QFileDialog::getSaveFileName(this,
        tr("Export line plot"), QString(), tr("ASCII text (*.txt);;All files (*)"));
    if (filename.isEmpty()) {
        return;
    }
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Cannot export line plot"),
            tr("The file %1 could not be opened for writing.").arg(filename));
        return;
    }
    QTextStream stream(&file);
    for (const auto& curve : m_curves) {
        QStringList fixedParts;
        for (int axis = 0; axis < curve.dimension; ++axis) {
            if (axis == curve.line.axis) {
                continue;
            }
            const auto index = static_cast<std::size_t>(axis);
            fixedParts << QStringLiteral("%1=%2")
                .arg(QLatin1String(axisLetters[index]))
                .arg(curve.fixedCoordinates[index], 0, 'g', 6);
        }
        stream << "# " << QString::fromStdString(curve.fieldName) << " "
            << QLatin1String(axisLetters[static_cast<std::size_t>(curve.line.axis)])
            << " fixed=" << fixedParts.join(QLatin1Char(','))
            << " level=" << levelPolicyText(curve) << "\n";
        const auto count = sampleCount(curve);
        for (std::size_t sample = 0; sample < count; ++sample) {
            if (curve.line.valid[sample] == 0) {
                continue;
            }
            stream << QString::number(curve.line.positions[sample], 'g', 17) << " "
                << QString::number(static_cast<double>(curve.line.values[sample]),
                    'g', 9)
                << "\n";
        }
        stream << "\n";
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        QMessageBox::critical(this, tr("Cannot export line plot"),
            tr("Writing to %1 failed.").arg(filename));
    }
}

} // namespace amrvis::qt

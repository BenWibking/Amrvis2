#pragma once

#include <QDialog>

#include <string>
#include <utility>
#include <vector>

class QButtonGroup;
class QComboBox;
class QGroupBox;
class QSpinBox;

namespace amrvis::qt {

enum class DisplayMode {
    Raster,
    RasterContours,
    ColorContours,
    BWContours,
    VelocityVectors
};

// Legacy vector-field defaults: a case-insensitive "x_velocity"/"y_velocity"
// substring match, then an exact "u"/"v" field name, else the first two
// fields.
[[nodiscard]] std::pair<int, int> detectVectorFields(
    const std::vector<std::string>& fieldNames);

class SetContoursDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SetContoursDialog(const std::vector<std::string>& fieldNames,
        QWidget* parent = nullptr);

    void setMode(DisplayMode mode);
    void setContourCount(int count);
    void setVectorFields(int uField, int vField);
    [[nodiscard]] DisplayMode mode() const;
    [[nodiscard]] int contourCount() const;
    [[nodiscard]] int uField() const;
    [[nodiscard]] int vField() const;

signals:
    void applied();

private:
    QButtonGroup* m_modeButtons = nullptr;
    QSpinBox* m_contourCount = nullptr;
    QGroupBox* m_vectorBox = nullptr;
    QComboBox* m_uField = nullptr;
    QComboBox* m_vField = nullptr;
};

} // namespace amrvis::qt

#pragma once

#include <QString>
#include <QWidget>

namespace amrvis {
class Palette;
}

namespace amrvis::qt {

class ColorBarWidget final : public QWidget {
public:
    explicit ColorBarWidget(QWidget* parent = nullptr);

    void setPalette(const amrvis::Palette* palette);
    void setFieldRange(QString fieldName, double minimum, double maximum);
    void setNumberFormat(QString format);
    void setLogarithmic(bool logarithmic);
    void clearRange();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const amrvis::Palette* m_palette = nullptr;
    QString m_fieldName;
    QString m_numberFormat;
    double m_minimum = 0.0;
    double m_maximum = 1.0;
    bool m_logarithmic = false;
    bool m_hasRange = false;
};

} // namespace amrvis::qt

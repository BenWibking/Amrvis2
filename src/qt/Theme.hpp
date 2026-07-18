#pragma once

#include <QColor>

namespace amrvis::qt {

// Background shared by the image viewports and the color scale: a dark gray
// that reads as a distinct panel without the harshness of pure black. Adjust
// here to retune every viewport at once.
inline QColor viewportBackground()
{
    return {0x88, 0x88, 0x88};
}

} // namespace amrvis::qt

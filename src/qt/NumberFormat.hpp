#pragma once

#include <QString>

namespace amrvis::qt {

// The legacy Amrvis default readout format (View -> Number Format...).
QString defaultNumberFormat();

// True when format is a printf-style string carrying exactly one
// floating-point conversion specifier: '%', optional flags out of -+0 #,
// optional decimal width, optional .precision, then one of eEfgG. Literal
// text and %% escapes may surround the specifier; anything else is rejected
// (no %d/%s/%n, no '*' width or precision, no trailing '%', no second
// specifier).
bool isValidNumberFormat(const QString& format);

// snprintf()s value through the validated format into a 128-byte buffer
// (C locale; Qt never installs another one). An invalid format or a result
// that does not fit falls back to 'g' with 7 significant digits.
QString formatNumber(double value, const QString& format);

} // namespace amrvis::qt

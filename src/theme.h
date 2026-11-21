#pragma once

#include <QColor>
#include <QString>

#include "appsettings.h"

// Applies the color scheme (style sheet + palette) to the whole application.
namespace Theme {

struct Palette {
    QColor windowBg;
    QColor surfaceBg;
    QColor cardBg;
    QColor cardBorder;
    QColor separator;
    QColor text;
    QColor muted;
    QColor placeholder;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent;
    QColor chipBg;
    QColor iconAccent;
    QColor inputBg;
    QColor inputBorder;
    QColor inputFocus;
    QColor buttonBg;
    QColor buttonBorder;
    QColor buttonHover;
    QColor buttonPressed;
    QColor shadow;
};

// Resolves Appearance::Auto against the system color scheme and applies the result.
void apply(Appearance appearance);

Appearance currentAppearance();
bool isDark();
const Palette& palette();

} // namespace Theme

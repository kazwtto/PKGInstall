#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

// Small vector glyphs drawn at runtime, so they always match the active theme colors.
namespace Icons {

enum class Glyph {
    Package,
    Folder,
    Database,
    Gear,
    Globe,
    ChevronDown,
    Search,
    List,
    Close,
    Minimize,
    Check,
};

// Device pixels per logical pixel on the primary screen (1.0, 1.25, 1.5, 2.0, ...).
qreal screenRatio();

// Renders the glyph in a transparent box. dpr <= 0 uses the primary screen's ratio.
QPixmap pixmap(Glyph glyph, const QColor& color, int size, qreal dpr = 0.0);
QIcon icon(Glyph glyph, const QColor& color, int size);

// Renders the glyph centered inside a larger transparent box (used for style sheet images).
QImage image(Glyph glyph, const QColor& color, qreal glyphSize, int boxSize);

} // namespace Icons

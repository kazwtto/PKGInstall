#include "icons.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTransform>
#include <cmath>

namespace {

// All glyphs are designed on a 24x24 grid and scaled to the requested size.
constexpr qreal kGrid = 24.0;

qreal screenDpr() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return 1.0;
    }
    const qreal dpr = screen->devicePixelRatio();
    return dpr > 0.0 ? dpr : 1.0;
}

QPen strokePen(const QColor& color, qreal width) {
    QPen pen(color);
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setBrush(color);
    return pen;
}

void drawPackage(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 1.6));
    p.setBrush(Qt::NoBrush);

    QPainterPath lid;
    lid.moveTo(12.0, 3.0);
    lid.lineTo(20.8, 7.6);
    lid.lineTo(12.0, 12.2);
    lid.lineTo(3.2, 7.6);
    lid.closeSubpath();
    p.drawPath(lid);

    p.drawLine(QPointF(3.2, 7.6), QPointF(3.2, 16.2));
    p.drawLine(QPointF(20.8, 7.6), QPointF(20.8, 16.2));
    p.drawLine(QPointF(12.0, 12.2), QPointF(12.0, 20.8));

    QPainterPath bottom;
    bottom.moveTo(3.2, 16.2);
    bottom.lineTo(12.0, 20.8);
    bottom.lineTo(20.8, 16.2);
    p.drawPath(bottom);
}

void drawFolder(QPainter& p, const QColor& color) {
    QPainterPath path;
    path.moveTo(2.6, 6.8);
    path.cubicTo(2.6, 5.6, 3.6, 4.6, 4.8, 4.6);
    path.lineTo(9.2, 4.6);
    path.cubicTo(9.8, 4.6, 10.4, 4.9, 10.8, 5.4);
    path.lineTo(12.0, 7.0);
    path.lineTo(19.2, 7.0);
    path.cubicTo(20.4, 7.0, 21.4, 8.0, 21.4, 9.2);
    path.lineTo(21.4, 17.4);
    path.cubicTo(21.4, 18.6, 20.4, 19.6, 19.2, 19.6);
    path.lineTo(4.8, 19.6);
    path.cubicTo(3.6, 19.6, 2.6, 18.6, 2.6, 17.4);
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
}

void drawDatabase(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 1.6));
    p.setBrush(Qt::NoBrush);

    p.drawEllipse(QRectF(4.8, 3.2, 14.4, 5.8));
    p.drawLine(QPointF(4.8, 6.1), QPointF(4.8, 17.9));
    p.drawLine(QPointF(19.2, 6.1), QPointF(19.2, 17.9));

    QPainterPath bottom;
    bottom.arcMoveTo(QRectF(4.8, 15.0, 14.4, 5.8), 180.0);
    bottom.arcTo(QRectF(4.8, 15.0, 14.4, 5.8), 180.0, 180.0);
    p.drawPath(bottom);

    QPainterPath middle;
    middle.arcMoveTo(QRectF(4.8, 10.0, 14.4, 5.8), 0.0);
    middle.arcTo(QRectF(4.8, 10.0, 14.4, 5.8), 0.0, -180.0);
    p.drawPath(middle);
}

void drawGear(QPainter& p, const QColor& color) {
    QPainterPath gear;
    gear.addEllipse(QPointF(12.0, 12.0), 6.7, 6.7);

    for (int i = 0; i < 8; ++i) {
        QPainterPath tooth;
        tooth.addRoundedRect(QRectF(10.4, 2.4, 3.2, 5.4), 1.5, 1.5);

        QTransform transform;
        transform.translate(12.0, 12.0);
        transform.rotate(i * 45.0);
        transform.translate(-12.0, -12.0);
        gear = gear.united(transform.map(tooth));
    }

    QPainterPath hole;
    hole.addEllipse(QPointF(12.0, 12.0), 3.1, 3.1);

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(gear.subtracted(hole));
}

void drawGlobe(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 1.5));
    p.setBrush(Qt::NoBrush);

    p.drawEllipse(QPointF(12.0, 12.0), 8.4, 8.4);
    p.drawEllipse(QPointF(12.0, 12.0), 3.9, 8.4);
    p.drawLine(QPointF(3.6, 12.0), QPointF(20.4, 12.0));

    const qreal chord = std::sqrt(8.4 * 8.4 - 4.4 * 4.4);
    p.drawLine(QPointF(12.0 - chord, 16.4), QPointF(12.0 + chord, 16.4));
    p.drawLine(QPointF(12.0 - chord, 7.6), QPointF(12.0 + chord, 7.6));
}

void drawChevronDown(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 2.2));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    path.moveTo(7.0, 9.8);
    path.lineTo(12.0, 14.8);
    path.lineTo(17.0, 9.8);
    p.drawPath(path);
}

void drawSearch(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 1.9));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10.4, 10.4), 6.1, 6.1);
    p.drawLine(QPointF(14.9, 14.9), QPointF(20.0, 20.0));
}

void drawList(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 1.8));
    p.setBrush(color);

    for (int i = 0; i < 3; ++i) {
        const qreal y = 6.8 + i * 5.2;
        p.drawEllipse(QPointF(4.6, y), 1.5, 1.5);
        p.drawLine(QPointF(9.4, y), QPointF(19.4, y));
    }
}

void drawClose(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 2.1));
    p.drawLine(QPointF(7.2, 7.2), QPointF(16.8, 16.8));
    p.drawLine(QPointF(16.8, 7.2), QPointF(7.2, 16.8));
}

void drawMinimize(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 2.1));
    p.drawLine(QPointF(7.0, 12.5), QPointF(17.0, 12.5));
}

void drawCheck(QPainter& p, const QColor& color) {
    p.setPen(strokePen(color, 2.9));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    path.moveTo(5.8, 12.6);
    path.lineTo(10.2, 17.0);
    path.lineTo(18.2, 8.0);
    p.drawPath(path);
}

void drawGlyph(QPainter& p, Icons::Glyph glyph, const QColor& color) {
    switch (glyph) {
    case Icons::Glyph::Package:
        drawPackage(p, color);
        break;
    case Icons::Glyph::Folder:
        drawFolder(p, color);
        break;
    case Icons::Glyph::Database:
        drawDatabase(p, color);
        break;
    case Icons::Glyph::Gear:
        drawGear(p, color);
        break;
    case Icons::Glyph::Globe:
        drawGlobe(p, color);
        break;
    case Icons::Glyph::ChevronDown:
        drawChevronDown(p, color);
        break;
    case Icons::Glyph::Search:
        drawSearch(p, color);
        break;
    case Icons::Glyph::List:
        drawList(p, color);
        break;
    case Icons::Glyph::Close:
        drawClose(p, color);
        break;
    case Icons::Glyph::Minimize:
        drawMinimize(p, color);
        break;
    case Icons::Glyph::Check:
        drawCheck(p, color);
        break;
    }
}

} // namespace

qreal Icons::screenRatio() {
    return screenDpr();
}

QImage Icons::image(Glyph glyph, const QColor& color, qreal glyphSize, int boxSize) {
    const int pixels = qMax(1, boxSize);
    QImage img(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal size = glyphSize > 0.0 ? glyphSize : pixels;
    const qreal offset = (pixels - size) / 2.0;
    p.translate(offset, offset);
    p.scale(size / kGrid, size / kGrid);

    drawGlyph(p, glyph, color);
    p.end();

    return img;
}

QPixmap Icons::pixmap(Glyph glyph, const QColor& color, int size, qreal dpr) {
    // Render the exact number of device pixels the screen needs: a 125% display wants
    // 1.25 device pixels per logical pixel, and rounding that to 1 or 2 gives blurry icons.
    const qreal ratio = dpr > 0.0 ? dpr : Icons::screenRatio();
    const int pixels = qMax(1, static_cast<int>(std::ceil(size * ratio)));

    QPixmap pixmap = QPixmap::fromImage(image(glyph, color, size * ratio, pixels));
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

QIcon Icons::icon(Glyph glyph, const QColor& color, int size) {
    QIcon result;
    // Qt picks the closest variant, so the usual desktop scales are all covered.
    static constexpr qreal ratios[] = {1.0, 1.25, 1.5, 2.0, 3.0};
    for (qreal ratio : ratios) {
        result.addPixmap(pixmap(glyph, color, size, ratio));
    }
    return result;
}

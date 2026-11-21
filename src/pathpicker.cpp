#include "pathpicker.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizeF>
#include <QStyle>
#include <QtMath>

PathPicker::PathPicker(QWidget* parent) : QPushButton(parent) {
    setCursor(Qt::PointingHandCursor);

    glyphLabel = new QLabel(this);
    glyphLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    glyphLabel->setAlignment(Qt::AlignCenter);

    textLabel = new QLabel(this);
    textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    textLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    textLabel->setProperty("pickerPlaceholder", true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 12, 0);
    layout->setSpacing(8);
    layout->addWidget(glyphLabel);
    layout->addWidget(textLabel, 1);

    updateDisplay();
}

void PathPicker::setGlyph(const QPixmap& glyph) {
    glyphLabel->setPixmap(glyph);

    // Size the label from the pixmap's logical size so the glyph is never rescaled.
    const QSizeF logical = glyph.deviceIndependentSize();
    glyphLabel->setFixedSize(qCeil(logical.width()), qCeil(logical.height()));
}

void PathPicker::setPlaceholderText(const QString& text) {
    placeholderText = text;
    updateDisplay();
}

void PathPicker::setPath(const QString& path) {
    selectedPath = path;
    updateDisplay();
}

QString PathPicker::path() const {
    return selectedPath;
}

void PathPicker::resizeEvent(QResizeEvent* event) {
    QPushButton::resizeEvent(event);
    updateDisplay();
}

void PathPicker::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        updateDisplay();
    }
    QPushButton::changeEvent(event);
}

void PathPicker::updateDisplay() {
    const bool placeholder = selectedPath.isEmpty();

    if (placeholder != showingPlaceholder) {
        showingPlaceholder = placeholder;
        textLabel->setProperty("pickerPlaceholder", placeholder);
        textLabel->style()->unpolish(textLabel);
        textLabel->style()->polish(textLabel);
    }

    const QString text = placeholder ? placeholderText : selectedPath;
    const QFontMetrics metrics(textLabel->font());
    textLabel->setText(metrics.elidedText(text, Qt::ElideMiddle, qMax(0, textLabel->width())));
}

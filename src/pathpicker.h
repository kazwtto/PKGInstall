#pragma once

#include <QPushButton>

class QLabel;

// A field that behaves like a button: clicking it opens a file/folder dialog.
// The glyph sits on the left, the selected path is shown on the right.
class PathPicker : public QPushButton {
    Q_OBJECT

public:
    explicit PathPicker(QWidget* parent = nullptr);

    void setGlyph(const QPixmap& glyph);
    void setPlaceholderText(const QString& text);
    void setPath(const QString& path);
    QString path() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateDisplay();

    QLabel* glyphLabel = nullptr;
    QLabel* textLabel = nullptr;
    QString selectedPath;
    QString placeholderText;
    bool showingPlaceholder = true;
};

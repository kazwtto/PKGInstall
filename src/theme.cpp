#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QStyleHints>

#include "icons.h"

namespace {

Theme::Palette g_palette;
Appearance g_appearance = Appearance::Auto;

QColor color(const char* hex) {
    return QColor(QString::fromLatin1(hex));
}

Theme::Palette makeLightPalette() {
    Theme::Palette p;
    p.windowBg = color("#F3F6F9");
    p.surfaceBg = color("#FFFFFF");
    p.cardBg = color("#FFFFFF");
    p.cardBorder = color("#E4EAF1");
    p.separator = color("#E7ECF2");
    p.text = color("#14181F");
    p.muted = color("#5B6472");
    p.placeholder = color("#8A929F");
    p.accent = color("#2682F6");
    p.accentHover = color("#1A73E8");
    p.accentPressed = color("#1660C9");
    p.onAccent = color("#FFFFFF");
    p.chipBg = color("#E4F0FD");
    p.iconAccent = color("#1A6CF0");
    p.inputBg = color("#FFFFFF");
    p.inputBorder = color("#D7DAE5");
    p.inputFocus = color("#2682F6");
    p.buttonBg = color("#F7F9FC");
    p.buttonBorder = color("#DCE0EA");
    p.buttonHover = color("#EEF3FA");
    p.buttonPressed = color("#E3EAF4");
    p.shadow = QColor(16, 24, 40, 46);
    return p;
}

Theme::Palette makeDarkPalette() {
    Theme::Palette p;
    p.windowBg = color("#15171C");
    p.surfaceBg = color("#1C1F25");
    p.cardBg = color("#1C1F25");
    p.cardBorder = color("#2B3038");
    p.separator = color("#262B33");
    p.text = color("#E9EDF3");
    p.muted = color("#A6AEBB");
    p.placeholder = color("#7C8593");
    p.accent = color("#2E86F7");
    p.accentHover = color("#4A9BFA");
    p.accentPressed = color("#1F6BD4");
    p.onAccent = color("#FFFFFF");
    p.chipBg = color("#17263E");
    p.iconAccent = color("#4D96FF");
    p.inputBg = color("#14161B");
    p.inputBorder = color("#3A4049");
    p.inputFocus = color("#2E86F7");
    p.buttonBg = color("#242831");
    p.buttonBorder = color("#373D47");
    p.buttonHover = color("#2C323C");
    p.buttonPressed = color("#20242C");
    p.shadow = QColor(0, 0, 0, 140);
    return p;
}

bool resolveDark(Appearance appearance) {
    if (appearance == Appearance::Light) {
        return false;
    }
    if (appearance == Appearance::Dark) {
        return true;
    }
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Dark;
}

void applyPalette(const Theme::Palette& p) {
    QPalette palette;
    palette.setColor(QPalette::Window, p.windowBg);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.inputBg);
    palette.setColor(QPalette::AlternateBase, p.cardBg);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::PlaceholderText, p.placeholder);
    palette.setColor(QPalette::Button, p.buttonBg);
    palette.setColor(QPalette::ButtonText, p.text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, p.accent);
    palette.setColor(QPalette::HighlightedText, p.onAccent);
    palette.setColor(QPalette::ToolTipBase, p.surfaceBg);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::Link, p.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, p.placeholder);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.placeholder);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.placeholder);

    QApplication::setPalette(palette);
}

struct IndicatorFiles {
    QString chevronUrl;
    QString checkUrl;
};

// Combo arrows and the check mark come from the resource system: style sheets can only load
// images through urls, and the resource urls work on every platform and deployment.
IndicatorFiles indicatorFiles(bool dark) {
    IndicatorFiles files;
    files.chevronUrl = dark ? QStringLiteral(":/icons/chevron-dark.png")
                            : QStringLiteral(":/icons/chevron-light.png");
    files.checkUrl = QStringLiteral(":/icons/check.png");
    return files;
}

QString buildStyleSheet(const Theme::Palette& p, const IndicatorFiles& files) {
    QFile file(QStringLiteral(":/styles/app.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString styleSheet = QString::fromUtf8(file.readAll());

    const QHash<QString, QString> tokens = {
        {QStringLiteral("WindowBg"), p.windowBg.name()},
        {QStringLiteral("SurfaceBg"), p.surfaceBg.name()},
        {QStringLiteral("CardBg"), p.cardBg.name()},
        {QStringLiteral("CardBorder"), p.cardBorder.name()},
        {QStringLiteral("Separator"), p.separator.name()},
        {QStringLiteral("Text"), p.text.name()},
        {QStringLiteral("Muted"), p.muted.name()},
        {QStringLiteral("Subtext"), p.muted.name()},
        {QStringLiteral("Placeholder"), p.placeholder.name()},
        {QStringLiteral("Accent"), p.accent.name()},
        {QStringLiteral("AccentHover"), p.accentHover.name()},
        {QStringLiteral("AccentPressed"), p.accentPressed.name()},
        {QStringLiteral("OnAccent"), p.onAccent.name()},
        {QStringLiteral("ChipBg"), p.chipBg.name()},
        {QStringLiteral("IconAccent"), p.iconAccent.name()},
        {QStringLiteral("InputBg"), p.inputBg.name()},
        {QStringLiteral("InputBorder"), p.inputBorder.name()},
        {QStringLiteral("InputFocus"), p.inputFocus.name()},
        {QStringLiteral("ButtonBg"), p.buttonBg.name()},
        {QStringLiteral("ButtonBorder"), p.buttonBorder.name()},
        {QStringLiteral("ButtonHover"), p.buttonHover.name()},
        {QStringLiteral("ButtonPressed"), p.buttonPressed.name()},
        {QStringLiteral("ChevronUrl"), files.chevronUrl},
        {QStringLiteral("CheckUrl"), files.checkUrl},
    };

    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        styleSheet.replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
    }

    return styleSheet;
}

} // namespace

void Theme::apply(Appearance appearance) {
    g_appearance = appearance;

    const bool dark = resolveDark(appearance);
    g_palette = dark ? makeDarkPalette() : makeLightPalette();

    applyPalette(g_palette);

    if (qApp == nullptr) {
        return;
    }

    qApp->setStyleSheet(buildStyleSheet(g_palette, indicatorFiles(dark)));
}

Appearance Theme::currentAppearance() {
    return g_appearance;
}

bool Theme::isDark() {
    return g_palette.windowBg.lightness() < 128;
}

const Theme::Palette& Theme::palette() {
    return g_palette;
}

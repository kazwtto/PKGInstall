#pragma once

#include <QString>
#include <filesystem>

// User selectable color scheme.
enum class Appearance { Auto, Light, Dark };

// Preferences that must be known before the main window is created.
// "system" follows the operating system language.
struct UiPreferences {
    QString language = QStringLiteral("system");
    Appearance appearance = Appearance::Auto;
};

namespace AppSettings {
// Location of settings.toml (per-user, platform specific).
std::filesystem::path filePath();

UiPreferences loadUiPreferences();
void saveUiPreferences(const UiPreferences& preferences);

QString appearanceToString(Appearance appearance);
Appearance appearanceFromString(const QString& value);
} // namespace AppSettings

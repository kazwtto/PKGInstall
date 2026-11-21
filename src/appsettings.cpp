#include "appsettings.h"
#include "path_utils.h"

#include <QStandardPaths>
#include <fstream>
#include <toml.hpp>

namespace {

// Reads the settings file, returns an empty value when it does not exist or is malformed.
toml::value ReadSettings(const std::filesystem::path& file) {
    toml::value data;
    if (!std::filesystem::exists(file)) {
        return data;
    }

    try {
        std::ifstream ifs;
        ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        ifs.open(file, std::ios_base::binary);
        data = toml::parse(ifs, Common::Path::toUtf8(file.filename()));
    } catch (...) {
        data = toml::value{};
    }

    return data;
}

} // namespace

std::filesystem::path AppSettings::filePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return Common::Path::fromQString(dir) / "settings.toml";
}

UiPreferences AppSettings::loadUiPreferences() {
    UiPreferences preferences;

    const toml::value data = ReadSettings(filePath());
    if (!data.is_table() || !data.contains("UI")) {
        return preferences;
    }

    const toml::value& ui = data.at("UI");
    if (!ui.is_table()) {
        return preferences;
    }
    preferences.language =
        QString::fromStdString(toml::find_or<std::string>(ui, "language", "system"));
    preferences.appearance =
        appearanceFromString(QString::fromStdString(toml::find_or<std::string>(ui, "appearance", "auto")));

    return preferences;
}

void AppSettings::saveUiPreferences(const UiPreferences& preferences) {
    const std::filesystem::path file = filePath();

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    toml::value data = ReadSettings(file);
    data["UI"]["language"] = preferences.language.toStdString();
    data["UI"]["appearance"] = appearanceToString(preferences.appearance).toStdString();

    std::ofstream out(file, std::ios::binary);
    if (out) {
        out << data;
    }
}

QString AppSettings::appearanceToString(Appearance appearance) {
    switch (appearance) {
    case Appearance::Light:
        return QStringLiteral("light");
    case Appearance::Dark:
        return QStringLiteral("dark");
    case Appearance::Auto:
    default:
        return QStringLiteral("auto");
    }
}

Appearance AppSettings::appearanceFromString(const QString& value) {
    if (value == QStringLiteral("light")) {
        return Appearance::Light;
    }
    if (value == QStringLiteral("dark")) {
        return Appearance::Dark;
    }
    return Appearance::Auto;
}

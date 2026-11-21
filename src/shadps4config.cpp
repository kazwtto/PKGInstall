#include "shadps4config.h"
#include "path_utils.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <fstream>
#include <toml.hpp>

namespace {

// Search limits, so a button press cannot walk a whole disk.
constexpr int kScanDepth = 3;
constexpr int kScanBudget = 6000;
constexpr std::uintmax_t kMaxConfigSize = 4 * 1024 * 1024;

std::filesystem::path PathFromQString(const QString& path) {
    return Common::Path::fromQString(path);
}

QString QStringFromPath(const std::filesystem::path& path) {
    return Common::Path::toQString(path);
}

// Paths are only used as lookup keys, never opened, so a simple text form is enough.
std::string Key(const std::filesystem::path& path) {
#ifdef _WIN32
    return Common::Path::toQString(path.lexically_normal()).toLower().toStdString();
#else
    return Common::Path::toUtf8(path.lexically_normal());
#endif
}

bool IsEmpty(const Shadps4Config& config) {
    return config.installDirs.empty() && !config.addonDir.has_value();
}

std::optional<std::filesystem::path> NonEmpty(const QString& path) {
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return PathFromQString(path);
}

// ------------------------------- JSON -----------------------------------
// Current emulator builds write config.json: General.install_dirs is a list of
// { enabled, path } objects and General.addon_install_dir a plain string.

void CollectJsonDirs(const QJsonValue& value, std::vector<std::filesystem::path>& dirs) {
    if (!value.isArray()) {
        return;
    }

    for (const QJsonValue& entry : value.toArray()) {
        if (entry.isString()) {
            if (auto path = NonEmpty(entry.toString())) {
                dirs.push_back(*path);
            }
        } else if (entry.isObject()) {
            const QJsonObject object = entry.toObject();
            if (object.contains(QLatin1String("enabled")) && !object.value(QLatin1String("enabled")).toBool(true)) {
                continue;
            }
            if (auto path = NonEmpty(object.value(QLatin1String("path")).toString())) {
                dirs.push_back(*path);
            }
        }
    }
}

std::optional<Shadps4Config> ReadJson(const QByteArray& bytes) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    Shadps4Config config;

    for (const char* section : {"General", "GUI"}) {
        const QJsonObject object = root.value(QLatin1String(section)).toObject();
        if (object.isEmpty()) {
            continue;
        }

        if (config.installDirs.empty()) {
            for (const char* key : {"install_dirs", "installDirs"}) {
                CollectJsonDirs(object.value(QLatin1String(key)), config.installDirs);
                if (!config.installDirs.empty()) {
                    break;
                }
            }
        }
        if (!config.addonDir.has_value()) {
            for (const char* key : {"addon_install_dir", "addonInstallDir"}) {
                if (auto path = NonEmpty(object.value(QLatin1String(key)).toString())) {
                    config.addonDir = path;
                    break;
                }
            }
        }
    }

    if (IsEmpty(config)) {
        return std::nullopt;
    }
    return config;
}

// ------------------------------- TOML -----------------------------------
// Older builds write config.toml with [GUI] installDirs / addonInstallDir.

void CollectTomlDirs(const toml::value& table, const char* key, std::vector<std::filesystem::path>& dirs) {
    if (!table.contains(key)) {
        return;
    }

    const toml::value& value = table.at(key);
    if (!value.is_array()) {
        return;
    }

    for (const toml::value& entry : value.as_array()) {
        if (entry.is_string()) {
            if (auto path = NonEmpty(QString::fromStdString(entry.as_string()))) {
                dirs.push_back(*path);
            }
        } else if (entry.is_table() && entry.contains("path") && entry.at("path").is_string()) {
            if (auto path = NonEmpty(QString::fromStdString(entry.at("path").as_string()))) {
                dirs.push_back(*path);
            }
        }
    }
}

std::optional<Shadps4Config> ReadToml(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios_base::binary);
    if (!stream) {
        return std::nullopt;
    }

    toml::value data;
    try {
        data = toml::parse(stream, Key(file.filename()));
    } catch (...) {
        return std::nullopt;
    }

    if (!data.is_table()) {
        return std::nullopt;
    }

    Shadps4Config config;
    for (const char* section : {"General", "GUI"}) {
        if (!data.contains(section) || !data.at(section).is_table()) {
            continue;
        }

        const toml::value& table = data.at(section);
        if (config.installDirs.empty()) {
            CollectTomlDirs(table, "install_dirs", config.installDirs);
            if (config.installDirs.empty()) {
                CollectTomlDirs(table, "installDirs", config.installDirs);
            }
        }
        if (!config.addonDir.has_value()) {
            for (const char* key : {"addon_install_dir", "addonInstallDir"}) {
                if (table.contains(key) && table.at(key).is_string()) {
                    if (auto path = NonEmpty(QString::fromStdString(table.at(key).as_string()))) {
                        config.addonDir = path;
                        break;
                    }
                }
            }
        }
    }

    if (IsEmpty(config)) {
        return std::nullopt;
    }
    return config;
}

// ----------------------------- locating ----------------------------------

void AddConfigFiles(const std::filesystem::path& directory, std::vector<std::filesystem::path>& files) {
    for (const char* name : {"config.json", "config.toml"}) {
        files.push_back(directory / name);
    }
}

// Directories that hold the per-user emulator data (next to data written by this application).
std::vector<std::filesystem::path> DataParents() {
    std::vector<std::filesystem::path> parents;

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        parents.push_back(PathFromQString(appData).parent_path());
    }

    for (QStandardPaths::StandardLocation location : {QStandardPaths::GenericConfigLocation,
                                                      QStandardPaths::GenericDataLocation}) {
        const QString directory = QStandardPaths::writableLocation(location);
        if (!directory.isEmpty()) {
            parents.push_back(PathFromQString(directory));
        }
    }

    return parents;
}

// Portable installs end up in the places users unpack downloads into, most likely first.
std::vector<std::filesystem::path> ScanRoots() {
    std::vector<std::filesystem::path> roots;

    const QStandardPaths::StandardLocation locations[] = {
        QStandardPaths::DocumentsLocation,
        QStandardPaths::DownloadLocation,
        QStandardPaths::DesktopLocation,
        QStandardPaths::HomeLocation,
    };

    for (QStandardPaths::StandardLocation location : locations) {
        const QString directory = QStandardPaths::writableLocation(location);
        if (!directory.isEmpty() && std::find(roots.begin(), roots.end(), PathFromQString(directory)) == roots.end()) {
            roots.push_back(PathFromQString(directory));
        }
    }

    return roots;
}

// System and build folders never hold a portable emulator, and walking them is slow.
bool IsUninteresting(const QString& name) {
    if (name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1Char('$'))) {
        return true;
    }

    static const QStringList skipped = {
        QStringLiteral("appdata"),         QStringLiteral("node_modules"),
        QStringLiteral("windows"),          QStringLiteral("program files"),
        QStringLiteral("program files (x86)"), QStringLiteral("programdata"),
        QStringLiteral("temp"),             QStringLiteral("tmp"),
        QStringLiteral("cache"),            QStringLiteral("build"),
        QStringLiteral("builds"),           QStringLiteral("externals"),
        QStringLiteral("onedrive"),
    };
    return skipped.contains(name);
}

void ScanDirectory(const std::filesystem::path& directory, int depth, std::vector<std::filesystem::path>& files,
                   int& budget) {
    if (depth < 0 || budget <= 0) {
        return;
    }

    std::error_code ec;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        if (budget-- <= 0) {
            return;
        }

        const std::filesystem::path path = entry.path();
        const QString name = QStringFromPath(path.filename()).toLower();

        if (entry.is_directory(ec)) {
            if (name.startsWith(QStringLiteral("shadps4"))) {
                // Portable layout: the emulator keeps its data in a "user" folder beside itself.
                AddConfigFiles(path, files);
                AddConfigFiles(path / "user", files);
            }
            if (name != QStringLiteral("user") && !IsUninteresting(name)) {
                ScanDirectory(path, depth - 1, files, budget);
            }
        } else if (name == QStringLiteral("config.json") || name == QStringLiteral("config.toml")) {
            files.push_back(path);
        }
    }
}

} // namespace

std::optional<Shadps4Config> Shadps4ConfigFinder::Read(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::file_size(file, ec) > kMaxConfigSize) {
        return std::nullopt;
    }

    std::optional<Shadps4Config> config;
    if (QStringFromPath(file).endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
        QFile handle(QStringFromPath(file));
        if (!handle.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        config = ReadJson(handle.readAll());
    } else {
        config = ReadToml(file);
    }

    if (config.has_value()) {
        // shadPS4 ships install_dirs with a single empty entry; keeping it would add a
        // blank row to the folder list and hide the "nothing found" message.
        std::vector<std::filesystem::path>& dirs = config->installDirs;
        dirs.erase(std::remove_if(dirs.begin(), dirs.end(),
                                  [](const std::filesystem::path& entry) { return entry.empty(); }),
                   dirs.end());
        if (dirs.empty() && !config->addonDir.has_value()) {
            return std::nullopt;
        }
    }
    return config;
}

std::vector<std::filesystem::path> Shadps4ConfigFinder::Candidates() {
    std::vector<std::filesystem::path> files;

    for (const std::filesystem::path& parent : DataParents()) {
        for (const char* name : {"shadPS4", "shadps4", "ShadPS4"}) {
            const std::filesystem::path directory = parent / name;
            AddConfigFiles(directory, files);
            AddConfigFiles(directory / "user", files);
        }
    }

    // A portable install placed next to this application.
    const std::filesystem::path executableDir = PathFromQString(QCoreApplication::applicationDirPath());
    AddConfigFiles(executableDir / "user", files);
    AddConfigFiles(executableDir.parent_path() / "user", files);

    std::vector<std::filesystem::path> unique;
    std::vector<std::string> seen;
    for (const std::filesystem::path& file : files) {
        const std::string key = Key(file);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        unique.push_back(file);
    }

    return unique;
}

std::optional<std::filesystem::path> Shadps4ConfigFinder::Detect() {
    std::vector<std::filesystem::path> files = Candidates();

    int budget = kScanBudget;
    for (const std::filesystem::path& root : ScanRoots()) {
        ScanDirectory(root, kScanDepth, files, budget);
    }

    // Several installs can coexist; the one edited most recently is the one in use.
    std::optional<std::filesystem::path> best;
    std::filesystem::file_time_type bestTime{};
    std::error_code ec;

    for (const std::filesystem::path& file : files) {
        const std::optional<Shadps4Config> config = Read(file);
        if (!config.has_value() || config->installDirs.empty()) {
            continue;
        }

        const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, ec);
        if (!best.has_value() || (!ec && time > bestTime)) {
            best = file;
            if (!ec) {
                bestTime = time;
            }
        }
    }

    return best;
}

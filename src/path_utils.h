#pragma once

#include <QString>
#include <filesystem>
#include <string>
#include <string_view>

namespace Common::Path {

inline std::filesystem::path fromQString(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

inline QString toQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

inline std::filesystem::path fromUtf8(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const std::u8string u8str(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(u8str);
}

inline std::string toUtf8(const std::filesystem::path& path) {
    const auto u8_string = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8_string.data()), u8_string.size());
}

inline bool isSubPath(const std::filesystem::path& candidate,
                      const std::filesystem::path& root) {
    const auto normRoot = root.lexically_normal();
    const auto normCandidate = candidate.lexically_normal();

    auto rootIt = normRoot.begin();
    auto candIt = normCandidate.begin();
    for (; rootIt != normRoot.end(); ++rootIt, ++candIt) {
        if (rootIt->empty() || *rootIt == ".") {
            continue;
        }
        if (candIt == normCandidate.end() || *rootIt != *candIt) {
            return false;
        }
    }
    for (; candIt != normCandidate.end(); ++candIt) {
        if (*candIt == "..") {
            return false;
        }
    }
    return true;
}

} // namespace Common::Path

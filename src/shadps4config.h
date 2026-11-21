#pragma once

#include <filesystem>
#include <optional>
#include <vector>

// Folders the emulator is configured to install games and add-ons into.
struct Shadps4Config {
    std::vector<std::filesystem::path> installDirs;
    std::optional<std::filesystem::path> addonDir;
};

// Finds the emulator configuration without the user having to point at it:
// the known per-user locations are checked first, then the usual portable
// install folders are searched for a config.json/config.toml that lists
// install folders.
namespace Shadps4ConfigFinder {

// Reads a config.json (current emulator format) or config.toml (older builds).
std::optional<Shadps4Config> Read(const std::filesystem::path& file);

// Configuration file holding install folders, or nullopt when there is none.
std::optional<std::filesystem::path> Detect();

// Configuration paths in the known per-user locations (no folder search).
std::vector<std::filesystem::path> Candidates();

} // namespace Shadps4ConfigFinder

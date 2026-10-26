#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Libs::Graphics::Vr {

struct RuntimeSearchPaths {
    std::filesystem::path explicit_dll;
    std::filesystem::path registry_file;
    std::vector<std::filesystem::path> runtime_roots;
    std::vector<std::filesystem::path> steam_roots;
};

// Only resolves existing absolute files. Neither function loads a DLL or starts SteamVR.
bool LocateOpenVrRuntimeDll(const RuntimeSearchPaths& locations,
                           std::filesystem::path& dll, std::string& error) noexcept;
bool FindOpenVrRuntimeDll(std::filesystem::path& dll, std::string& error) noexcept;

} // namespace Libs::Graphics::Vr

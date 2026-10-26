#include "graphics/vr/openvrRuntimePath.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Libs::Graphics::Vr {
namespace {
bool ExistingDll(const std::filesystem::path& candidate) {
    if (!candidate.is_absolute() || candidate.filename() != "openvr_api.dll") { return false; }
    std::error_code ec;
    return std::filesystem::is_regular_file(candidate, ec) && !ec;
}
#ifdef _WIN32
std::filesystem::path EnvironmentPath(const wchar_t* name) {
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0 || size > 32768) { return {}; }
    std::wstring value(size, L'\0');
    const auto copied = GetEnvironmentVariableW(name, value.data(), size);
    if (copied == 0 || copied >= size) { return {}; }
    value.resize(copied);
    return value;
}
std::filesystem::path RegistryPath(HKEY hive, const wchar_t* key,
                                   const wchar_t* value, DWORD view = 0) {
    DWORD bytes = 0;
    const auto flags = RRF_RT_REG_SZ | view;
    if (RegGetValueW(hive, key, value, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > 65536) { return {}; }
    std::wstring text(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(hive, key, value, flags, nullptr, text.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    text.resize(static_cast<size_t>(std::find(text.begin(), text.end(), L'\0') - text.begin()));
    return text;
}
#endif
} // namespace

bool LocateOpenVrRuntimeDll(const RuntimeSearchPaths& locations,
                           std::filesystem::path& dll, std::string& error) noexcept {
    dll.clear();
    error.clear();
    try {
        // A bad explicit override is an error, not permission to silently select another DLL.
        if (!locations.explicit_dll.empty()) {
            if (!ExistingDll(locations.explicit_dll)) {
                error = "KYTY_OPENVR_DLL must be an absolute path to an existing openvr_api.dll";
                return false;
            }
            dll = locations.explicit_dll.lexically_normal();
            return true;
        }
        auto try_root = [&](const std::filesystem::path& root) {
            if (!root.is_absolute()) { return false; }
            const auto candidate = root / "bin" / "win64" / "openvr_api.dll";
            if (!ExistingDll(candidate)) { return false; }
            dll = candidate.lexically_normal();
            return true;
        };
        // Valve's path registry also covers Steam libraries installed on another drive.
        std::error_code ec;
        if (locations.registry_file.is_absolute()) {
            const auto bytes = std::filesystem::file_size(locations.registry_file, ec);
            if (!ec && bytes <= 65536) {
                std::ifstream stream(locations.registry_file, std::ios::binary);
                const auto paths = nlohmann::json::parse(stream, nullptr, false);
                if (paths.is_object()) {
                    const auto roots = paths.find("runtime");
                    if (roots != paths.end() && roots->is_array()) {
                        for (const auto& root : *roots) {
                            if (root.is_string()) {
                                const auto utf8 = root.get<std::string>();
                                if (utf8.find('\0') == std::string::npos &&
                                    try_root(std::filesystem::u8path(utf8))) { return true; }
                            }
                        }
                    }
                }
            }
        }
        for (const auto& root : locations.runtime_roots) {
            if (try_root(root)) { return true; }
        }
        for (const auto& root : locations.steam_roots) {
            if (root.is_absolute() && try_root(root / "steamapps" / "common" / "SteamVR")) {
                return true;
            }
        }
        error = "SteamVR openvr_api.dll was not found. Install and launch SteamVR once, or set "
                "KYTY_OPENVR_DLL to its absolute bin/win64/openvr_api.dll path";
    } catch (const std::exception&) {
        error = "Could not read the SteamVR installation paths; set KYTY_OPENVR_DLL to an "
                "absolute path to the installed openvr_api.dll";
    }
    return false;
}

bool FindOpenVrRuntimeDll(std::filesystem::path& dll, std::string& error) noexcept {
#ifdef _WIN32
    try {
        RuntimeSearchPaths paths;
        paths.explicit_dll = EnvironmentPath(L"KYTY_OPENVR_DLL");
        const auto local = EnvironmentPath(L"LOCALAPPDATA");
        if (local.is_absolute()) { paths.registry_file = local / "openvr" / "openvrpaths.vrpath"; }
        constexpr auto uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 250820";
        paths.runtime_roots = {
            RegistryPath(HKEY_LOCAL_MACHINE, uninstall, L"InstallLocation", RRF_SUBKEY_WOW6464KEY),
            RegistryPath(HKEY_LOCAL_MACHINE, uninstall, L"InstallLocation", RRF_SUBKEY_WOW6432KEY)};
        paths.steam_roots = {
            RegistryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
            RegistryPath(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath", RRF_SUBKEY_WOW6432KEY)};
        for (const auto* name : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
            const auto directory = EnvironmentPath(name);
            if (directory.is_absolute()) { paths.steam_roots.push_back(directory / "Steam"); }
        }
        return LocateOpenVrRuntimeDll(paths, dll, error);
    } catch (const std::exception&) {
        dll.clear();
        error = "Could not inspect the SteamVR installation; set KYTY_OPENVR_DLL explicitly";
        return false;
    }
#else
    dll.clear();
    error = "This SteamVR bridge currently requires Windows";
    return false;
#endif
}

} // namespace Libs::Graphics::Vr

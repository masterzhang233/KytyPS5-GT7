#include "graphics/vr/openvrRuntimePath.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace Libs::Graphics::Vr;
namespace fs = std::filesystem;
#define CHECK(value) do { if (!(value)) { throw std::runtime_error(#value); } } while (false)
static std::string Utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}

struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("kyty-openvr-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { fs::create_directories(root); }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    fs::path Runtime(const fs::path& name) {
        const auto directory = root / name;
        const auto dll = directory / "bin" / "win64" / "openvr_api.dll";
        fs::create_directories(dll.parent_path());
        std::ofstream(dll).put('x'); // The locator never loads this fixture file.
        return directory;
    }
    fs::path Registry(const nlohmann::json& paths) {
        const auto registry = root / "openvrpaths.vrpath";
        std::ofstream(registry) << paths.dump();
        return registry;
    }
};

int main() try {
    Fixture fixture;
    const auto registered = fixture.Runtime(fs::u8path("runtime-\xc3\xa9"));
    const auto fallback = fixture.Runtime("Steam/steamapps/common/SteamVR");
    RuntimeSearchPaths paths;
    paths.registry_file = fixture.Registry({{"runtime", nlohmann::json::array({Utf8(registered)})}});
    paths.steam_roots = {fixture.root / "Steam"};
    fs::path dll;
    std::string error;
    CHECK(LocateOpenVrRuntimeDll(paths, dll, error));
    CHECK(dll == registered / "bin/win64/openvr_api.dll" && error.empty());
    std::puts("PASS registered_runtime_and_utf8");
    paths.explicit_dll = fallback / "bin/win64/openvr_api.dll";
    CHECK(LocateOpenVrRuntimeDll(paths, dll, error) && dll == paths.explicit_dll);
    std::puts("PASS explicit_override_priority");
    paths.explicit_dll = "openvr_api.dll";
    CHECK(!LocateOpenVrRuntimeDll(paths, dll, error) && dll.empty() && !error.empty());
    paths.explicit_dll = fixture.root / "missing/openvr_api.dll";
    CHECK(!LocateOpenVrRuntimeDll(paths, dll, error));
    paths.explicit_dll = registered / "bin/win64";
    CHECK(!LocateOpenVrRuntimeDll(paths, dll, error));
    std::puts("PASS invalid_explicit_override_never_silently_falls_back");
    paths.explicit_dll.clear();
    std::ofstream(paths.registry_file) << "{not json";
    CHECK(LocateOpenVrRuntimeDll(paths, dll, error) && dll == fallback / "bin/win64/openvr_api.dll");
    std::puts("PASS malformed_registry_uses_steam_fallback");
    paths.registry_file = fixture.Registry({{"runtime", nlohmann::json::array({"relative", 42, Utf8(fixture.root / "missing")})}});
    paths.runtime_roots = {registered};
    CHECK(LocateOpenVrRuntimeDll(paths, dll, error) && dll == registered / "bin/win64/openvr_api.dll");
    std::puts("PASS invalid_registry_entries_skipped");
    paths.runtime_roots.clear();
    paths.steam_roots = {"relative/Steam"};
    CHECK(!LocateOpenVrRuntimeDll(paths, dll, error) && dll.empty() && !error.empty());
    std::puts("PASS no_cwd_search_and_clear_failure");
    std::ofstream(paths.registry_file) << std::string(65537, ' ');
    CHECK(!LocateOpenVrRuntimeDll(paths, dll, error));
    std::puts("PASS registry_size_bound");
    return 0;
} catch (const std::exception& exception) {
    std::fprintf(stderr, "FAIL %s\n", exception.what());
    return 1;
}

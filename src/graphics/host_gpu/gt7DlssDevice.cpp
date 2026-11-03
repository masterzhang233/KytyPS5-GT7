#include "graphics/host_gpu/gt7DlssDevice.h"
#include "graphics/host_gpu/graphicContext.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace Libs::Graphics::Gt7DlssHost {
namespace {
struct Backend {
    KytyDlssApi api {};
    KytyDlssPaths paths {sizeof(KytyDlssPaths), nullptr, nullptr};
    std::wstring runtime, logs;
    KytyDlssExtensions instance_extensions {sizeof(KytyDlssExtensions)};
    bool loaded = false, instance_ready = false;
    Backend() {
        if (!Requested()) return;
#if defined(_WIN32)
        try {
            const auto* dll_env = _wgetenv(L"KYTY_GT7_DLSS_BACKEND");
            const auto* runtime_env = _wgetenv(L"KYTY_GT7_DLSS_RUNTIME");
            if (!dll_env || !runtime_env || !std::filesystem::path(dll_env).is_absolute() ||
                !std::filesystem::path(runtime_env).is_absolute()) {
                LOGF("GT7 DLSS disabled: backend and runtime need explicit absolute paths\n");
                return;
            }
            runtime = runtime_env;
            logs = (std::filesystem::current_path() / L"ngx-logs").wstring();
            paths.runtime_directory = runtime.c_str();
            paths.log_directory = logs.c_str();
            // The process owns this module for its lifetime; no dangling API pointers during shutdown.
            const auto module = LoadLibraryExW(dll_env, nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!module) {
                LOGF("GT7 DLSS disabled: backend load failed (%lu)\n", GetLastError());
                return;
            }
            const auto get = reinterpret_cast<PFN_KytyDlssGetApi>(GetProcAddress(module, "KytyDlssGetApi"));
            api.struct_size = sizeof(api);
            if (!get || get(KYTY_DLSS_ABI_VERSION, &api) != KYTY_DLSS_OK ||
                api.struct_size != sizeof(api) || api.abi_version != KYTY_DLSS_ABI_VERSION || !api.query_instance_extensions ||
                !api.query_device_extensions || !api.create_context || !api.record_evaluate ||
                !api.destroy_context || !api.query_optimal) {
                LOGF("GT7 DLSS disabled: incompatible backend ABI\n");
                FreeLibrary(module);
                return;
            }
            loaded = true;
        } catch (const std::exception& error) {
            LOGF("GT7 DLSS disabled: %s\n", error.what());
        }
#else
        LOGF("GT7 DLSS disabled: experimental backend requires Windows\n");
#endif
    }
};
Backend& State() { static Backend state; return state; }
bool Has(const std::vector<vk::ExtensionProperties>& available, const char* name) {
    return std::any_of(available.begin(), available.end(), [&](const auto& entry) {
        return std::strcmp(entry.extensionName, name) == 0;
    });
}
void Add(std::vector<const char*>& required, const char* name) {
    if (std::none_of(required.begin(), required.end(), [&](const auto* value) {
        return std::strcmp(value, name) == 0;
    })) required.push_back(name);
}
bool Valid(const KytyDlssExtensions& extensions) {
    if (extensions.struct_size != sizeof(extensions) || extensions.count > KYTY_DLSS_EXTENSION_CAPACITY) return false;
    for (uint32_t i = 0; i < extensions.count; ++i)
        if (!extensions.names[i][0] || !std::memchr(extensions.names[i], 0, VK_MAX_EXTENSION_NAME_SIZE)) return false;
    return true;
}
} // namespace
bool Requested() {
    const auto* value = std::getenv("KYTY_GT7_DLSS");
    return value && std::strcmp(value, "1") == 0 && !Config::VrEnabled();
}
const KytyDlssApi* Api() { return State().loaded ? &State().api : nullptr; }
const KytyDlssPaths& Paths() { return State().paths; }
void AddInstanceExtensions(std::vector<const char*>& required,
                           const std::vector<vk::ExtensionProperties>& available) {
    auto& state = State();
    if (!state.loaded) return;
    KytyDlssError error {};
    auto& extensions = state.instance_extensions;
    if (state.api.query_instance_extensions(&state.paths, &extensions, &error) != KYTY_DLSS_OK ||
        !Valid(extensions)) {
        LOGF("GT7 DLSS disabled: instance extension query: %s\n", error.message);
        return;
    }
    for (uint32_t i = 0; i < extensions.count; ++i) {
        if (!Has(available, extensions.names[i])) {
            LOGF("GT7 DLSS disabled: missing instance extension %s\n", extensions.names[i]);
            return;
        }
    }
    for (uint32_t i = 0; i < extensions.count; ++i) Add(required, extensions.names[i]);
    state.instance_ready = true;
}
void AddDeviceExtensions(GraphicContext& graphics, std::vector<const char*>& required) {
    auto& state = State();
    if (!state.instance_ready || graphics.physical_device.getProperties().vendorID != 0x10de) return;
    KytyDlssExtensions extensions {sizeof(KytyDlssExtensions)};
    KytyDlssError error {};
    if (state.api.query_device_extensions(&state.paths, graphics.instance, graphics.physical_device,
        &extensions, &error) != KYTY_DLSS_OK || !Valid(extensions)) {
        LOGF("GT7 DLSS disabled: device extension query: %s\n", error.message);
        return;
    }
    const auto available = graphics.physical_device.enumerateDeviceExtensionProperties();
    if (available.result != vk::Result::eSuccess) return;
    for (uint32_t i = 0; i < extensions.count; ++i) {
        if (!Has(available.value, extensions.names[i])) {
            LOGF("GT7 DLSS disabled: missing device extension %s\n", extensions.names[i]);
            return;
        }
    }
    graphics.gt7_dlss_device_extensions.reserve(extensions.count);
    for (uint32_t i = 0; i < extensions.count; ++i)
        graphics.gt7_dlss_device_extensions.emplace_back(extensions.names[i]);
    for (const auto& name : graphics.gt7_dlss_device_extensions) Add(required, name.c_str());
    graphics.gt7_dlss_device_enabled = true;
    LOGF("GT7 DLSS: experimental device extensions prepared; rendering remains conditional\n");
}
} // namespace Libs::Graphics::Gt7DlssHost

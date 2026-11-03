#include "kyty_dlss_backend.h"
#include <windows.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr char ProjectId[] = "9410e655-dcb1-4bcc-aa6e-90584f4a3877";
constexpr char EngineVersion[] = "Kyty-GT7-DLSS-Backend-1";
std::mutex global_mutex;
bool context_active = false; // NGX library lifecycle is process-global in this first ABI.
struct Failure : std::runtime_error {
    KytyDlssStatus status;
    uint32_t ngx;
    Failure(KytyDlssStatus s, const std::string& text, uint32_t n = 0): std::runtime_error(text), status(s), ngx(n) {}
};
void Require(bool condition, const char* message) {
    if (!condition) throw Failure(KYTY_DLSS_INVALID_ARGUMENT, message);
}
void Ngx(NVSDK_NGX_Result result, const char* operation) {
    if ((static_cast<uint32_t>(result) & 0xfff00000u) == static_cast<uint32_t>(NVSDK_NGX_Result_Fail)) {
        char text[200];
        std::snprintf(text, sizeof(text), "%s failed: 0x%08x", operation, unsigned(result));
        throw Failure(KYTY_DLSS_NGX_FAILED, text, unsigned(result));
    }
}
template<class F> KytyDlssStatus Safe(KytyDlssError* error, F&& f) noexcept {
    if (error) *error = {};
    try { f(); return KYTY_DLSS_OK; }
    catch (const Failure& e) {
        if (error) { error->ngx_result = e.ngx; std::snprintf(error->message, sizeof(error->message), "%s", e.what()); }
        return e.status;
    } catch (const std::exception& e) {
        if (error) std::snprintf(error->message, sizeof(error->message), "%s", e.what());
        return KYTY_DLSS_INTERNAL_ERROR;
    } catch (...) {
        if (error) std::snprintf(error->message, sizeof(error->message), "Unknown backend exception");
        return KYTY_DLSS_INTERNAL_ERROR;
    }
}
struct Discovery {
    std::wstring runtime, logs;
    const wchar_t* paths[1] {};
    NVSDK_NGX_FeatureCommonInfo common {};
    NVSDK_NGX_FeatureDiscoveryInfo info {};
    explicit Discovery(const KytyDlssPaths* input) {
        Require(input && input->struct_size == sizeof(*input), "Paths struct size mismatch");
        Require(input->runtime_directory && input->log_directory, "Missing runtime/log path");
        const std::filesystem::path r(input->runtime_directory), l(input->log_directory);
        Require(r.is_absolute() && l.is_absolute(), "Runtime and log directories must be absolute");
        Require(std::filesystem::is_regular_file(r / L"nvngx_dlss.dll"), "Runtime directory has no nvngx_dlss.dll");
        std::filesystem::create_directories(l);
        runtime = std::filesystem::weakly_canonical(r).wstring();
        logs = std::filesystem::weakly_canonical(l).wstring();
        paths[0] = runtime.c_str(); common.PathListInfo.Path = paths; common.PathListInfo.Length = 1;
        info.SDKVersion = NVSDK_NGX_Version_API;
        info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
        info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        info.Identifier.v.ProjectDesc = {ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion};
        info.ApplicationDataPath = logs.c_str(); info.FeatureInfo = &common;
    }
    void VerifyLoadedRuntime(bool required = false) const {
        const auto module = GetModuleHandleW(L"nvngx_dlss.dll");
        if (!module) {
            Require(!required, "NGX did not load an identifiable nvngx_dlss.dll runtime");
            return;
        }
        std::vector<wchar_t> filename(32768);
        const DWORD length = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
        Require(length && length < filename.size(), "Cannot identify loaded DLSS runtime");
        Require(std::filesystem::equivalent(std::filesystem::path(runtime) / L"nvngx_dlss.dll",
                                           std::filesystem::path(std::wstring(filename.data(), length))),
                "An unexpected DLSS runtime is already loaded in this process");
    }
};
void CopyExtensions(uint32_t count, VkExtensionProperties* required, KytyDlssExtensions* out) {
    Require(out && out->struct_size == sizeof(*out), "Extensions struct size mismatch");
    Require(count <= KYTY_DLSS_EXTENSION_CAPACITY, "Extension list exceeds ABI capacity");
    *out = {}; out->struct_size = sizeof(*out); out->count = count;
    for (uint32_t i=0; i<count; ++i)
        std::snprintf(out->names[i], sizeof(out->names[i]), "%s", required[i].extensionName);
}
NVSDK_NGX_PerfQuality_Value Mode(KytyDlssQuality quality) {
    switch (quality) {
    case KYTY_DLSS_QUALITY: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case KYTY_DLSS_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case KYTY_DLSS_DLAA: return NVSDK_NGX_PerfQuality_Value_DLAA;
    default: throw Failure(KYTY_DLSS_INVALID_ARGUMENT, "Unknown quality mode");
    }
}
const char* PresetKey(KytyDlssQuality q) {
    switch (q) {
    case KYTY_DLSS_QUALITY: return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality;
    case KYTY_DLSS_PERFORMANCE: return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance;
    case KYTY_DLSS_DLAA: return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA;
    default: throw Failure(KYTY_DLSS_INVALID_ARGUMENT, "Unknown quality mode");
    }
}
} // namespace

struct KytyDlssContext {
    std::unique_ptr<Discovery> discovery; // Keep NGX path strings/common-info alive through shutdown.
    VkDevice device {};
    uint32_t width {}, height {}, out_width {}, out_height {};
    KytyDlssQuality quality {};
    NVSDK_NGX_Parameter* capabilities {};
    NVSDK_NGX_Parameter* parameters {};
    NVSDK_NGX_Handle* feature {};
    bool initialized {}, poisoned {};
    uint64_t evaluations {};
    // Destruction is legal only after caller-owned GPU work completes.
    ~KytyDlssContext() {
        if (feature) NVSDK_NGX_VULKAN_ReleaseFeature(feature);
        if (parameters) NVSDK_NGX_VULKAN_DestroyParameters(parameters);
        if (capabilities) NVSDK_NGX_VULKAN_DestroyParameters(capabilities);
        if (initialized) NVSDK_NGX_VULKAN_Shutdown1(device);
    }
};

namespace {
KytyDlssStatus QueryInstance(const KytyDlssPaths* paths, KytyDlssExtensions* out, KytyDlssError* e) {
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Discovery d(paths); d.VerifyLoadedRuntime();
        uint32_t count {}; VkExtensionProperties* required {};
        Ngx(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d.info, &count, &required), "Instance extension discovery");
        CopyExtensions(count, required, out);
    });
}
KytyDlssStatus QueryDevice(const KytyDlssPaths* paths, VkInstance instance, VkPhysicalDevice physical,
                          KytyDlssExtensions* out, KytyDlssError* e) {
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Require(instance && physical, "Device extension discovery requires instance and physical device");
        Discovery d(paths); d.VerifyLoadedRuntime();
        uint32_t count {}; VkExtensionProperties* required {};
        Ngx(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &d.info, &count, &required), "Device extension discovery");
        CopyExtensions(count, required, out);
    });
}
KytyDlssStatus Create(const KytyDlssCreateInfo* i, KytyDlssContext** out, KytyDlssError* e) {
    if (out) *out = nullptr;
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Require(i && i->struct_size == sizeof(*i) && out, "Create struct size or output mismatch");
        if (context_active) throw Failure(KYTY_DLSS_BUSY, "ABI v1 permits one active NGX context per process");
        Require(i->instance && i->physical_device && i->device && i->get_instance_proc_addr && i->get_device_proc_addr,
                "Missing host Vulkan handles or dispatch functions");
        Require(i->input_width >= 64 && i->input_height >= 64 && i->output_width >= i->input_width &&
                i->output_height >= i->input_height && i->output_width <= 16384 && i->output_height <= 16384,
                "Invalid fixed input/output extents");
        Require(i->quality != KYTY_DLSS_DLAA || (i->input_width == i->output_width && i->input_height == i->output_height),
                "DLAA requires equal input/output extents");
        (void)Mode(i->quality);
        Require((i->enabled_feature_flags & 3u) == 3u, "Host must enable bufferDeviceAddress and shaderInt64 before creating its device");
        Require(!i->enabled_device_extension_count || i->enabled_device_extensions, "Missing enabled device extension names");
        auto discovery = std::make_unique<Discovery>(&i->paths);
        auto& d = *discovery;
        d.VerifyLoadedRuntime();
        uint32_t count {}; VkExtensionProperties* required {};
        Ngx(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(i->instance, i->physical_device, &d.info, &count, &required), "Device extension discovery");
        for (uint32_t n=0; n<count; ++n) {
            bool found = false;
            for (uint32_t k=0; k<i->enabled_device_extension_count; ++k)
                found |= i->enabled_device_extensions[k] && std::strcmp(required[n].extensionName, i->enabled_device_extensions[k]) == 0;
            if (!found) throw Failure(KYTY_DLSS_UNSUPPORTED, std::string("Host device did not enable ") + required[n].extensionName);
        }
        const auto get_queues = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            i->get_instance_proc_addr(i->instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
        Require(get_queues != nullptr, "Missing queue-family query");
        uint32_t queues {}; get_queues(i->physical_device, &queues, nullptr);
        std::vector<VkQueueFamilyProperties> properties(queues); get_queues(i->physical_device, &queues, properties.data());
        Require(i->queue_family_index < queues && properties[i->queue_family_index].queueCount &&
                (properties[i->queue_family_index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT), "NGX command buffer needs a graphics+compute queue family");
        NVSDK_NGX_FeatureRequirement requirements {};
        Ngx(NVSDK_NGX_VULKAN_GetFeatureRequirements(i->instance, i->physical_device, &d.info, &requirements), "Feature support query");
        if (requirements.FeatureSupported != 0) throw Failure(KYTY_DLSS_UNSUPPORTED, "DLSS is unsupported on the host physical device");
        auto ctx = std::make_unique<KytyDlssContext>();
        ctx->discovery = std::move(discovery);
        ctx->device=i->device; ctx->width=i->input_width; ctx->height=i->input_height;
        ctx->out_width=i->output_width; ctx->out_height=i->output_height; ctx->quality=i->quality;
        Ngx(NVSDK_NGX_VULKAN_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion,
            d.logs.c_str(), i->instance, i->physical_device, i->device, i->get_instance_proc_addr,
            i->get_device_proc_addr, &d.common), "NGX Init with borrowed host device");
        ctx->initialized = true; d.VerifyLoadedRuntime();
        Ngx(NVSDK_NGX_VULKAN_GetCapabilityParameters(&ctx->capabilities), "Get capabilities");
        d.VerifyLoadedRuntime(true);
        int available {}, driver {};
        Ngx(NVSDK_NGX_Parameter_GetI(ctx->capabilities, NVSDK_NGX_Parameter_SuperSampling_Available, &available), "DLSS availability");
        Ngx(NVSDK_NGX_Parameter_GetI(ctx->capabilities, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &driver), "DLSS driver check");
        if (!available || driver) throw Failure(KYTY_DLSS_UNSUPPORTED, "DLSS unavailable or driver update required");
        Ngx(NVSDK_NGX_VULKAN_AllocateParameters(&ctx->parameters), "Allocate feature parameters");
        NVSDK_NGX_Parameter_SetUI(ctx->parameters, PresetKey(ctx->quality), NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(ctx->capabilities, PresetKey(ctx->quality), NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        *out = ctx.release(); context_active = true;
    });
}
KytyDlssStatus Optimal(KytyDlssContext* c, KytyDlssOptimalSettings* out, KytyDlssError* e) {
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Require(c && out && out->struct_size == sizeof(*out), "Optimal settings context/struct mismatch");
        float sharpness {};
        Ngx(NGX_DLSS_GET_OPTIMAL_SETTINGS(c->capabilities, c->out_width, c->out_height, Mode(c->quality),
            &out->optimal_width, &out->optimal_height, &out->max_width, &out->max_height,
            &out->min_width, &out->min_height, &sharpness), "Optimal settings");
    });
}
void ValidateImage(const KytyDlssImage& i, VkFormat format, uint32_t w, uint32_t h) {
    Require(i.image && i.view && i.format == format && i.width == w && i.height == h,
            "Input/output image has missing handle, incorrect format, or incorrect extent");
}
NVSDK_NGX_Resource_VK Resource(const KytyDlssImage& i, bool writable) {
    const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(i.view, i.image, range, i.format, i.width, i.height, writable);
}
KytyDlssStatus Evaluate(KytyDlssContext* c, const KytyDlssEvaluateInfo* i, KytyDlssRecordResult* out, KytyDlssError* e) {
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Require(c && i && i->struct_size == sizeof(*i) && out && out->struct_size == sizeof(*out), "Evaluate context/struct mismatch");
        *out = {}; out->struct_size = sizeof(*out);
        if (c->poisoned) throw Failure(KYTY_DLSS_POISONED, "Previous NGX recording failed; discard the failed command buffer and recreate context after host GPU completion");
        Require(i->command_buffer, "Missing host recording command buffer");
        Require(std::isfinite(i->jitter_x) && std::isfinite(i->jitter_y) && std::abs(i->jitter_x) <= .5f && std::abs(i->jitter_y) <= .5f,
                "Jitter must be finite input-pixel offsets in [-0.5,0.5]");
        Require(std::isfinite(i->frame_time_ms) && i->frame_time_ms > 0 && i->frame_time_ms <= 1000 &&
                std::isfinite(i->pre_exposure) && i->pre_exposure > 0 && std::isfinite(i->exposure_scale) && i->exposure_scale > 0,
                "Invalid frame time or exposure values");
        ValidateImage(i->color, VK_FORMAT_R16G16B16A16_SFLOAT, c->width, c->height);
        ValidateImage(i->depth, VK_FORMAT_R32_SFLOAT, c->width, c->height);
        ValidateImage(i->motion, VK_FORMAT_R32G32_SFLOAT, c->width, c->height);
        ValidateImage(i->output, VK_FORMAT_R16G16B16A16_SFLOAT, c->out_width, c->out_height);
        Require(i->output.image != i->color.image && i->output.image != i->depth.image && i->output.image != i->motion.image,
                "NGX output must not alias input images");
        try {
            if (!c->feature) {
                NVSDK_NGX_DLSS_Create_Params settings {};
                settings.Feature.InWidth=c->width; settings.Feature.InHeight=c->height;
                settings.Feature.InTargetWidth=c->out_width; settings.Feature.InTargetHeight=c->out_height;
                settings.Feature.InPerfQualityValue=Mode(c->quality);
                settings.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                    NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
                Ngx(NGX_VULKAN_CREATE_DLSS_EXT1(c->device, i->command_buffer, 1, 1, &c->feature, c->parameters, &settings), "Create persistent DLSS feature");
                c->discovery->VerifyLoadedRuntime(true);
                out->feature_created = 1;
            }
            auto color=Resource(i->color, false), depth=Resource(i->depth, false), motion=Resource(i->motion, false), output=Resource(i->output, true);
            NVSDK_NGX_VK_DLSS_Eval_Params p {};
            p.Feature.pInColor=&color; p.Feature.pInOutput=&output; p.pInDepth=&depth; p.pInMotionVectors=&motion;
            p.InRenderSubrectDimensions={c->width,c->height};
            p.InJitterOffsetX=i->jitter_x; p.InJitterOffsetY=i->jitter_y;
            p.InMVScaleX=1; p.InMVScaleY=1;
            p.InReset=c->evaluations == 0 || i->reset_history != 0;
            p.InFrameTimeDeltaInMsec=i->frame_time_ms;
            p.InPreExposure=i->pre_exposure; p.InExposureScale=i->exposure_scale;
            Ngx(NGX_VULKAN_EVALUATE_DLSS_EXT(i->command_buffer, c->feature, c->parameters, &p), "Record DLSS evaluation");
            out->reset_used=p.InReset; out->evaluations_recorded=++c->evaluations;
        } catch (const Failure& failure) {
            c->poisoned=true;
            // INVALID_ARGUMENT is reserved for pre-record validation. In
            // particular, a runtime-path check after CreateFeature must never
            // look like a safe-to-fallback argument error to the host.
            throw Failure(failure.status == KYTY_DLSS_INVALID_ARGUMENT ? KYTY_DLSS_INTERNAL_ERROR : failure.status,
                          failure.what(), failure.ngx);
        } catch (...) { c->poisoned=true; throw; }
    });
}
KytyDlssStatus Destroy(KytyDlssContext* c, uint32_t completed, KytyDlssError* e) {
    return Safe(e, [&] {
        std::lock_guard lock(global_mutex);
        Require(c && completed == 1, "Destroy requires host-confirmed GPU completion (or discarded unsubmitted work)");
        delete c; context_active=false;
    });
}
} // namespace

extern "C" __declspec(dllexport) KytyDlssStatus KytyDlssGetApi(uint32_t version, KytyDlssApi* api) {
    if (version != KYTY_DLSS_ABI_VERSION || !api || api->struct_size != sizeof(*api)) return KYTY_DLSS_INVALID_ARGUMENT;
    *api = {sizeof(*api), KYTY_DLSS_ABI_VERSION, QueryInstance, QueryDevice, Create, Optimal, Evaluate, Destroy};
    return KYTY_DLSS_OK;
}

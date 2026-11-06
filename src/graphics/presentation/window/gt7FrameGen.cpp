#include "graphics/presentation/window/gt7FrameGen.h"

#include "common/assert.h"
#include "common/logging/log.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>

#if defined(_WIN32) && defined(KYTY_GT7_FRAMEGEN_FSR)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>
#include "graphics/presentation/window/gt7FrameGenGuard.h"
// Private encoder settings: capture must not change another image writer's state.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBIWDEF static inline
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#define GT7_HAS_FSR_RUNTIME 1
#endif

namespace Libs::Graphics::Gt7FrameGen {
namespace {

std::once_flag g_initialize;
bool g_requested = false;
std::atomic<bool> g_discontinuity {true};
StatusTracker g_status;

int64_t ClockMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef GT7_HAS_FSR_RUNTIME
constexpr uint32_t OutputCapturePairs = 4;
constexpr uint32_t OutputCaptureBatches = 4;
constexpr uint32_t OutputCaptureMaxWidth = 2560;
constexpr uint32_t OutputCaptureMaxHeight = 1440;
constexpr uint32_t OutputCaptureInputWidth = 1920;
constexpr uint32_t OutputCaptureInputHeight = 1080;
enum CaptureResource : uint32_t { CapturePresent, CaptureGenerated, CaptureHudless, CaptureDepth, CaptureMotion, CaptureResourceCount };
constexpr VkDeviceSize OutputCaptureDepthBytes = VkDeviceSize(OutputCaptureInputWidth) * OutputCaptureInputHeight * 4;
constexpr VkDeviceSize OutputCaptureMotionBytes = OutputCaptureDepthBytes * 2;
constexpr VkDeviceSize OutputCaptureMaxPairBytes =
    VkDeviceSize(OutputCaptureMaxWidth) * OutputCaptureMaxHeight * 4 * 3 + OutputCaptureDepthBytes + OutputCaptureMotionBytes;
constexpr VkDeviceSize OutputCaptureMaxBatchBytes = OutputCaptureMaxPairBytes * OutputCapturePairs;
static_assert(OutputCaptureMaxBatchBytes == 276480000); // 263.671875 MiB; four batches = 1,105,920,000 bytes.
static_assert(std::endian::native == std::endian::little); // Raw Vulkan R32/RG32 texel bytes, no CPU conversion.

// Only the window-thread request flag is shared. All other capture state is
// owned by the present thread, including Generate (synchronous QueuePresent).
std::atomic<bool> g_output_capture_requested {false};
uint32_t g_output_capture_batches = 0; // Process budget survives swapchain recreation.

bool OutputCaptureEnabled() {
    static const bool enabled = [] {
        const char* capture = std::getenv("KYTY_GT7_FSR_FG_CAPTURE_OUTPUTS");
        const char* generation = std::getenv("KYTY_GT7_FSR_FG");
        return capture != nullptr && std::string_view(capture) == "1" &&
               generation != nullptr && std::string_view(generation) == "1";
    }();
    return enabled;
}

bool OcclusionGuardRequested() {
    static const bool enabled = [] {
        const char* guard = std::getenv("KYTY_GT7_FSR_FG_OCCLUSION_GUARD");
        const char* generation = std::getenv("KYTY_GT7_FSR_FG");
        return guard != nullptr && std::string_view(guard) == "1" &&
               generation != nullptr && std::string_view(generation) == "1";
    }();
    return enabled;
}

bool TrackFrameInputs() { return OutputCaptureEnabled() || OcclusionGuardRequested(); }

struct CaptureIdentity {
    uint64_t frame_id = 0;
    uint64_t guest_frame = 0;
    vk::Extent2D render {};
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
    float frame_time_ms = 0.0f;
    bool input_reset = false;
    float near_plane = 0.0f;
    float far_plane = 0.0f;
    float fov_y = 0.0f;
    bool depth_infinite = false;
};

struct CaptureInputResources {
    uint64_t configured_frame_id = 0;
    uint64_t prepared_frame_id = 0;
    FfxApiResource hudless {};
    FfxApiResource depth {};
    FfxApiResource motion {};
    bool hudless_transfer_src = false;
    bool temporal_transfer_src = false;
};

struct OutputCapturePair {
    CaptureIdentity current {};
    uint64_t previous_dispatch_frame_id = 0;
    uint64_t previous_dispatch_guest_frame = 0;
    FfxApiRect2D region {};
    uint32_t present_state = 0;
    uint32_t output_state = 0;
    uint32_t api_format = 0;
    uint32_t transfer_function = 0;
    bool reset = false;
    CaptureInputResources inputs {};
    OcclusionGuard::Result guard {};
};

struct OutputCapture {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    vk::Extent2D extent {};
    vk::Format format = vk::Format::eUndefined;
    VkDeviceSize image_bytes = 0;
    VkDeviceSize image_stride = 0;
    VkDeviceSize pair_bytes = 0;
    std::filesystem::path directory;
    std::array<OutputCapturePair, OutputCapturePairs> pairs {};
    uint32_t batch = 0;
    uint32_t count = 0;
    uint64_t skipped_reset = 0;
    uint64_t skipped_prepare = 0;
    const char* error = nullptr;
    bool active = false;
};

struct State {
    HMODULE module = nullptr;
    ffxFunctions api {};
    GraphicContext* graphics = nullptr;
    bool ready = false;
    bool failed = false;
    bool enabled = false;
    bool infinite_depth = false;
    ffxContext swapchain_context = nullptr;
    ffxContext generation_context = nullptr;
    ffxQueryDescSwapchainReplacementFunctionsVK functions {};
    vk::SwapchainKHR swapchain = nullptr;
    vk::Extent2D display {};
    vk::Extent2D max_render {};
    vk::Format format = vk::Format::eUndefined;
    uint64_t frame_id = 0;
    uint64_t last_guest_frame = 0;
    uint64_t prepared = 0;
    uint64_t rejected = 0;
    std::atomic<uint64_t> generated {0};
    std::atomic<uint64_t> generated_non_reset {0};
    std::atomic<bool> reset {true};
    std::atomic<bool> callback_failed {false};
    std::chrono::steady_clock::time_point last_frame {};
    OutputCapture output_capture {};
    CaptureIdentity capture_identity {};
    CaptureInputResources capture_inputs {};
    uint64_t capture_previous_dispatch_frame_id = 0;
    uint64_t capture_previous_dispatch_guest_frame = 0;
    OcclusionGuard guard {};
    OcclusionGuard::Result guard_result {};
    vk::Image guard_output {};
    uint64_t guard_output_frame_id = 0;
    uint64_t guard_applied = 0, guard_skipped = 0, guard_history_updates = 0;
    bool guard_failed = false;
};
State g_state;

bool Report(uint64_t count) { return count <= 4 || count % 300 == 0; }

bool Ok(ffxReturnCode_t result, const char* operation) {
    if (result == FFX_API_RETURN_OK) return true;
    g_status.Set(StatusReason::RuntimeError);
    std::printf("GT7 FSR FG: %s failed (%u); generation disabled\n", operation,
         static_cast<unsigned>(result));
    return false;
}

void Message(uint32_t type, const wchar_t* message) {
    if (message == nullptr) return;
    char text[2048] {};
    WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text) - 1, nullptr, nullptr);
    LOGF("GT7 FSR FG: runtime message type=%u: %s\n", type, text);
    // Runtime callbacks may repeat an error every frame. Keep Silent-mode
    // diagnostics bounded independently of the ordinary presentation summary.
    static StatusTracker messages;
    if (Log::IsSilent() && messages.TryReport(ClockMilliseconds()))
        std::printf("GT7 FSR FG: runtime message type=%u: %s\n", type, text);
}

FfxApiResource ImageResource(const VulkanImage& image) {
    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = static_cast<VkFormat>(image.format);
    info.extent = {image.extent.width, image.extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = static_cast<VkImageUsageFlags>(image.usage);
    return ffxApiGetResourceVK(static_cast<VkImage>(image.image),
        ffxApiGetImageResourceDescriptionVK(static_cast<VkImage>(image.image), info, 0),
        FFX_API_RESOURCE_STATE_COMPUTE_READ);
}

void WaitForPresentsLocked();

std::string OutputCaptureName(uint32_t pair, uint32_t resource) {
    constexpr std::array<const char*, CaptureResourceCount> suffixes {
        "-present-color.png", "-generated.png", "-hudless.png", "-depth-r32f.bin", "-motion-rg32f.bin"};
    return "pair-" + std::to_string(pair + 1) + suffixes[resource];
}

VkDeviceSize OutputCaptureOffset(const OutputCapture& capture, uint32_t pair, uint32_t resource) {
    const VkDeviceSize offset = resource <= CaptureDepth ? capture.image_stride * resource
        : capture.image_stride * 3 + OutputCaptureDepthBytes;
    return capture.pair_bytes * pair + offset;
}

void WriteOutputCaptureManifest(const char* status, const char* reason,
                                const std::array<bool, OutputCapturePairs * CaptureResourceCount>& saved = {}) {
    const auto& capture = g_state.output_capture;
    nlohmann::json pairs = nlohmann::json::array();
    for (uint32_t i = 0; i < capture.count; ++i) {
        const auto& pair = capture.pairs[i];
        const auto input_layout = [&](uint32_t resource) {
            const bool motion = resource == CaptureMotion;
            const auto& image = motion ? pair.inputs.motion : pair.inputs.depth;
            const auto texel_bytes = motion ? 8u : 4u;
            return nlohmann::json {
                {"raw_file", OutputCaptureName(i, resource)}, {"saved", saved[i * CaptureResourceCount + resource]},
                {"width", image.description.width}, {"height", image.description.height},
                {"channels", motion ? 2 : 1}, {"component_type", "IEEE754_float32"}, {"byte_order", "little"},
                {"texel_bytes", texel_bytes}, {"row_pitch", uint64_t(image.description.width) * texel_bytes},
                {"payload_bytes", motion ? OutputCaptureMotionBytes : OutputCaptureDepthBytes},
                {"file_offset", 0}, {"mip", 0}, {"array_layer", 0}, {"aspect", "color"},
                {"tightly_packed", true}, {"row_order", "increasing Vulkan image y; no vertical flip"},
                {"value_conversion", "none; exact texel bytes copied from the private image passed to SDK Prepare"},
                {"vk_format", uint32_t(motion ? vk::Format::eR32G32Sfloat : vk::Format::eR32Sfloat)},
                {"ffx_surface_format", image.description.format}, {"ffx_resource_state", image.state},
                {"layout_before_and_after_copy", "SHADER_READ_ONLY_OPTIMAL"},
                {"semantic", motion ? "current-to-previous displacement in render pixels, x right / y down, projection jitter removed"
                                     : "device depth; reversed Z, no linearization"}
            };
        };
        pairs.push_back({
            {"index", i + 1}, {"frame_id", pair.current.frame_id},
            {"guest_frame", pair.current.guest_frame}, {"reset", pair.reset},
            {"input_reset", pair.current.input_reset},
            {"previous_dispatch_frame_id", pair.previous_dispatch_frame_id},
            {"previous_dispatch_guest_frame", pair.previous_dispatch_guest_frame},
            {"render_width", pair.current.render.width}, {"render_height", pair.current.render.height},
            {"jitter_x", pair.current.jitter_x}, {"jitter_y", pair.current.jitter_y},
            {"prepare_frame_time_ms", pair.current.frame_time_ms},
            {"generation_rect", {pair.region.left, pair.region.top, pair.region.width, pair.region.height}},
            {"ffx_surface_format", pair.api_format}, {"transfer_function", pair.transfer_function},
            {"present_color_state", pair.present_state}, {"generated_output_state", pair.output_state},
            {"present_color_png", OutputCaptureName(i, CapturePresent)}, {"generated_png", OutputCaptureName(i, CaptureGenerated)},
            {"present_color_saved", saved[i * CaptureResourceCount + CapturePresent]},
            {"generated_saved", saved[i * CaptureResourceCount + CaptureGenerated]},
            {"generated_stage", pair.guard.applied ? "SDK output followed by experimental occlusion guard" : "unmodified SDK output"},
            {"occlusion_guard", {
                {"requested", OcclusionGuardRequested()}, {"applied", pair.guard.applied},
                {"history_updated", pair.guard.history_updated},
                {"previous_history_frame_id", pair.guard.history_frame_id},
                {"previous_history_guest_frame", pair.guard.history_guest_frame},
                {"reason", pair.guard.reason}, {"extra_dilation_render_pixels", 0}
            }},
            {"inputs", {
                {"prepared_frame_id", pair.inputs.prepared_frame_id}, {"configured_frame_id", pair.inputs.configured_frame_id},
                {"phase", "same Generate command buffer, after successful SDK Dispatch; private Prepare inputs and configured HUDLessColor"},
                {"hudless", {{"png_file", OutputCaptureName(i, CaptureHudless)},
                             {"saved", saved[i * CaptureResourceCount + CaptureHudless]},
                             {"width", pair.inputs.hudless.description.width}, {"height", pair.inputs.hudless.description.height},
                             {"vk_format", uint32_t(capture.format)}, {"ffx_surface_format", pair.inputs.hudless.description.format},
                             {"ffx_resource_state", pair.inputs.hudless.state},
                             {"layout_before_and_after_copy", "SHADER_READ_ONLY_OPTIMAL"}}},
                {"depth", input_layout(CaptureDepth)}, {"motion", input_layout(CaptureMotion)},
                {"camera_near", pair.current.near_plane}, {"camera_far", pair.current.far_plane},
                {"camera_fov_y", pair.current.fov_y}, {"depth_inverted", true},
                {"depth_infinite", pair.current.depth_infinite}, {"depth_finite", !pair.current.depth_infinite},
                {"view_space_to_meters_factor", 1.0f}, {"motion_vector_scale", {1.0f, 1.0f}},
                {"motion_vector_units", "render pixels"}, {"motion_vectors_jittered", false},
                {"jitter_offset", {pair.current.jitter_x, pair.current.jitter_y}}, {"jitter_units", "render pixels"}
            }}
        });
    }
    nlohmann::json manifest = {
        {"schema", "kyty.gt7-fg-output-pairs.v2"}, {"status", status}, {"reason", reason},
        {"phase", "after successful frame-generation Dispatch and optional occlusion guard; before SDK UI composition and presentation"},
        {"display_confirmed", false},
        {"pair_semantics", "presentColor is the current real frame; outputs[0] is the interpolated candidate after the per-pair documented optional guard"},
        {"occlusion_guard_requested", OcclusionGuardRequested()},
        {"consecutive_frames_required", false}, {"reset_outputs_skipped", true},
        {"png_conversion", "RGB8; BGRA reordered when necessary, alpha omitted, no resampling, gamma conversion or vertical flip"},
        {"process_id", GetCurrentProcessId()}, {"batch", capture.batch},
        {"max_process_batches", OutputCaptureBatches}, {"requested_pairs", OutputCapturePairs},
        {"files_per_pair", CaptureResourceCount}, {"max_process_data_files", OutputCapturePairs * OutputCaptureBatches * CaptureResourceCount},
        {"max_process_manifest_files", OutputCaptureBatches * 2},
        {"max_batch_readback_bytes", OutputCaptureMaxBatchBytes},
        {"max_process_readback_bytes", OutputCaptureMaxBatchBytes * OutputCaptureBatches},
        {"recorded_pairs", capture.count}, {"partial", capture.count != OutputCapturePairs},
        {"skipped_reset", capture.skipped_reset}, {"skipped_prepare", capture.skipped_prepare},
        {"width", capture.extent.width}, {"height", capture.extent.height},
        {"vk_format", static_cast<uint32_t>(capture.format)},
        {"readback_bytes", capture.pair_bytes * capture.count},
        {"allocated_readback_bytes", capture.pair_bytes * OutputCapturePairs},
        {"pairs", std::move(pairs)}
    };
    const auto path = capture.directory / (std::string_view(status) == "armed" ? "request.json" : "manifest.json");
    const auto temporary = std::filesystem::path(path.wstring() + L".part");
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << manifest.dump(2) << '\n';
    stream.flush();
    if (!stream) throw std::runtime_error("capture manifest write failed");
    stream.close();
    if (stream.fail()) throw std::runtime_error("capture manifest close failed");
    std::filesystem::rename(temporary, path);
}

void ReleaseOutputCapture() {
    auto& capture = g_state.output_capture;
    // Caller has either checked completion or has not recorded any copies.
    if (capture.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(g_state.graphics->allocator, capture.buffer, capture.allocation);
    capture = {};
    g_output_capture_requested.store(false, std::memory_order_release);
}

void BeginOutputCapture(vk::Extent2D render) {
    if (!OutputCaptureEnabled() || g_state.output_capture.active ||
        !g_output_capture_requested.load(std::memory_order_acquire)) return;
    if (g_output_capture_batches >= OutputCaptureBatches) {
        std::printf("GT7 FSR FG capture: rejected; process limit is four batches (16 pairs).\n");
        std::fflush(stdout);
        g_output_capture_requested.store(false, std::memory_order_release);
        return;
    }
    auto& capture = g_state.output_capture;
    capture.active = true;
    capture.batch = ++g_output_capture_batches;
    capture.extent = g_state.display;
    capture.format = g_state.format;
    try {
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        capture.directory = std::filesystem::path("fg-output-captures") /
            (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stamp) +
             "-batch-" + std::to_string(capture.batch));
        std::filesystem::create_directories(capture.directory.parent_path());
        if (!std::filesystem::create_directory(capture.directory))
            throw std::runtime_error("capture directory already exists");
        // Four pairs: three display RGBA8 images plus 1080p R32/RG32 inputs
        // per pair. At most 263.671875 MiB resident, 1054.6875 MiB/process.
        if (capture.extent.width == 0 || capture.extent.height == 0 ||
            capture.extent.width > OutputCaptureMaxWidth || capture.extent.height > OutputCaptureMaxHeight ||
            render != vk::Extent2D {OutputCaptureInputWidth, OutputCaptureInputHeight} ||
            (capture.format != vk::Format::eR8G8B8A8Unorm && capture.format != vk::Format::eB8G8R8A8Unorm)) {
            capture.error = "requires 1920x1080 temporal inputs and SDR RGBA8/BGRA8 UNORM display no larger than 2560x1440";
        } else {
            capture.image_bytes = VkDeviceSize(capture.extent.width) * capture.extent.height * 4;
            // RG32 copy offsets must align to an eight-byte texel, including
            // when a resized display has an odd number of RGBA8 pixels.
            capture.image_stride = (capture.image_bytes + 7) & ~VkDeviceSize(7);
            capture.pair_bytes = capture.image_stride * 3 + OutputCaptureDepthBytes + OutputCaptureMotionBytes;
            vk::BufferCreateInfo buffer {};
            buffer.size = capture.pair_bytes * OutputCapturePairs;
            buffer.usage = vk::BufferUsageFlagBits::eTransferDst;
            VmaAllocationCreateInfo allocation {};
            allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info {};
            const auto raw = static_cast<VkBufferCreateInfo>(buffer);
            if (vmaCreateBuffer(g_state.graphics->allocator, &raw, &allocation, &capture.buffer,
                                &capture.allocation, &info) != VK_SUCCESS || info.pMappedData == nullptr) {
                capture.error = "could not allocate mapped capture buffer";
            } else {
                capture.mapped = info.pMappedData;
            }
        }
        WriteOutputCaptureManifest(capture.error == nullptr ? "armed" : "rejected",
                                   capture.error == nullptr ? "waiting for four non-reset pairs" : capture.error);
        std::printf("GT7 FSR FG capture: batch %u %s (%ux%u).\n", capture.batch,
                    capture.error == nullptr ? "armed" : capture.error, capture.extent.width, capture.extent.height);
        if (capture.error != nullptr) ReleaseOutputCapture();
    } catch (const std::exception& error) {
        std::printf("GT7 FSR FG capture: request failed: %s\n", error.what());
        ReleaseOutputCapture(); // No capture commands have been recorded yet.
    }
    std::fflush(stdout);
}

bool ValidOutputCaptureImage(const FfxApiResource& image, uint32_t expected_state,
                             vk::Extent2D extent, vk::Format format) {
    const auto& description = image.description;
    return image.resource != nullptr && image.state == expected_state &&
        description.type == FFX_API_RESOURCE_TYPE_TEXTURE2D && description.depth == 1 &&
        description.mipCount == 1 && description.width == extent.width &&
        description.height == extent.height &&
        description.format == ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(format));
}

void DisableOcclusionGuard(const char* reason) {
    auto& state = g_state;
    state.guard.InvalidateHistory();
    state.guard_output = nullptr;
    state.guard_output_frame_id = 0;
    if (!state.guard_failed) {
        std::printf("GT7 FSR FG guard: disabled for this swapchain: %s; original SDK output retained.\n", reason);
        std::fflush(stdout);
    }
    state.guard_failed = true;
}

void RecordOcclusionGuard(const ffxDispatchDescFrameGeneration& parameters) {
    auto& state = g_state;
    state.guard_result = {};
    state.guard_result.reason = "not requested";
    if (!OcclusionGuardRequested()) return;
    const auto& inputs = state.capture_inputs;
    const auto& identity = state.capture_identity;
    const auto& rect = parameters.generationRect;
    const bool valid = !state.guard_failed && state.guard.Initialized() &&
        parameters.frameID == identity.frame_id && identity.guest_frame != 0 &&
        inputs.prepared_frame_id == parameters.frameID && inputs.configured_frame_id == parameters.frameID &&
        state.guard_output_frame_id == parameters.frameID &&
        static_cast<VkImage>(state.guard_output) == static_cast<VkImage>(parameters.outputs[0].resource) &&
        state.guard.Matches(identity.render, state.display, state.format) && inputs.temporal_transfer_src &&
        parameters.commandList != nullptr && parameters.numGeneratedFrames == 1 &&
        parameters.backbufferTransferFunction == FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB &&
        rect.left == 0 && rect.top == 0 && rect.width == int32_t(state.display.width) && rect.height == int32_t(state.display.height) &&
        ValidOutputCaptureImage(parameters.presentColor, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, state.display, state.format) &&
        ValidOutputCaptureImage(parameters.outputs[0], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, state.display, state.format) &&
        ValidOutputCaptureImage(inputs.motion, FFX_API_RESOURCE_STATE_COMPUTE_READ, identity.render, vk::Format::eR32G32Sfloat) &&
        inputs.motion.resource != parameters.presentColor.resource && inputs.motion.resource != parameters.outputs[0].resource &&
        parameters.presentColor.resource != parameters.outputs[0].resource;
    if (!valid) {
        state.guard.InvalidateHistory();
        state.guard_result.reason = state.guard_failed ? "guard setup failed" : "callback/input/output identity or resource contract mismatch";
    } else {
        state.guard_result = state.guard.Record(vk::CommandBuffer(static_cast<VkCommandBuffer>(parameters.commandList)),
            vk::Image(static_cast<VkImage>(inputs.motion.resource)),
            vk::Image(static_cast<VkImage>(parameters.presentColor.resource)),
            vk::Image(static_cast<VkImage>(parameters.outputs[0].resource)),
            parameters.frameID, identity.guest_frame, parameters.reset);
    }
    if (state.guard_result.history_updated) ++state.guard_history_updates;
    const auto count = state.guard_result.applied ? ++state.guard_applied : ++state.guard_skipped;
    if (Report(count)) {
        std::printf("GT7 FSR FG guard: frame_id=%llu guest=%llu applied=%u history_updated=%u previous=%llu/%llu totals=%llu/%llu/%llu reason=%s\n",
            static_cast<unsigned long long>(parameters.frameID), static_cast<unsigned long long>(identity.guest_frame),
            state.guard_result.applied ? 1u : 0u, state.guard_result.history_updated ? 1u : 0u,
            static_cast<unsigned long long>(state.guard_result.history_frame_id),
            static_cast<unsigned long long>(state.guard_result.history_guest_frame),
            static_cast<unsigned long long>(state.guard_applied), static_cast<unsigned long long>(state.guard_skipped),
            static_cast<unsigned long long>(state.guard_history_updates), state.guard_result.reason);
        std::fflush(stdout);
    }
}

void RecordOutputCapture(const ffxDispatchDescFrameGeneration& parameters) {
    auto& state = g_state;
    auto& capture = state.output_capture;
    if (!capture.active || capture.error != nullptr || capture.count == OutputCapturePairs) return;
    if (parameters.reset) {
        ++capture.skipped_reset;
        return;
    }
    const auto& inputs = state.capture_inputs;
    const vk::Extent2D input_extent {OutputCaptureInputWidth, OutputCaptureInputHeight};
    if (parameters.frameID != state.capture_identity.frame_id || state.capture_identity.guest_frame == 0 ||
        inputs.prepared_frame_id != parameters.frameID || inputs.configured_frame_id != parameters.frameID ||
        !inputs.hudless_transfer_src || !inputs.temporal_transfer_src || state.capture_identity.render != input_extent ||
        parameters.commandList == nullptr || parameters.numGeneratedFrames != 1 ||
        parameters.backbufferTransferFunction != FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB ||
        !ValidOutputCaptureImage(parameters.presentColor, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, capture.extent, capture.format) ||
        !ValidOutputCaptureImage(parameters.outputs[0], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, capture.extent, capture.format) ||
        !ValidOutputCaptureImage(inputs.hudless, FFX_API_RESOURCE_STATE_COMPUTE_READ, capture.extent, capture.format) ||
        !ValidOutputCaptureImage(inputs.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ, input_extent, vk::Format::eR32Sfloat) ||
        !ValidOutputCaptureImage(inputs.motion, FFX_API_RESOURCE_STATE_COMPUTE_READ, input_extent, vk::Format::eR32G32Sfloat)) {
        capture.error = "callback identity, resource state, format or dimensions did not match capture contract";
        return; // Diagnostic rejection must not alter frame generation.
    }
    const std::array<void*, CaptureResourceCount> images {
        parameters.presentColor.resource, parameters.outputs[0].resource, inputs.hudless.resource,
        inputs.depth.resource, inputs.motion.resource};
    for (size_t i = 0; i < images.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (images[i] == images[j]) {
                capture.error = "capture resources unexpectedly alias one image";
                return;
            }

    // Pinned SDK swapchain images include TRANSFER_SRC. Dispatch unregisters
    // resources back to these input states. The three frame-owned inputs stay
    // alive through the present retirement tick and are explicitly TransferSrc.
    // Restore all states before SDK ownership transfers and UI composition.
    const vk::CommandBuffer command(static_cast<VkCommandBuffer>(parameters.commandList));
    std::array<vk::ImageMemoryBarrier, CaptureResourceCount> barriers {};
    for (uint32_t i = 0; i < barriers.size(); ++i) {
        auto& barrier = barriers[i];
        barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.oldLayout = i == CaptureGenerated ? vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = static_cast<VkImage>(images[i]);
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    }
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
                            {}, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    for (uint32_t i = 0; i < barriers.size(); ++i) {
        vk::BufferImageCopy copy {};
        copy.bufferOffset = OutputCaptureOffset(capture, capture.count, i);
        copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        const auto extent = i <= CaptureHudless ? capture.extent : input_extent;
        copy.imageExtent = vk::Extent3D {extent.width, extent.height, 1};
        command.copyImageToBuffer(barriers[i].image, vk::ImageLayout::eTransferSrcOptimal,
                                  vk::Buffer(capture.buffer), 1, &copy);
        std::swap(barriers[i].oldLayout, barriers[i].newLayout);
        barriers[i].srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barriers[i].dstAccessMask = i == CaptureGenerated ? vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
            : vk::AccessFlagBits::eShaderRead;
    }
    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                            {}, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    vk::BufferMemoryBarrier to_host {};
    to_host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    to_host.dstAccessMask = vk::AccessFlagBits::eHostRead;
    to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.buffer = vk::Buffer(capture.buffer);
    to_host.offset = capture.pair_bytes * capture.count;
    to_host.size = capture.pair_bytes;
    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                            {}, 0, nullptr, 1, &to_host, 0, nullptr);

    auto& pair = capture.pairs[capture.count++];
    pair.current = state.capture_identity;
    pair.previous_dispatch_frame_id = state.capture_previous_dispatch_frame_id;
    pair.previous_dispatch_guest_frame = state.capture_previous_dispatch_guest_frame;
    pair.region = parameters.generationRect;
    pair.present_state = parameters.presentColor.state;
    pair.output_state = parameters.outputs[0].state;
    pair.api_format = parameters.presentColor.description.format;
    pair.transfer_function = parameters.backbufferTransferFunction;
    pair.reset = parameters.reset;
    pair.inputs = inputs;
    pair.guard = state.guard_result;
}

void CapturePngBytes(void* context, void* data, int size) {
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

void FinishOutputCaptureLocked(const char* reason, bool already_drained = false) {
    auto& capture = g_state.output_capture;
    if (!capture.active) return;
    bool readback_valid = true;
    if (capture.count != 0) {
        if (!already_drained) WaitForPresentsLocked();
        // The SDK wrapper discards its internal wait result. Check the device
        // explicitly before reading/releasing captured GPU writes.
        const auto idle = g_state.graphics->device.waitIdle();
        if (idle != vk::Result::eSuccess)
            EXIT("GT7 FSR FG capture: device completion failed (%d); refusing readback or resource release.\n", static_cast<int>(idle));
        if (vmaInvalidateAllocation(g_state.graphics->allocator, capture.allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS) {
            capture.error = "mapped capture invalidate failed";
            readback_valid = false;
        }
    }
    std::array<bool, OutputCapturePairs * CaptureResourceCount> saved {};
    try {
        if (readback_valid && capture.count != 0) {
            // RGB PNG avoids arbitrary game alpha affecting visual comparison.
            std::vector<uint8_t> rgb(static_cast<size_t>(capture.extent.width) * capture.extent.height * 3);
            const bool bgra = capture.format == vk::Format::eB8G8R8A8Unorm;
            for (uint32_t i = 0; i < capture.count * CaptureResourceCount; ++i) {
                const uint32_t pair = i / CaptureResourceCount;
                const uint32_t resource = i % CaptureResourceCount;
                const auto* pixels = static_cast<const uint8_t*>(capture.mapped) + OutputCaptureOffset(capture, pair, resource);
                const auto path = capture.directory / OutputCaptureName(pair, resource);
                const auto temporary = std::filesystem::path(path.wstring() + L".part");
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                bool encoded = true;
                if (resource <= CaptureHudless) {
                    for (size_t pixel = 0; pixel < rgb.size() / 3; ++pixel) {
                        rgb[pixel * 3] = pixels[pixel * 4 + (bgra ? 2 : 0)];
                        rgb[pixel * 3 + 1] = pixels[pixel * 4 + 1];
                        rgb[pixel * 3 + 2] = pixels[pixel * 4 + (bgra ? 0 : 2)];
                    }
                    encoded = stbi_write_png_to_func(CapturePngBytes, &stream,
                        static_cast<int>(capture.extent.width), static_cast<int>(capture.extent.height),
                        3, rgb.data(), static_cast<int>(capture.extent.width * 3)) != 0;
                } else {
                    const auto bytes = resource == CaptureDepth ? OutputCaptureDepthBytes : OutputCaptureMotionBytes;
                    stream.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(bytes));
                }
                stream.flush();
                const bool written = bool(stream);
                stream.close();
                if (!encoded || !written || stream.fail()) throw std::runtime_error("capture PNG/raw write failed");
                std::filesystem::rename(temporary, path);
                saved[i] = true;
            }
        }
    } catch (const std::exception& error) {
        std::printf("GT7 FSR FG capture: image export failed: %s\n", error.what());
        capture.error = "image export failed; inspect per-image saved flags";
    }
    try {
        WriteOutputCaptureManifest(capture.error != nullptr ? "failed" :
                                    capture.count == OutputCapturePairs ? "complete" : "partial",
                                   capture.error != nullptr ? capture.error : reason, saved);
    } catch (const std::exception& error) {
        std::printf("GT7 FSR FG capture: manifest export failed: %s\n", error.what());
        capture.error = "manifest export failed";
    }
    std::printf("GT7 FSR FG capture: batch %u %s, %u/%u pairs, skipped reset=%llu; fg-output-captures.\n",
                capture.batch, capture.error != nullptr ? capture.error : reason,
                capture.count, OutputCapturePairs, static_cast<unsigned long long>(capture.skipped_reset));
    std::fflush(stdout);
    ReleaseOutputCapture();
}

ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* parameters, void* context) {
    auto& state = *static_cast<State*>(context);
    parameters->reset |= state.reset.exchange(false, std::memory_order_relaxed);
    // This integration is deliberately 2x. A different provider must not silently
    // turn it into multi-frame generation or consume more output slots.
    if (parameters->numGeneratedFrames == 0) {
        g_status.Set(StatusReason::WaitingDispatch);
        if (OcclusionGuardRequested()) state.guard.InvalidateHistory();
        return FFX_API_RETURN_OK;
    }
    if (parameters->numGeneratedFrames != 1) {
        g_status.Set(StatusReason::RuntimeError);
        state.callback_failed.store(true, std::memory_order_relaxed);
        LOGF("GT7 FSR FG: rejected generation count=%u (expected one)\n", parameters->numGeneratedFrames);
        EXIT("GT7 FSR FG: the runtime requested an unsupported generation count; stopping before recording.\n");
    }
    const auto result = state.api.Dispatch(&state.generation_context, &parameters->header);
    if (!Ok(result, "generation dispatch")) {
        state.callback_failed.store(true, std::memory_order_relaxed);
        EXIT("GT7 FSR FG: generation recording failed (%u). The command buffer may be partially recorded; stopping the experimental session before submission.\n", static_cast<unsigned>(result));
    } else {
        RecordOcclusionGuard(*parameters);
        if (OutputCaptureEnabled()) {
            RecordOutputCapture(*parameters);
            state.capture_previous_dispatch_frame_id = parameters->frameID;
            state.capture_previous_dispatch_guest_frame = parameters->frameID == state.capture_identity.frame_id
                ? state.capture_identity.guest_frame : 0;
        }
        const auto count = state.generated.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto non_reset_count = !parameters->reset
            ? state.generated_non_reset.fetch_add(1, std::memory_order_relaxed) + 1
            : state.generated_non_reset.load(std::memory_order_relaxed);
        g_status.Generated(parameters->reset, ClockMilliseconds());
        if (Report(count) || (!parameters->reset && Report(non_reset_count))) {
            LOGF("GT7 FSR FG: generation dispatch #%llu succeeded frame_id=%llu reset=%u non_reset_total=%llu\n",
                 static_cast<unsigned long long>(count),
                 static_cast<unsigned long long>(parameters->frameID), parameters->reset ? 1u : 0u,
                 static_cast<unsigned long long>(non_reset_count));
        }
    }
    return result;
}

uint32_t DebugFlags() {
    static const uint32_t flags = [] {
        const auto* value = std::getenv("KYTY_GT7_FSR_FG_DEBUG");
        const int level = value != nullptr ? std::atoi(value) : 0;
        return level >= 2 ? uint32_t(FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW)
             : level == 1 ? uint32_t(FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES |
                                    FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS)
                          : 0u;
    }();
    return flags;
}

bool Configure(bool enabled, uint64_t frame_id, vk::Rect2D region, const VulkanImage* hudless = nullptr) {
    auto& state = g_state;
    if (state.generation_context == nullptr) return !enabled;
    ffxConfigureDescFrameGeneration config {};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = static_cast<VkSwapchainKHR>(state.swapchain);
    config.frameGenerationCallback = Generate;
    config.frameGenerationCallbackUserContext = &state;
    config.frameGenerationEnabled = enabled;
    config.allowAsyncWorkloads = false;
    config.onlyPresentGenerated = false;
    // FSR compares the complete frame against this same-frame scene to retain
    // HUD pixels in generated frames. It consumes the image during QueuePresent,
    // not during Prepare. No UI texture or alpha convention is inferred here.
    if (enabled && hudless != nullptr) config.HUDLessColor = ImageResource(*hudless);
    config.flags = DebugFlags();
    config.generationRect = {region.offset.x, region.offset.y,
                            static_cast<int32_t>(region.extent.width), static_cast<int32_t>(region.extent.height)};
    config.frameID = frame_id;
    if (!Ok(state.api.Configure(&state.generation_context, &config.header), "configure")) {
        state.failed = true;
        state.reset.store(true, std::memory_order_relaxed);
        EXIT("GT7 FSR FG: runtime configuration failed; cannot safely determine interpolation state. Stopping before another present.\n");
    }
    if (TrackFrameInputs()) {
        state.capture_inputs = {};
        if (enabled && hudless != nullptr) {
            state.capture_inputs.configured_frame_id = config.frameID;
            state.capture_inputs.hudless = config.HUDLessColor;
            state.capture_inputs.hudless_transfer_src = bool(hudless->usage & vk::ImageUsageFlagBits::eTransferSrc);
        }
    }
    if (OcclusionGuardRequested()) {
        state.guard_output = nullptr;
        state.guard_output_frame_id = 0;
        if (!enabled) state.guard.InvalidateHistory();
    }
    if (state.enabled != enabled) {
        LOGF("GT7 FSR FG: interpolation %s (2x; DLSS SR remains independent)\n", enabled ? "on" : "off");
        state.reset.store(true, std::memory_order_relaxed);
    }
    state.enabled = enabled;
    return true;
}

bool EnsureGeneration(const FrameInputs& inputs) {
    auto& state = g_state;
    if (state.generation_context != nullptr) {
        // Recreating a context while its earlier presents are queued would free
        // live resources. A changed input contract waits for swapchain recreation.
        return inputs.render_extent.width <= state.max_render.width &&
               inputs.render_extent.height <= state.max_render.height &&
               inputs.depth_infinite == state.infinite_depth;
    }
    ffxCreateBackendVKDesc backend {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = static_cast<VkDevice>(state.graphics->device);
    backend.vkPhysicalDevice = static_cast<VkPhysicalDevice>(state.graphics->physical_device);
    backend.vkDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    ffxCreateContextDescFrameGeneration create {};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.header.pNext = &backend.header;
    create.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    if (inputs.depth_infinite) create.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
    if (std::getenv("KYTY_GT7_FSR_FG_DEBUG") != nullptr) create.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    state.max_render = {std::max(inputs.render_extent.width, state.display.width),
                        std::max(inputs.render_extent.height, state.display.height)};
    create.maxRenderSize = {state.max_render.width, state.max_render.height};
    create.displaySize = {state.display.width, state.display.height};
    create.backBufferFormat = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(state.format));
    if (!Ok(state.api.CreateContext(&state.generation_context, &create.header, nullptr), "context creation")) {
        state.generation_context = nullptr;
        state.failed = true;
        return false;
    }
    state.infinite_depth = inputs.depth_infinite;
    ffxQueryGetProviderVersion version {};
    version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (state.api.Query(&state.generation_context, &version.header) == FFX_API_RETURN_OK && version.versionName != nullptr)
        LOGF("GT7 FSR FG: provider %s\n", version.versionName);
    LOGF("GT7 FSR FG: context ready display=%ux%u render=%ux%u inverted_depth=1 infinite=%u near=%.6f far=%.3f fov=%.6f pose=%u HUD-less extraction=1\n",
         state.display.width, state.display.height, inputs.render_extent.width, inputs.render_extent.height,
         inputs.depth_infinite ? 1u : 0u, inputs.near_plane, inputs.far_plane, inputs.fov_y,
         inputs.camera_pose_valid ? 1u : 0u);
    return true;
}

bool ValidImage(const VulkanImage& image, vk::Extent2D extent, vk::Format format) {
    return image.image != nullptr && image.format == format && image.image_type == vk::ImageType::e2D &&
           image.extent.width == extent.width && image.extent.height == extent.height && image.extent.depth == 1 &&
           image.mip_levels == 1 && image.layers == 1 && image.samples == 1 &&
           bool(image.usage & vk::ImageUsageFlagBits::eSampled) && image.state.layout == vk::ImageLayout::eShaderReadOnlyOptimal;
}

// The SDK's drain calls vkQueueWaitIdle on gameQueue and vkDeviceWaitIdle.
// Its configure/creation/destruction paths can drain too. External queue
// synchronization must cover those calls as well as vkQueuePresentKHR.
void WaitForPresentsLocked() {
    ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait {};
    wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
    if (!Ok(g_state.api.Dispatch(&g_state.swapchain_context, &wait.header), "wait for presents"))
        EXIT("GT7 FSR FG: pending presentation work could not be drained; stopping before freeing its images.\n");
}

const char* InputProblem(const FrameInputs* inputs, vk::Rect2D region, const VulkanImage* hudless,
                         StatusReason& reason) {
    reason = StatusReason::InvalidInput;
    if (inputs == nullptr || !inputs->valid) {
        reason = inputs != nullptr ? inputs->rejection_reason : StatusReason::WaitingInputs;
        return StatusReasonText(reason);
    }
    if (inputs->guest_frame == 0 || inputs->guest_frame == g_state.last_guest_frame) {
        reason = StatusReason::DuplicateFrame;
        return "duplicate frame";
    }
    if (!inputs->camera_valid || !std::isfinite(inputs->near_plane) || inputs->near_plane <= 0.0f ||
        !std::isfinite(inputs->fov_y) || inputs->fov_y <= 0.0f || inputs->fov_y >= 3.14159265f ||
        (!inputs->depth_infinite && (!std::isfinite(inputs->far_plane) || inputs->far_plane <= inputs->near_plane))) {
        reason = StatusReason::CameraUnverified;
        return "unverified or invalid camera";
    }
    if (!std::isfinite(inputs->jitter_x) || !std::isfinite(inputs->jitter_y)) return "invalid jitter";
    if (inputs->render_extent.width == 0 || inputs->render_extent.height == 0 ||
        !ValidImage(inputs->depth, inputs->render_extent, vk::Format::eR32Sfloat) ||
        !ValidImage(inputs->motion, inputs->render_extent, vk::Format::eR32G32Sfloat)) return "invalid depth/motion copy";
    if (!inputs->hudless_valid || inputs->hudless_color.image == nullptr) {
        reason = StatusReason::HudlessUnverified;
        return "no verified HUD-less scene for this frame";
    }
    if (hudless == nullptr || !ValidImage(*hudless, g_state.display, g_state.format)) {
        reason = StatusReason::HudlessUnverified;
        return "HUD-less display image is missing or mismatched";
    }
    // Frame interpolation copies this image into its previous-frame history
    // after sampling it. The Vulkan backend records vkCmdCopyImage directly.
    if (!(hudless->usage & vk::ImageUsageFlagBits::eTransferSrc))
        return "HUD-less display image cannot be copied into interpolation history";
    if (region.offset.x < 0 || region.offset.y < 0 || region.extent.width == 0 || region.extent.height == 0 ||
        uint64_t(region.offset.x) + region.extent.width > g_state.display.width ||
        uint64_t(region.offset.y) + region.extent.height > g_state.display.height) return "invalid presentation region";
    return nullptr;
}
#endif

} // namespace

bool Requested() {
    std::call_once(g_initialize, [] {
        const auto* value = std::getenv("KYTY_GT7_FSR_FG");
        if (value == nullptr || std::string_view(value) != "1") {
            std::printf("GT7 FSR FG: disabled by configuration (KYTY_GT7_FSR_FG is not 1)\n");
            std::fflush(stdout);
            return;
        }
        g_status.Set(StatusReason::Loading);
#ifdef GT7_HAS_FSR_RUNTIME
        wchar_t executable[32768] {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (length == 0 || length >= 32768) {
            g_status.Set(StatusReason::RuntimeMissing);
            std::printf("GT7 FSR FG: executable path unavailable; frame generation off\n");
            std::fflush(stdout);
            return;
        }
        const auto runtime = std::filesystem::path(executable).parent_path() / "amd_fidelityfx_vk.dll";
        auto& state = g_state;
        state.module = LoadLibraryExW(runtime.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (state.module == nullptr) {
            g_status.Set(StatusReason::RuntimeMissing);
            std::printf("GT7 FSR FG: adjacent amd_fidelityfx_vk.dll unavailable (Windows error %lu); DLSS SR unaffected\n", GetLastError());
            std::fflush(stdout);
            return;
        }
        ffxLoadFunctions(&state.api, state.module);
        if (state.api.CreateContext == nullptr || state.api.DestroyContext == nullptr || state.api.Configure == nullptr ||
            state.api.Query == nullptr || state.api.Dispatch == nullptr) {
            g_status.Set(StatusReason::RuntimeMissing);
            std::printf("GT7 FSR FG: runtime missing FidelityFX entry points; frame generation off\n");
            std::fflush(stdout);
            FreeLibrary(state.module);
            state.module = nullptr;
            return;
        }
        ffxConfigureDescGlobalDebug1 debug {};
        debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        debug.fpMessage = Message;
        debug.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_ERRORS;
        (void)state.api.Configure(nullptr, &debug.header);
        g_requested = true;
        g_status.Set(StatusReason::WaitingSwapchain);
        std::printf("GT7 FSR FG: Vulkan runtime loaded, desktop 2x requested; verified HUD-less scene required. Counts below are dispatches, not displayed frames.\n");
#else
        g_status.Set(StatusReason::BuildUnsupported);
        std::printf("GT7 FSR FG: this build has no FidelityFX Vulkan runtime support; DLSS SR unaffected\n");
#endif
        std::fflush(stdout);
    });
    return g_requested;
}

void OnQueues(GraphicContext& graphics) {
#ifdef GT7_HAS_FSR_RUNTIME
    if (!Requested()) return;
    auto& state = g_state;
    state.ready = graphics.queue != nullptr && graphics.gt7_fsr_present_queue != nullptr &&
                  graphics.gt7_fsr_compute_queue != nullptr && graphics.gt7_fsr_acquire_queue != nullptr &&
                  graphics.queue != graphics.gt7_fsr_present_queue &&
                  graphics.gt7_fsr_compute_queue != graphics.gt7_fsr_acquire_queue;
    state.graphics = state.ready ? &graphics : nullptr;
    if (!state.ready) {
        g_status.Set(StatusReason::QueuesUnavailable);
        std::printf("GT7 FSR FG: dedicated queues unavailable; ordinary presentation retained\n");
        std::fflush(stdout);
    }
#else
    (void)graphics;
#endif
}

bool OwnsSwapchain() {
#ifdef GT7_HAS_FSR_RUNTIME
    return g_state.swapchain_context != nullptr && g_state.swapchain != nullptr;
#else
    return false;
#endif
}

bool IsGenerating() {
    return GetStatus().reason == StatusReason::ActiveDispatch;
}

StatusSnapshot GetStatus() {
    (void)Requested();
    return g_status.Read(ClockMilliseconds());
}

void RequestOutputCapture() {
#ifdef GT7_HAS_FSR_RUNTIME
    if (!OutputCaptureEnabled()) return;
    bool expected = false;
    if (!g_output_capture_requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::printf("GT7 FSR FG capture: ignored F10; a request is pending or being captured/exported.\n");
    } else {
        std::printf("GT7 FSR FG capture: F10 requested four non-reset output pairs; export may briefly pause presentation.\n");
    }
    std::fflush(stdout);
#endif
}

vk::Result CreateSwapchain(GraphicContext& graphics, const vk::SwapchainCreateInfoKHR& info, vk::SwapchainKHR* swapchain) {
#ifdef GT7_HAS_FSR_RUNTIME
    auto& state = g_state;
    if (!state.ready || state.graphics != &graphics || OwnsSwapchain() || swapchain == nullptr)
        return vk::Result::eErrorInitializationFailed;
    if (info.imageColorSpace != vk::ColorSpaceKHR::eSrgbNonlinear) {
        g_status.Set(StatusReason::SwapchainUnsupported);
        std::printf("GT7 FSR FG: initial implementation requires SDR presentation; ordinary swapchain retained\n");
        std::fflush(stdout);
        return vk::Result::eErrorInitializationFailed;
    }
    Common::LockGuard queue_lock(graphics.queue_mutex);
    ffxCreateContextDescFrameGenerationSwapChainVK create {};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    create.physicalDevice = static_cast<VkPhysicalDevice>(graphics.physical_device);
    create.device = static_cast<VkDevice>(graphics.device);
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    create.swapchain = &handle;
    create.createInfo = static_cast<VkSwapchainCreateInfoKHR>(info);
    // The caller destroys the previous proxy first. Never pass its opaque handle
    // into a fresh context or the driver's native swapchain destruction path.
    create.createInfo.oldSwapchain = VK_NULL_HANDLE;
    create.gameQueue = {static_cast<VkQueue>(graphics.queue), graphics.queue_family, nullptr};
    create.presentQueue = {static_cast<VkQueue>(graphics.gt7_fsr_present_queue), graphics.queue_family, nullptr};
    create.asyncComputeQueue = {static_cast<VkQueue>(graphics.gt7_fsr_compute_queue), graphics.gt7_fsr_compute_family, nullptr};
    create.imageAcquireQueue = {static_cast<VkQueue>(graphics.gt7_fsr_acquire_queue), graphics.gt7_fsr_compute_family, nullptr};
    if (!Ok(state.api.CreateContext(&state.swapchain_context, &create.header, nullptr), "swapchain creation")) {
        if (state.swapchain_context != nullptr) (void)state.api.DestroyContext(&state.swapchain_context, nullptr);
        state.swapchain_context = nullptr;
        state.ready = false;
        g_status.Set(StatusReason::RuntimeError);
        return vk::Result::eErrorInitializationFailed;
    }
    state.functions = {};
    state.functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
    if (!Ok(state.api.Query(&state.swapchain_context, &state.functions.header), "swapchain functions") ||
        state.functions.pOutGetSwapchainImagesKHR == nullptr || state.functions.pOutAcquireNextImageKHR == nullptr ||
        state.functions.pOutQueuePresentKHR == nullptr || handle == VK_NULL_HANDLE) {
        (void)state.api.DestroyContext(&state.swapchain_context, nullptr);
        state.swapchain_context = nullptr;
        state.ready = false;
        g_status.Set(StatusReason::RuntimeError);
        std::printf("GT7 FSR FG: replacement swapchain functions or handle unavailable; ordinary swapchain retained\n");
        std::fflush(stdout);
        return vk::Result::eErrorInitializationFailed;
    }
    state.swapchain = handle;
    state.display = info.imageExtent;
    state.format = info.imageFormat;
    state.failed = false;
    state.callback_failed.store(false, std::memory_order_relaxed);
    state.reset.store(true, std::memory_order_relaxed);
    state.last_guest_frame = 0;
    state.last_frame = {};
    state.capture_identity = {};
    state.capture_inputs = {};
    state.capture_previous_dispatch_frame_id = 0;
    state.capture_previous_dispatch_guest_frame = 0;
    state.guard_failed = false;
    state.guard_output = nullptr;
    state.guard_output_frame_id = 0;
    state.guard_result = {};
    state.guard_applied = state.guard_skipped = state.guard_history_updates = 0;
    *swapchain = handle;
    g_status.Set(StatusReason::WaitingInputs);
    std::printf("GT7 FSR FG: replacement swapchain ready %ux%u, asynchronous interpolation=0\n", state.display.width, state.display.height);
    return vk::Result::eSuccess;
#else
    (void)graphics; (void)info; (void)swapchain;
    return vk::Result::eErrorInitializationFailed;
#endif
}

void WaitForPresents(GraphicContext& graphics) {
#ifdef GT7_HAS_FSR_RUNTIME
    auto& state = g_state;
    if (!OwnsSwapchain() || state.graphics != &graphics) return;
    Common::LockGuard queue_lock(graphics.queue_mutex);
    WaitForPresentsLocked();
    FinishOutputCaptureLocked("external present drain", true);
#else
    (void)graphics;
#endif
}

void DestroySwapchain(GraphicContext& graphics, vk::SwapchainKHR swapchain) {
#ifdef GT7_HAS_FSR_RUNTIME
    auto& state = g_state;
    if (!OwnsSwapchain() || swapchain != state.swapchain) return;
    Common::LockGuard queue_lock(graphics.queue_mutex);
    WaitForPresentsLocked();
    FinishOutputCaptureLocked("swapchain destruction or resize", true);
    if (OcclusionGuardRequested()) {
        // The SDK wait wrapper does not propagate its internal device-wait result.
        const auto idle = graphics.device.waitIdle();
        if (idle != vk::Result::eSuccess)
            EXIT("GT7 FSR FG guard: device completion failed (%d); refusing resource release.\n", static_cast<int>(idle));
        std::printf("GT7 FSR FG guard: swapchain summary applied=%llu skipped=%llu history_updates=%llu setup_failed=%u\n",
            static_cast<unsigned long long>(state.guard_applied), static_cast<unsigned long long>(state.guard_skipped),
            static_cast<unsigned long long>(state.guard_history_updates), state.guard_failed ? 1u : 0u);
        std::fflush(stdout);
        state.guard.Destroy(); // Destroy borrowed SDK image views before their images.
        state.guard_output = nullptr;
        state.guard_output_frame_id = 0;
    }
    g_output_capture_requested.store(false, std::memory_order_release);
    if (state.generation_context != nullptr) {
        (void)Configure(false, ++state.frame_id, {{0, 0}, state.display});
        (void)Ok(state.api.DestroyContext(&state.generation_context, nullptr), "destroy generation context");
        state.generation_context = nullptr;
    }
    const uint64_t presents = state.functions.pOutGetLastPresentCountFFXAPI != nullptr
        ? state.functions.pOutGetLastPresentCountFFXAPI(static_cast<VkSwapchainKHR>(state.swapchain)) : 0;
    std::printf("GT7 FSR FG: swapchain summary prepared=%llu generation_dispatches=%llu non_reset_dispatches=%llu rejected=%llu sdk_present_submissions=%llu sdk_counter_available=%u (not measured display frames)\n",
         static_cast<unsigned long long>(state.prepared),
         static_cast<unsigned long long>(state.generated.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(state.generated_non_reset.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(state.rejected), static_cast<unsigned long long>(presents),
         state.functions.pOutGetLastPresentCountFFXAPI != nullptr ? 1u : 0u);
    (void)Ok(state.api.DestroyContext(&state.swapchain_context, nullptr), "destroy swapchain context");
    state.swapchain_context = nullptr;
    state.swapchain = nullptr;
    state.functions = {};
    state.enabled = false;
    g_status.Set(StatusReason::WaitingSwapchain);
    state.frame_id = 0;
    state.last_guest_frame = 0;
    state.capture_identity = {};
    state.capture_inputs = {};
    state.capture_previous_dispatch_frame_id = 0;
    state.capture_previous_dispatch_guest_frame = 0;
    state.prepared = 0;
    state.rejected = 0;
    state.generated.store(0, std::memory_order_relaxed);
    state.generated_non_reset.store(0, std::memory_order_relaxed);
#else
    (void)graphics; (void)swapchain;
#endif
}

vk::Result GetSwapchainImages(vk::Device device, vk::SwapchainKHR swapchain, uint32_t* count, vk::Image* images) {
#ifdef GT7_HAS_FSR_RUNTIME
    if (!OwnsSwapchain()) return vk::Result::eErrorInitializationFailed;
    const auto result = static_cast<vk::Result>(g_state.functions.pOutGetSwapchainImagesKHR(static_cast<VkDevice>(device),
        static_cast<VkSwapchainKHR>(swapchain), count, reinterpret_cast<VkImage*>(images)));
    if (OcclusionGuardRequested() && !g_state.guard_failed && images != nullptr && count != nullptr &&
        (result == vk::Result::eSuccess || result == vk::Result::eIncomplete)) {
        for (uint32_t i = 0; i < *count; ++i) {
            if (!g_state.guard.RegisterPresent(*g_state.graphics, images[i], g_state.display, g_state.format)) {
                DisableOcclusionGuard("present image registration failed");
                break;
            }
        }
    }
    return result;
#else
    (void)device; (void)swapchain; (void)count; (void)images;
    return vk::Result::eErrorInitializationFailed;
#endif
}

vk::Result AcquireNextImage(vk::Device device, vk::SwapchainKHR swapchain, uint64_t timeout, vk::Semaphore semaphore, uint32_t* index) {
#ifdef GT7_HAS_FSR_RUNTIME
    if (!OwnsSwapchain()) return vk::Result::eErrorInitializationFailed;
    return static_cast<vk::Result>(g_state.functions.pOutAcquireNextImageKHR(static_cast<VkDevice>(device),
        static_cast<VkSwapchainKHR>(swapchain), timeout, static_cast<VkSemaphore>(semaphore), VK_NULL_HANDLE, index));
#else
    (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)index;
    return vk::Result::eErrorInitializationFailed;
#endif
}

vk::Result QueuePresent(vk::Queue queue, const vk::PresentInfoKHR& present) {
#ifdef GT7_HAS_FSR_RUNTIME
    if (!OwnsSwapchain()) return vk::Result::eErrorInitializationFailed;
    const VkPresentInfoKHR& native = present;
    const auto result = static_cast<vk::Result>(g_state.functions.pOutQueuePresentKHR(static_cast<VkQueue>(queue), &native));
    const auto now = ClockMilliseconds();
    if (g_status.TryReport(now)) {
        const auto status = g_status.Read(now);
        std::printf("GT7 FSR FG status: %s; non_reset_dispatches=%llu prepared=%llu rejected=%llu present_result=%d\n",
            StatusReasonText(status.reason), static_cast<unsigned long long>(status.generated_dispatches),
            static_cast<unsigned long long>(g_state.prepared), static_cast<unsigned long long>(g_state.rejected),
            static_cast<int>(result));
        // A five-second summary is useful while redirected into a launcher log.
        // No per-frame flush is introduced.
        std::fflush(stdout);
    }
    return result;
#else
    (void)queue; (void)present;
    return vk::Result::eErrorInitializationFailed;
#endif
}

void RecordPrepare(GraphicContext& graphics, vk::CommandBuffer command, const FrameInputs* inputs,
                   vk::Rect2D region, const VulkanImage* hudless) {
#ifdef GT7_HAS_FSR_RUNTIME
    auto& state = g_state;
    if (!OwnsSwapchain() || state.graphics != &graphics) return;
    Common::LockGuard queue_lock(graphics.queue_mutex);
    // Copies belong to earlier QueuePresent submissions. This runs before this
    // frame's Prepare dispatch, never inside Generate or while waiting for a
    // command buffer which is still being recorded. Temporary FG fallback can
    // leave a partial batch alive; explicit final drains save it as partial.
    if (state.output_capture.active &&
        (state.output_capture.count == OutputCapturePairs || state.output_capture.error != nullptr))
        FinishOutputCaptureLocked("four non-reset pairs recorded");
    const uint64_t frame_id = ++state.frame_id;
    StatusReason reason;
    const char* problem = InputProblem(inputs, region, hudless, reason);
    if (state.callback_failed.load(std::memory_order_relaxed)) state.failed = true;
    if (problem != nullptr || state.failed) {
        g_status.Set(state.failed ? StatusReason::RuntimeError : reason);
        if (state.output_capture.active) ++state.output_capture.skipped_prepare;
        state.reset.store(true, std::memory_order_relaxed);
        if (problem != nullptr && Report(++state.rejected))
            LOGF("GT7 FSR FG: original frame retained #%llu: %s\n", static_cast<unsigned long long>(state.rejected), problem);
        (void)Configure(false, frame_id, region);
        return;
    }
    if (!EnsureGeneration(*inputs)) {
        g_status.Set(state.failed ? StatusReason::RuntimeError : StatusReason::InvalidInput);
        if (state.output_capture.active) ++state.output_capture.skipped_prepare;
        state.reset.store(true, std::memory_order_relaxed);
        (void)Configure(false, frame_id, region);
        return;
    }
    BeginOutputCapture(inputs->render_extent);
    // Consume the discontinuity even when the SR input already requests reset;
    // short-circuiting here would reset an additional, otherwise continuous frame.
    const bool discontinuity = g_discontinuity.exchange(false, std::memory_order_relaxed);
    if (inputs->reset || discontinuity ||
        state.last_guest_frame == 0 || inputs->guest_frame != state.last_guest_frame + 1) {
        state.reset.store(true, std::memory_order_relaxed);
        g_status.Set(StatusReason::HistoryReset);
    }
    state.last_guest_frame = inputs->guest_frame;
    const auto now = std::chrono::steady_clock::now();
    float delta = inputs->frame_time_ms;
    if (!std::isfinite(delta) || delta <= 0.0f) {
        delta = state.last_frame == std::chrono::steady_clock::time_point {} ? 16.666667f
            : std::chrono::duration<float, std::milli>(now - state.last_frame).count();
    }
    state.last_frame = now;
    if (delta > 100.0f) {
        state.reset.store(true, std::memory_order_relaxed);
        g_status.Set(StatusReason::HistoryReset);
    }
    delta = std::clamp(delta, 1.0f, 100.0f);
    if (!Configure(true, frame_id, region, hudless)) return;

    ffxDispatchDescFrameGenerationPrepareCameraInfo camera {};
    camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    for (uint32_t i = 0; i < 3; ++i) {
        camera.cameraPosition[i] = inputs->camera_position[i];
        camera.cameraUp[i] = inputs->camera_up[i];
        camera.cameraRight[i] = inputs->camera_right[i];
        camera.cameraForward[i] = inputs->camera_forward[i];
    }
    ffxDispatchDescFrameGenerationPrepare prepare {};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
    prepare.header.pNext = inputs->camera_pose_valid ? &camera.header : nullptr;
    prepare.frameID = frame_id;
    prepare.flags = DebugFlags();
    prepare.commandList = static_cast<VkCommandBuffer>(command);
    prepare.renderSize = {inputs->render_extent.width, inputs->render_extent.height};
    prepare.jitterOffset = {inputs->jitter_x, inputs->jitter_y};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeDelta = delta;
    prepare.cameraNear = inputs->near_plane;
    prepare.cameraFar = inputs->depth_infinite ? FLT_MAX : inputs->far_plane;
    prepare.cameraFovAngleVertical = inputs->fov_y;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.depth = ImageResource(inputs->depth);
    prepare.motionVectors = ImageResource(inputs->motion);
    if (!Ok(state.api.Dispatch(&state.generation_context, &prepare.header), "prepare")) {
        state.failed = true;
        state.reset.store(true, std::memory_order_relaxed);
        EXIT("GT7 FSR FG: prepare recording failed. The command buffer may be partially recorded; stopping the experimental session before submission.\n");
    }
    if (TrackFrameInputs()) {
        state.capture_identity = {frame_id, inputs->guest_frame, inputs->render_extent,
                                  inputs->jitter_x, inputs->jitter_y, delta, inputs->reset,
                                  prepare.cameraNear, prepare.cameraFar, prepare.cameraFovAngleVertical,
                                  state.infinite_depth};
        // These are precisely the descriptors supplied to Prepare/Configure,
        // not later SR intermediates. Their frame owner lives until the present
        // retirement tick, covering the additional copies in Generate.
        state.capture_inputs.prepared_frame_id = prepare.frameID;
        state.capture_inputs.depth = prepare.depth;
        state.capture_inputs.motion = prepare.motionVectors;
        state.capture_inputs.temporal_transfer_src =
            bool(inputs->depth.usage & vk::ImageUsageFlagBits::eTransferSrc) &&
            bool(inputs->motion.usage & vk::ImageUsageFlagBits::eTransferSrc);
    }
    if (OcclusionGuardRequested() && !state.guard_failed) {
        const bool full_region = region.offset.x == 0 && region.offset.y == 0 && region.extent == state.display;
        if (!full_region || !state.capture_inputs.temporal_transfer_src) {
            state.guard.InvalidateHistory();
        } else if ((!state.guard.Initialized() && !state.guard.Initialize(graphics, inputs->render_extent, state.display, state.format)) ||
                   !state.guard.Matches(inputs->render_extent, state.display, state.format)) {
            DisableOcclusionGuard("guard allocation, pipeline or extent validation failed");
        } else {
            FfxApiResource output {};
            ffxQueryDescFrameGenerationSwapChainInterpolationTextureVK query {};
            query.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONTEXTURE_VK;
            query.pOutTexture = &output;
            if (state.api.Query(&state.swapchain_context, &query.header) != FFX_API_RETURN_OK ||
                !ValidOutputCaptureImage(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, state.display, state.format) ||
                !state.guard.RegisterOutput(vk::Image(static_cast<VkImage>(output.resource)))) {
                DisableOcclusionGuard("interpolation image query or registration failed");
            } else {
                state.guard_output = vk::Image(static_cast<VkImage>(output.resource));
                state.guard_output_frame_id = frame_id;
            }
        }
    }
    if (Report(++state.prepared))
        LOGF("GT7 FSR FG: prepared #%llu frame_id=%llu guest=%llu render=%ux%u output=%ux%u delta_ms=%.3f hudless=1\n",
             static_cast<unsigned long long>(state.prepared), static_cast<unsigned long long>(frame_id),
             static_cast<unsigned long long>(inputs->guest_frame), inputs->render_extent.width, inputs->render_extent.height,
             state.display.width, state.display.height, delta);
#else
    (void)graphics; (void)command; (void)inputs; (void)region; (void)hudless;
#endif
}

void NoteDroppedFrame() {
    g_discontinuity.store(true, std::memory_order_relaxed);
    // Dropped frames can be reported while FG is disabled or failed to load.
    // Preserve the more useful disabled/initialization error state.
    g_status.Dropped();
}

} // namespace Libs::Graphics::Gt7FrameGen

#include "graphics/host_gpu/renderer/gt7Dlss.h"

#include "graphics/host_gpu/gt7DlssDevice.h"
#include "graphics/host_gpu/renderer/colorRenderTarget.h"
#include "graphics/host_gpu/renderer/commandRecorder.h"
#include "graphics/host_gpu/renderer/depthRenderTarget.h"
#include "graphics/host_gpu/renderer/frameGraph.h"
#include "graphics/host_gpu/renderer/gt7FrameGenCamera.h"
#include "graphics/host_gpu/renderer/gt7FrameGenDcc.h"
#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/shader/shader.h"
#include "graphics/presentation/window/gt7FrameGen.h"
#include "kernel/memory.h"
#include "gpu_dlss_shaders/gt7_dlss_prepare_spv.h"
#include "gpu_dlss_shaders/gt7_dlss_composite_spv.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace Libs::Graphics {
namespace {
constexpr uint32_t InputWidth = 1920, InputHeight = 1080;
constexpr uint32_t OutputWidth = 2560, OutputHeight = 1440;
// The game's history allocation and tone-map/UI chain keep their native extent.
// They are independent of the private DLSS reconstruction resolution.
constexpr uint32_t GuestWidth = 3840, GuestHeight = 2160;
constexpr auto SampledLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
constexpr auto ShaderRead = vk::AccessFlagBits2::eShaderRead;
constexpr auto ShaderWrite = vk::AccessFlagBits2::eShaderWrite;

bool Report(uint64_t n) { return n <= 4 || n % 300 == 0; }

bool Shape(const Image& image, uint32_t width, uint32_t height, vk::Format format) {
    const auto& b = image.backing;
    return b.image != nullptr && b.image_type == vk::ImageType::e2D &&
           b.extent.width == width && b.extent.height == height && b.extent.depth == 1 &&
           b.format == format && b.layers == 1 && b.mip_levels == 1 && b.samples == 1;
}

bool BaseView(const TextureCache::ImageDesc& desc) {
    const auto& view = desc.view_info;
    return view.type == vk::ImageViewType::e2D && view.base_level == 0 &&
           view.base_layer == 0 && view.level_count == 1 && view.layer_count == 1;
}

bool Overlap(GuestRange a, GuestRange b) {
    return a.Valid() && b.Valid() && a.address < b.End() && b.address < a.End();
}

vk::ImageView View(Image& image, bool storage = false) {
    ImageViewInfo view {};
    view.type = vk::ImageViewType::e2D;
    view.format = image.backing.format;
    view.aspect = image.info.IsDepth() ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    view.usage = storage ? vk::ImageUsageFlagBits::eStorage : vk::ImageUsageFlagBits::eSampled;
    return image.FindView(view);
}

// Draw hooks run inside GuestImageUses. These accesses are emulator work and
// must never inherit the guest's lean-barrier assumption.
void Transit(Image& image, vk::ImageLayout layout, vk::AccessFlags2 access, vk::CommandBuffer command) {
    image.internal_use = true;
    image.Transit(layout, access, {}, command);
}

KytyDlssImage BackendImage(Image& image, bool storage = false) {
    return {static_cast<VkImage>(image.backing.image), static_cast<VkImageView>(View(image, storage)),
            static_cast<VkFormat>(image.backing.format), image.backing.extent.width, image.backing.extent.height};
}

struct ComputePass {
    vk::DescriptorSetLayout descriptors;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
};

struct FrameGenerationCamera {
    ImageId depth_id {}, motion_id {};
    Gt7FrameGenCamera::Matrix view_projection {}, view {}, projection {};
    Gt7FrameGenCamera::Parameters parameters {};
    bool valid = false, rejected = false;
    const char* reject_reason = nullptr;
};

// Recording-thread diagnostics only. Keep each frame's last meaningful HUD
// transition without retaining caller-owned strings or allocating per draw.
struct HudlessDiagnostic {
    const char* stage = "no tone-map candidate";
    std::array<char, 192> reason {};
    uint32_t tonemap_calls = 0, capture_attempts = 0;
    bool captured = false;
    uint64_t writer_vs = 0, writer_ps = 0;
    uint32_t writer_mask = 0;
    bool writer_overlay = false;
    size_t writer_targets = 0;

    void Note(const char* where, const char* why) {
        stage = where; // All stage labels below are string literals.
        std::snprintf(reason.data(), reason.size(), "%s", why ? why : "none");
    }
};

// The first operation that invalidates a scene is more useful than a later
// cascade of cancellations. Owned text and fixed storage keep this bounded;
// this state has the same recording-thread lifetime as the scene itself.
struct SceneDiagnostic {
    const char* stage = "none";
    std::array<char, 192> reason {};
    ImageId depth_id {}, motion_id {};
    uint64_t writer_vs = 0;
    uint64_t writer_ps = 0;
    bool scene_writer = false;
    std::array<char, 2048> writer_details {};

    void NoteFirst(const char* where, const char* why, ImageId depth = {},
                   ImageId motion = {}, uint64_t vertex_shader = 0) {
        if (reason[0]) return;
        stage = where; // All stage labels are string literals.
        std::snprintf(reason.data(), reason.size(), "%s", why ? why : "none");
        depth_id = depth;
        motion_id = motion;
        writer_vs = vertex_shader;
    }
};

#include "gt7FrameGenCameraProbe.inl"

enum CameraObservation : size_t {
    CameraCalls, CameraDepth, CameraMotion, CameraShape, CameraLate, CameraLimit,
    CameraAlreadyRejected, CameraUser, CameraAddress, CameraUnmapped, CameraGpuDirty,
    CameraRead, CameraMatrix, CameraChanged, CameraAccepted, CameraObservationCount
};

bool ConfigureFrameGenerationImage(GraphicContext& graphics, VulkanImage& image, vk::Format format,
                                    uint32_t width = InputWidth, uint32_t height = InputHeight) {
    const auto usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                       vk::ImageUsageFlagBits::eSampled;
    if (image.image && image.extent == vk::Extent3D {width, height, 1} &&
        image.format == format && image.image_type == vk::ImageType::e2D &&
        image.layers == 1 && image.mip_levels == 1 && image.samples == 1 &&
        (image.usage & usage) == usage) return true;
    // The presenter has retired all uses of the frame before calling us.
    if (image.image) graphics.DeleteImage(image);
    const auto required = vk::FormatFeatureFlagBits::eTransferDst | vk::FormatFeatureFlagBits::eTransferSrc |
                          vk::FormatFeatureFlagBits::eSampledImage;
    if ((graphics.GetFormatProperties(format).optimalTilingFeatures & required) != required) return false;
    vk::ImageCreateInfo create {};
    create.imageType = vk::ImageType::e2D;
    create.format = format;
    create.extent = vk::Extent3D {width, height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = vk::SampleCountFlagBits::e1;
    create.tiling = vk::ImageTiling::eOptimal;
    create.usage = usage;
    create.sharingMode = vk::SharingMode::eExclusive;
    create.initialLayout = vk::ImageLayout::eUndefined;
    return graphics.CreateImage(create, image);
}

void TransitFrameGenerationImage(vk::CommandBuffer command, VulkanImage& image,
                                 vk::ImageLayout layout, vk::AccessFlags2 access) {
    const auto stage = access == vk::AccessFlagBits2::eTransferWrite ?
        vk::PipelineStageFlagBits2::eTransfer : vk::PipelineStageFlagBits2::eAllCommands;
    vk::ImageMemoryBarrier2 barrier {};
    barrier.srcStageMask = image.state.pl_stage;
    barrier.srcAccessMask = image.state.access_mask;
    barrier.dstStageMask = stage;
    barrier.dstAccessMask = access;
    barrier.oldLayout = image.state.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    vk::DependencyInfo dependency {};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    command.pipelineBarrier2(dependency);
    image.state = {stage, access, layout};
    image.subresource_states.clear();
}

bool CreatePass(GraphicContext& graphics, ComputePass& pass, std::span<const uint32_t> code,
                uint32_t sampled, uint32_t storage) {
    std::vector<vk::DescriptorSetLayoutBinding> bindings(sampled + storage);
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i < sampled ? vk::DescriptorType::eCombinedImageSampler : vk::DescriptorType::eStorageImage;
        bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
    }
    vk::DescriptorSetLayoutCreateInfo set {};
    set.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
    set.bindingCount = static_cast<uint32_t>(bindings.size());
    set.pBindings = bindings.data();
    if (graphics.device.createDescriptorSetLayout(&set, nullptr, &pass.descriptors) != vk::Result::eSuccess) return false;
    const vk::PushConstantRange push {vk::ShaderStageFlagBits::eCompute, 0, 16};
    vk::PipelineLayoutCreateInfo layout {};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &pass.descriptors;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (graphics.device.createPipelineLayout(&layout, nullptr, &pass.layout) != vk::Result::eSuccess) return false;
    const auto module = CompileSPV(code, graphics.device);
    vk::ComputePipelineCreateInfo pipeline {};
    pipeline.stage.stage = vk::ShaderStageFlagBits::eCompute;
    pipeline.stage.module = module;
    pipeline.stage.pName = "main";
    pipeline.layout = pass.layout;
    const auto result = graphics.device.createComputePipelines(nullptr, 1, &pipeline, nullptr, &pass.pipeline);
    graphics.device.destroyShaderModule(module, nullptr);
    return result == vk::Result::eSuccess;
}

void DestroyPass(vk::Device device, ComputePass& pass) {
    if (pass.pipeline) device.destroyPipeline(pass.pipeline, nullptr);
    if (pass.layout) device.destroyPipelineLayout(pass.layout, nullptr);
    if (pass.descriptors) device.destroyDescriptorSetLayout(pass.descriptors, nullptr);
}

void Dispatch(vk::CommandBuffer command, const ComputePass& pass,
              std::span<Image* const> sampled, std::span<Image* const> storage,
              vk::Sampler sampler, const void* push, uint32_t width, uint32_t height,
              std::span<const vk::ImageView> sampled_views = {}) {
    std::array<vk::DescriptorImageInfo, 7> infos {};
    std::array<vk::WriteDescriptorSet, 7> writes {};
    const auto count = sampled.size() + storage.size();
    EXIT_IF(count > infos.size());
    for (size_t i = 0; i < count; ++i) {
        const bool is_storage = i >= sampled.size();
        auto& image = *(is_storage ? storage[i - sampled.size()] : sampled[i]);
        infos[i].sampler = is_storage ? vk::Sampler {} : sampler;
        infos[i].imageView = !is_storage && i < sampled_views.size() ? sampled_views[i] : View(image, is_storage);
        infos[i].imageLayout = is_storage ? vk::ImageLayout::eGeneral : SampledLayout;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = is_storage ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eCombinedImageSampler;
        writes[i].pImageInfo = &infos[i];
    }
    command.bindPipeline(vk::PipelineBindPoint::eCompute, pass.pipeline);
    command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pass.layout, 0,
                                 static_cast<uint32_t>(count), writes.data());
    command.pushConstants(pass.layout, vk::ShaderStageFlagBits::eCompute, 0, 16, push);
    command.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}
}

struct Gt7Dlss::Impl {
    explicit Impl(RenderContext& c) : context(c), graphics(c.GetGraphics()), scheduler(c.GetCommandScheduler()) {}
    RenderContext& context;
    GraphicContext& graphics;
    CommandScheduler& scheduler;
    const KytyDlssApi* api = nullptr;
    KytyDlssContext* ngx = nullptr;
    ComputePass prepare, composite;
    vk::Sampler nearest, linear;
    std::unique_ptr<Image> color, depth, motion, invalid, output, effect, composed;
    ImageId scene_id {};
    GuestRange scene_range {};
    uint64_t evaluations = 0, composites = 0, cancellations = 0, rejected = 0;
    // Diagnostic buckets 0, 1, 2, 3+: the number of verified source-over draws
    // is not a validity condition for the premultiplied effect layer.
    std::array<uint64_t, 4> composites_by_effect_count {};
    std::chrono::steady_clock::time_point next_effect_report {};
    uint64_t boundary_evaluations = 0;
    uint64_t boundary_composites = 0, fg_frame = 1, fg_copied = 0, fg_snapshots = 0, fg_rejected = 0;
    uint32_t fg_temporal_attempts = 0;
    std::vector<FrameGenerationCamera> fg_cameras;
    CameraProbeState fg_camera_probe;
    std::array<uint32_t, CameraObservationCount> fg_camera_observations {};
    std::vector<std::array<uint64_t, 2>> fg_draw_signatures;
    uint32_t fg_candidate_draws = 0;
    uint64_t fg_camera_match_failures = 0, fg_hudless_captures = 0, fg_hudless_rejections = 0;
    std::array<std::pair<std::string, uint64_t>, 16> fg_cancel_reasons {};
    uint64_t fg_tonemap_rejections = 0;
    ImageId fg_scaled_scene_id {}, fg_output_id {};
    GuestRange fg_output_range {};
    vk::Image fg_output_image {};
    VulkanImage fg_hudless {};
    std::vector<uint8_t> fg_dcc_snapshot;
    bool fg_hudless_valid = false;
    HudlessDiagnostic fg_hudless_diagnostic;
    SceneDiagnostic fg_scene_diagnostic;
    uint32_t fg_tonemap_attempts = 0, fg_ui_draws = 0;
    Gt7FrameGenCamera::Parameters fg_camera {};
    float fg_jitter_x = 0.0f, fg_jitter_y = 0.0f, fg_frame_time_ms = 0.0f;
    bool fg_camera_valid = false, fg_reset = true, fg_input_ambiguous = false;
    bool active = false, reset = true, disabled = false, initialized = false;
    std::chrono::steady_clock::time_point last_time {};
    std::chrono::steady_clock::time_point fg_next_camera_report {};
    std::chrono::steady_clock::time_point fg_next_hudless_report {};
    std::chrono::steady_clock::time_point fg_next_scene_report {};

    ~Impl() {
        // RenderContext owns this object and has already finished its scheduler.
        LOGF("GT7 DLSS: session summary: evaluated=%llu composited=%llu cancelled=%llu rejected=%llu\n",
            static_cast<unsigned long long>(evaluations), static_cast<unsigned long long>(composites),
            static_cast<unsigned long long>(cancellations), static_cast<unsigned long long>(rejected));
        if (Gt7FrameGen::Requested())
            LOGF("GT7 FSR FG: input summary: captured=%llu rejected=%llu\n",
                 static_cast<unsigned long long>(fg_snapshots), static_cast<unsigned long long>(fg_rejected));
        if (ngx) {
            KytyDlssError error {};
            const auto result = api->destroy_context(ngx, 1, &error);
            if (result != KYTY_DLSS_OK) LOGF("GT7 DLSS: context destruction failed: %s\n", error.message);
        }
        DestroyPass(graphics.device, prepare);
        DestroyPass(graphics.device, composite);
        if (nearest) graphics.device.destroySampler(nearest, nullptr);
        if (linear) graphics.device.destroySampler(linear, nullptr);
        if (fg_hudless.image) graphics.DeleteImage(fg_hudless);
    }

    std::unique_ptr<Image> MakeImage(uint32_t w, uint32_t h, vk::Format format, uint32_t bytes) {
        ImageInfo info {};
        info.extent = vk::Extent3D {w, h, 1};
        info.pixel_format = format;
        info.bytes_per_block = bytes;
        info.pitch = w;
        return std::make_unique<Image>(graphics, scheduler, info);
    }

    bool Initialize() {
        if (initialized) return true;
        if (disabled || !graphics.gt7_dlss_device_enabled || !(api = Gt7DlssHost::Api())) return false;
        std::vector<const char*> extensions;
        for (const auto& name : graphics.gt7_dlss_device_extensions) extensions.push_back(name.c_str());
        KytyDlssCreateInfo create {};
        create.struct_size = sizeof(create);
        create.paths = Gt7DlssHost::Paths();
        create.instance = static_cast<VkInstance>(graphics.instance);
        create.physical_device = static_cast<VkPhysicalDevice>(graphics.physical_device);
        create.device = static_cast<VkDevice>(graphics.device);
        create.get_instance_proc_addr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
        create.get_device_proc_addr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
        create.queue_family_index = graphics.queue_family;
        create.enabled_feature_flags = graphics.gt7_dlss_feature_flags;
        create.enabled_device_extension_count = static_cast<uint32_t>(extensions.size());
        create.enabled_device_extensions = extensions.data();
        create.input_width = InputWidth; create.input_height = InputHeight;
        create.output_width = OutputWidth; create.output_height = OutputHeight;
        create.quality = KYTY_DLSS_QUALITY;
        KytyDlssError error {};
        if (api->create_context(&create, &ngx, &error) != KYTY_DLSS_OK) {
            LOGF("GT7 DLSS: backend unavailable; original renderer retained: %s\n", error.message);
            disabled = true;
            return false;
        }
        if (!CreatePass(graphics, prepare, GT7_DLSS_PREPARE_SPV, 3, 4) ||
            !CreatePass(graphics, composite, GT7_DLSS_COMPOSITE_SPV, 4, 1)) {
            LOGF("GT7 DLSS: compute pipeline unavailable; original renderer retained\n");
            disabled = true;
            return false;
        }
        vk::SamplerCreateInfo sampler {};
        sampler.magFilter = sampler.minFilter = vk::Filter::eNearest;
        sampler.mipmapMode = vk::SamplerMipmapMode::eNearest;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        RequireVulkanSuccess(graphics.device.createSampler(&sampler, nullptr, &nearest), "create GT7 DLSS point sampler");
        sampler.magFilter = sampler.minFilter = vk::Filter::eLinear;
        RequireVulkanSuccess(graphics.device.createSampler(&sampler, nullptr, &linear), "create GT7 DLSS linear sampler");
        color = MakeImage(InputWidth, InputHeight, vk::Format::eR16G16B16A16Sfloat, 8);
        depth = MakeImage(InputWidth, InputHeight, vk::Format::eR32Sfloat, 4);
        motion = MakeImage(InputWidth, InputHeight, vk::Format::eR32G32Sfloat, 8);
        invalid = MakeImage(InputWidth, InputHeight, vk::Format::eR8Unorm, 1);
        output = MakeImage(OutputWidth, OutputHeight, vk::Format::eR16G16B16A16Sfloat, 8);
        effect = MakeImage(InputWidth, InputHeight, vk::Format::eR16G16B16A16Sfloat, 8);
        composed = MakeImage(GuestWidth, GuestHeight, vk::Format::eR16G16B16A16Sfloat, 8);
        initialized = true;
        LOGF("GT7 DLSS: experimental GPU helper ready, 1920x1080 -> 2560x1440, preset K quality (fixed input); guest tone-map/UI 3840x2160\n");
        return true;
    }

    bool Reject(const char* reason) {
        fg_scene_diagnostic.NoteFirst("temporal pass rejected", reason);
        reset = true;
        fg_camera_valid = false;
        fg_input_ambiguous = true;
        if (Report(++rejected)) LOGF("GT7 DLSS: rejected temporal pass #%llu: %s\n", static_cast<unsigned long long>(rejected), reason);
        return false;
    }
};

Gt7Dlss::Gt7Dlss(RenderContext& context) : m_impl(std::make_unique<Impl>(context)) {}
Gt7Dlss::~Gt7Dlss() = default;
bool Gt7Dlss::Active() const {
    return m_impl->active;
}
Image* Gt7Dlss::EffectLayer() { return Active() ? m_impl->effect.get() : nullptr; }
bool Gt7Dlss::MatchesScene(const RenderColorInfo& color) const {
    return Active() && ((color.image_id && color.image_id == m_impl->scene_id) ||
                        Overlap(color.desc.info.data, m_impl->scene_range));
}
bool Gt7Dlss::MatchesWatchedWriteRange(GuestRange range) const {
    const auto& s = *m_impl;
    if (!s.active && !s.fg_hudless_valid) return false;
    auto& textures = s.context.GetTextureCache();
    return (s.active && textures.WriteOverlapsImage(range, s.scene_id, s.scene_range)) ||
           (s.fg_hudless_valid && textures.WriteOverlapsImage(range, s.fg_output_id, s.fg_output_range));
}
void Gt7Dlss::CancelFrame(const char* reason, bool invalidate_history) {
    auto& s = *m_impl;
    // Calls before the first temporal pass are often no-ops. Do not let those
    // hide the first cancellation which actually discarded scene state.
    if (s.active || s.fg_hudless_valid || invalidate_history)
        s.fg_scene_diagnostic.NoteFirst("scene cancelled", reason);
    // A clear, unbounded writer or later unverified operation must also discard
    // the already captured display-color scene, even after SR has composited.
    if (s.fg_hudless_valid) {
        s.fg_hudless_diagnostic.Note("captured scene invalidated", reason);
        bool first_for_reason = false;
        // Title-scene rejections must not hide the first new race failure.
        // Bound both storage and logging; reason strings are copied once.
        for (auto& [known_reason, count] : s.fg_cancel_reasons) {
            if (known_reason.empty()) known_reason = reason;
            if (known_reason == reason) {
                first_for_reason = ++count <= 4;
                break;
            }
        }
        if (++s.fg_hudless_rejections <= 8 || s.fg_hudless_rejections % 300 == 0 || first_for_reason)
            LOGF("GT7 FSR FG: HUD-less rejected #%llu frame=%llu: %s\n",
                 static_cast<unsigned long long>(s.fg_hudless_rejections),
                 static_cast<unsigned long long>(s.fg_frame), reason);
    }
    s.fg_hudless_valid = false;
    if (invalidate_history) {
        s.reset = true;
        s.fg_camera_valid = false;
        s.fg_input_ambiguous = true;
    }
    if (!s.active) return;
    s.active = false;
    s.fg_camera_valid = false;
    s.reset = true;
    if (Report(++s.cancellations)) LOGF("GT7 DLSS: cancelled frame #%llu: %s (evaluated=%llu composited=%llu)\n",
        static_cast<unsigned long long>(s.cancellations), reason,
        static_cast<unsigned long long>(s.evaluations), static_cast<unsigned long long>(s.composites));
}

void Gt7Dlss::CancelSceneWriterFrame(const char* reason, uint64_t vertex_shader,
                                    uint64_t pixel_shader, const char* details) {
    auto& s = *m_impl;
    auto& diagnostic = s.fg_scene_diagnostic;
    // The caller is the failed effect-writer branch. Keep the same first-cause
    // rule as CancelFrame; never attribute a prior rejection to this later draw.
    if (s.active && !diagnostic.reason[0]) {
        diagnostic.NoteFirst("scene writer cancelled", reason, {}, {}, vertex_shader);
        diagnostic.writer_ps = pixel_shader;
        diagnostic.scene_writer = true;
        std::snprintf(diagnostic.writer_details.data(), diagnostic.writer_details.size(),
                      "%s", details ? details : "details unavailable");
    }
    CancelFrame(reason);
}

void Gt7Dlss::FrameBoundary() {
    auto& s = *m_impl;
    // Retiring a completed frame is not an unverified writer rejection.
    if (!s.active) s.fg_hudless_valid = false;
    CancelFrame("scene reached presentation without a verified composite");
    // Motion vectors refer to the previous guest frame, not the previous NGX call.
    if (s.evaluations == s.boundary_evaluations) s.reset = true;
    s.boundary_evaluations = s.evaluations;
    s.boundary_composites = s.composites;
    s.fg_temporal_attempts = 0;
    s.fg_camera_valid = false;
    s.fg_input_ambiguous = false;
    s.fg_cameras.clear();
    s.fg_camera_probe.pending.clear();
    s.fg_camera_observations.fill(0);
    s.fg_candidate_draws = 0;
    s.fg_scaled_scene_id = {};
    s.fg_output_id = {};
    s.fg_output_range = {};
    s.fg_output_image = nullptr;
    s.fg_hudless_valid = false;
    s.fg_tonemap_attempts = s.fg_ui_draws = 0;
    s.fg_hudless_diagnostic = {};
    s.fg_scene_diagnostic = {};
    ++s.fg_frame;
}

// The recording hooks and the GPU-only methods below share no descriptor-pool
// allocations: push descriptors are copied into each command buffer.

bool Gt7Dlss::BeginTaa(CommandBuffer& buffer, const PreparedBindings& pixel,
                       std::span<const RenderColorInfo> colors) {
    auto& s = *m_impl;
    ++s.fg_temporal_attempts;
    s.fg_camera_valid = false;
    if (s.active) CancelFrame("new temporal pass before composite");
    if (!pixel.runtime || !*pixel.runtime || pixel.images.size() != 4 ||
        pixel.buffer_sources.size() != 1 || colors.size() != 2 ||
        !pixel.bindless_heaps.empty() || !pixel.bindless_patches.empty())
        return s.Reject("binding cardinality");
    const auto& program = *pixel.runtime->program;
    if (program.stage != ShaderType::Pixel || program.shader_hash != 0xc3113b114fb02e5dULL ||
        program.info.images.size() != 4 || program.info.buffers.size() != 1 ||
        program.info.buffers[0].written || !pixel.runtime->resources->gpu_fills.empty())
        return s.Reject("unverified temporal program");
    for (size_t i = 0; i < 4; ++i) {
        const auto& texture = pixel.images[i];
        const auto& resource = program.info.images[i];
        if (!texture.image_id || !texture.image_view)
            return s.Reject("unresolved temporal texture view");
        if (!BaseView(texture.desc))
            return s.Reject("unsupported temporal texture subresource");
        if (resource.written || resource.bindless || resource.runtime)
            return s.Reject("unverified temporal resource access");
    }
    for (const auto& target : colors) {
        if (!target.image_id || !BaseView(target.desc) || target.guest_mip_level || target.guest_array_layer)
            return s.Reject("unsupported temporal target view");
    }
    auto& textures = s.context.GetTextureCache();
    auto& source_depth = textures.GetImage(pixel.images[0].image_id);
    auto& source_motion = textures.GetImage(pixel.images[1].image_id);
    auto& source_color = textures.GetImage(pixel.images[2].image_id);
    auto& source_history = textures.GetImage(pixel.images[3].image_id);
    auto& history_target = textures.GetImage(colors[0].image_id);
    auto& scene_target = textures.GetImage(colors[1].image_id);
    if (!Shape(source_depth, InputWidth, InputHeight, vk::Format::eD32SfloatS8Uint) ||
        !Shape(source_motion, InputWidth, InputHeight, vk::Format::eR16G16Sfloat) ||
        !Shape(source_color, InputWidth, InputHeight, vk::Format::eB10G11R11UfloatPack32) ||
        !Shape(source_history, GuestWidth, GuestHeight, vk::Format::eR16G16B16A16Sfloat) ||
        !Shape(history_target, GuestWidth, GuestHeight, vk::Format::eR16G16B16A16Sfloat) ||
        !Shape(scene_target, InputWidth, InputHeight, vk::Format::eB10G11R11UfloatPack32) ||
        colors[0].target_slot != 0 || colors[1].target_slot != 1 ||
        colors[0].image_id == pixel.images[3].image_id ||
        colors[1].image_id == pixel.images[2].image_id)
        return s.Reject("not the verified 1080p temporal resources");

    // This hook runs after RebindImages but before CommitBindings, which is what
    // fills TextureBinding::layout for the guest draw. Use the tracked backing
    // state of the three images sampled here. History is only matched, not read
    // by this helper, and can still be uninitialized on the first guest frame.
    if (source_color.backing.state.layout == vk::ImageLayout::eUndefined ||
        source_depth.backing.state.layout == vk::ImageLayout::eUndefined ||
        source_motion.backing.state.layout == vk::ImageLayout::eUndefined)
        return s.Reject("uninitialized temporal input image state");

    const auto& source = pixel.buffer_sources[0];
    constexpr uint64_t ConstantBytes = 128;
    if (!source.address || source.address > std::numeric_limits<uint64_t>::max() - ConstantBytes ||
        source.size != ConstantBytes ||
        Libs::LibKernel::Memory::TryClampRangeSize(source.address, ConstantBytes) != ConstantBytes ||
        s.context.GetBufferCache().HasGpuDirtyBytes(source.address, ConstantBytes))
        return s.Reject("temporal constants are not clean CPU data");
    std::array<float, 32> constants {};
    if (!Libs::LibKernel::Memory::TryReadBacking(source.address, constants.data(), ConstantBytes) ||
        constants[14] != 960.0f || constants[15] != -540.0f ||
        constants[16] != 1920.0f || constants[17] != 1080.0f ||
        constants[18] != 1.0f || constants[19] != 1.0f ||
        constants[24] != 0.0f || constants[25] != 0.0f ||
        !std::isfinite(constants[20]) || !std::isfinite(constants[21]) ||
        std::abs(constants[20] * 960.0f) > 2.0f || std::abs(constants[21] * 540.0f) > 2.0f)
        return s.Reject("temporal dimensions or jitter contract changed");
    if (!s.Initialize()) return s.Reject("backend initialization unavailable");

    // Handle drains pending CPU recording, not GPU execution. Every subsequent
    // command here records synchronously, so NGX and the host do not race.
    s.scheduler.EndRendering();
    const auto command = buffer.Handle();
    const std::array<Image*, 3> inputs {&source_color, &source_depth, &source_motion};
    const std::array<size_t, 3> indices {2, 0, 1};
    std::array<VulkanImageState, 3> saved {};
    std::array<vk::ImageView, 3> views {};
    for (size_t i = 0; i < inputs.size(); ++i) {
        saved[i] = inputs[i]->backing.state;
        views[i] = pixel.images[indices[i]].image_view;
        Transit(*inputs[i], SampledLayout, ShaderRead, command);
    }
    const std::array<Image*, 4> prepared {s.color.get(), s.depth.get(), s.motion.get(), s.invalid.get()};
    for (auto* image : prepared) Transit(*image, vk::ImageLayout::eGeneral, ShaderWrite, command);
    const struct { uint32_t width, height; float x, y; } push {
        InputWidth, InputHeight, constants[20], constants[21]};
    Dispatch(command, s.prepare, inputs, prepared, s.nearest, &push, InputWidth, InputHeight, views);
    for (auto* image : prepared) Transit(*image, SampledLayout, ShaderRead, command);
    for (size_t i = 0; i < inputs.size(); ++i)
        Transit(*inputs[i], saved[i].layout, saved[i].access_mask | ShaderRead, command);
    Transit(*s.output, vk::ImageLayout::eGeneral, ShaderWrite, command);

    KytyDlssEvaluateInfo evaluate {};
    evaluate.struct_size = sizeof(evaluate);
    evaluate.command_buffer = static_cast<VkCommandBuffer>(command);
    evaluate.color = BackendImage(*s.color);
    evaluate.depth = BackendImage(*s.depth);
    evaluate.motion = BackendImage(*s.motion);
    evaluate.output = BackendImage(*s.output, true);
    evaluate.jitter_x = -960.0f * constants[20];
    evaluate.jitter_y = 540.0f * constants[21];
    const auto now = std::chrono::steady_clock::now();
    evaluate.frame_time_ms = s.last_time.time_since_epoch().count() == 0 ? 16.666667f :
        std::clamp(std::chrono::duration<float, std::milli>(now - s.last_time).count(), 1.0f, 100.0f);
    evaluate.pre_exposure = evaluate.exposure_scale = 1.0f;
    evaluate.reset_history = s.reset ? 1u : 0u;
    KytyDlssRecordResult result {};
    result.struct_size = sizeof(result);
    KytyDlssError error {};
    const auto status = s.api->record_evaluate(s.ngx, &evaluate, &result, &error);
    s.scheduler.StateShadow().InvalidateAll();
    if (status != KYTY_DLSS_OK) {
        if (status == KYTY_DLSS_INVALID_ARGUMENT) {
            LOGF("GT7 DLSS: evaluation rejected before NGX recording: %s\n", error.message);
            return s.Reject("backend input contract");
        }
        EXIT("GT7 DLSS: backend recording failed (%d, NGX 0x%x): %s. The shared command buffer may be partially recorded; terminating the experimental session without submitting it.\n",
             static_cast<int>(status), error.ngx_result, error.message);
    }
    Transit(*s.output, SampledLayout, ShaderRead, command);
    Transit(*s.effect, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, command);
    const vk::ClearColorValue zero {std::array<float, 4> {0, 0, 0, 0}};
    const vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    command.clearColorImage(s.effect->backing.image, vk::ImageLayout::eTransferDstOptimal, &zero, 1, &range);
    Transit(*s.effect, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite, command);
    s.scene_id = colors[1].image_id;
    s.scene_range = colors[1].desc.info.data;
    ++s.evaluations;
    if (CameraProbeEnabled())
        WriteCameraProbes(s.fg_camera_probe, pixel.images[0].image_id, pixel.images[1].image_id,
                         s.fg_frame, constants[20], constants[21]);
    if (s.graphics.gt7_fsr_present_queue && Gt7FrameGen::Requested()) {
        // Require camera and temporal resources from this guest frame, including
        // the precise projection jitter used by the producer and resolve.
        for (const auto& camera : s.fg_cameras) {
            if (camera.valid && camera.depth_id == pixel.images[0].image_id &&
                camera.motion_id == pixel.images[1].image_id &&
                camera.parameters.jitter_ndc_x == constants[20] &&
                camera.parameters.jitter_ndc_y == constants[21]) {
                s.fg_camera = camera.parameters;
                s.fg_camera_valid = true;
                break;
            }
        }
        const bool failed_camera = !s.fg_camera_valid && s.fg_camera_observations[CameraCalls] != 0;
        if (now >= s.fg_next_camera_report) {
            s.fg_next_camera_report = now + std::chrono::seconds(5);
            const char* reason = s.fg_camera_valid ? "none" :
                s.fg_camera_observations[CameraCalls] == 0 ? "no proved producer with compatible raster" :
                "no producer for current TAA depth/motion pair";
            if (!s.fg_camera_valid) for (const auto& camera : s.fg_cameras) {
                if (camera.depth_id == pixel.images[0].image_id && camera.motion_id == pixel.images[1].image_id) {
                    reason = camera.reject_reason ? camera.reject_reason : "projection jitter mismatch";
                    break;
                }
            }
            std::printf("GT7 FSR FG camera: frame=%llu candidate_draws=%u observed=%u accepted=%u matched=%u reason=%s\n",
                static_cast<unsigned long long>(s.fg_frame), s.fg_candidate_draws,
                s.fg_camera_observations[CameraCalls], s.fg_camera_observations[CameraAccepted],
                s.fg_camera_valid ? 1u : 0u, reason);
        }
        const bool report_failure = failed_camera &&
            (++s.fg_camera_match_failures <= 8 || s.fg_camera_match_failures % 300 == 0);
        if (report_failure)
            LOGF("GT7 FSR FG: camera association failure #%llu frame=%llu (details follow)\n",
                 static_cast<unsigned long long>(s.fg_camera_match_failures),
                 static_cast<unsigned long long>(s.fg_frame));
        if (Report(s.evaluations) || report_failure) {
            uint32_t valid = 0, depth_matches = 0, motion_matches = 0, pairs = 0, jitter_matches = 0;
            for (const auto& camera : s.fg_cameras) {
                valid += camera.valid;
                const bool depth_match = camera.depth_id == pixel.images[0].image_id;
                const bool motion_match = camera.motion_id == pixel.images[1].image_id;
                depth_matches += depth_match; motion_matches += motion_match;
                pairs += depth_match && motion_match;
                jitter_matches += camera.parameters.jitter_ndc_x == constants[20] &&
                                  camera.parameters.jitter_ndc_y == constants[21];
            }
            const auto& o = s.fg_camera_observations;
            LOGF("GT7 FSR FG: camera match frame=%llu candidate_draws=%u observed=%u candidates=%zu valid=%u depth_match=%u motion_match=%u pair_match=%u jitter_match=%u target_depth=%u:%u target_motion=%u:%u jitter_ndc=(%.9g,%.9g) accepted=%u failures=[depth:%u motion:%u shape:%u late:%u limit:%u rejected:%u user:%u address:%u unmapped:%u gpu_dirty:%u read:%u matrix:%u changed:%u]\n",
                 static_cast<unsigned long long>(s.fg_frame), s.fg_candidate_draws, o[CameraCalls],
                 s.fg_cameras.size(), valid, depth_matches, motion_matches, pairs, jitter_matches,
                 pixel.images[0].image_id.index, pixel.images[0].image_id.generation,
                 pixel.images[1].image_id.index, pixel.images[1].image_id.generation, constants[20], constants[21],
                 o[CameraAccepted], o[CameraDepth], o[CameraMotion], o[CameraShape], o[CameraLate], o[CameraLimit],
                 o[CameraAlreadyRejected], o[CameraUser], o[CameraAddress], o[CameraUnmapped],
                 o[CameraGpuDirty], o[CameraRead], o[CameraMatrix], o[CameraChanged]);
            for (size_t i = 0; i < std::min(s.fg_cameras.size(), size_t(2)); ++i) {
                const auto& camera = s.fg_cameras[i];
                LOGF("GT7 FSR FG: camera candidate[%zu] depth=%u:%u motion=%u:%u valid=%u rejected=%u jitter_ndc=(%.9g,%.9g) reason=%s\n",
                     i, camera.depth_id.index, camera.depth_id.generation, camera.motion_id.index,
                     camera.motion_id.generation, camera.valid ? 1u : 0u, camera.rejected ? 1u : 0u,
                     camera.parameters.jitter_ndc_x, camera.parameters.jitter_ndc_y,
                     camera.reject_reason ? camera.reject_reason : "none");
            }
        }
        s.fg_jitter_x = evaluate.jitter_x;
        s.fg_jitter_y = evaluate.jitter_y;
        // Keep the unclamped duration so the FG backend can detect a pause;
        // the stable SR backend retains its existing clamped duration above.
        s.fg_frame_time_ms = s.last_time.time_since_epoch().count() == 0 ? 16.666667f :
            std::chrono::duration<float, std::milli>(now - s.last_time).count();
        s.fg_reset = result.reset_used != 0 || s.fg_frame_time_ms > 100.0f;
    }
    s.last_time = now;
    s.reset = false;
    s.active = true;
    if (s.evaluations == 1) {
        const auto* trace = std::getenv("KYTY_GT7_DLSS_TRACE");
        if (trace && trace[0] == '1' && trace[1] == '\0') {
            FrameGraph::RequestCapture();
            LOGF("GT7 DLSS: requested frame-graph trace after first evaluation\n");
        }
    }
    if (Report(s.evaluations)) LOGF("GT7 DLSS: evaluated #%llu 1920x1080 -> 2560x1440, jitter=(%.6f,%.6f), reset=%u\n",
        static_cast<unsigned long long>(s.evaluations), evaluate.jitter_x, evaluate.jitter_y, result.reset_used);
    return true;
}

#include "gt7DlssComposite.inl"

#include "gt7FrameGenInputs.inl"

} // namespace Libs::Graphics

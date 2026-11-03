#include "graphics/host_gpu/renderer/gt7Dlss.h"

#include "graphics/host_gpu/gt7DlssDevice.h"
#include "graphics/host_gpu/renderer/colorRenderTarget.h"
#include "graphics/host_gpu/renderer/commandRecorder.h"
#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/shader/shader.h"
#include "kernel/memory.h"
#include "gpu_dlss_shaders/gt7_dlss_prepare_spv.h"
#include "gpu_dlss_shaders/gt7_dlss_composite_spv.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Libs::Graphics {
namespace {
constexpr uint32_t InputWidth = 1920, InputHeight = 1080;
constexpr uint32_t OutputWidth = 3840, OutputHeight = 2160;
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
    uint64_t boundary_evaluations = 0;
    bool active = false, reset = true, disabled = false, initialized = false;
    std::chrono::steady_clock::time_point last_time {};

    ~Impl() {
        // RenderContext owns this object and has already finished its scheduler.
        if (ngx) {
            KytyDlssError error {};
            const auto result = api->destroy_context(ngx, 1, &error);
            if (result != KYTY_DLSS_OK) LOGF("GT7 DLSS: context destruction failed: %s\n", error.message);
        }
        DestroyPass(graphics.device, prepare);
        DestroyPass(graphics.device, composite);
        if (nearest) graphics.device.destroySampler(nearest, nullptr);
        if (linear) graphics.device.destroySampler(linear, nullptr);
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
        create.quality = KYTY_DLSS_PERFORMANCE;
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
        composed = MakeImage(OutputWidth, OutputHeight, vk::Format::eR16G16B16A16Sfloat, 8);
        initialized = true;
        LOGF("GT7 DLSS: experimental GPU helper ready, 1920x1080 -> 3840x2160, preset K performance\n");
        return true;
    }

    bool Reject(const char* reason) {
        reset = true;
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
void Gt7Dlss::CancelFrame(const char* reason, bool invalidate_history) {
    auto& s = *m_impl;
    if (invalidate_history) s.reset = true;
    if (!s.active) return;
    s.active = false;
    s.reset = true;
    if (Report(++s.cancellations)) LOGF("GT7 DLSS: cancelled frame #%llu: %s (evaluated=%llu composited=%llu)\n",
        static_cast<unsigned long long>(s.cancellations), reason,
        static_cast<unsigned long long>(s.evaluations), static_cast<unsigned long long>(s.composites));
}

void Gt7Dlss::FrameBoundary() {
    auto& s = *m_impl;
    CancelFrame("scene reached presentation without a verified composite");
    // Motion vectors refer to the previous guest frame, not the previous NGX call.
    if (s.evaluations == s.boundary_evaluations) s.reset = true;
    s.boundary_evaluations = s.evaluations;
}

// The recording hooks and the GPU-only methods below share no descriptor-pool
// allocations: push descriptors are copied into each command buffer.

bool Gt7Dlss::BeginTaa(CommandBuffer& buffer, const PreparedBindings& pixel,
                       std::span<const RenderColorInfo> colors) {
    auto& s = *m_impl;
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
        if (!texture.image_id || !texture.image_view || !BaseView(texture.desc) ||
            texture.layout == vk::ImageLayout::eUndefined || resource.written ||
            resource.bindless || resource.runtime)
            return s.Reject("unsupported temporal texture view");
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
        !Shape(source_history, OutputWidth, OutputHeight, vk::Format::eR16G16B16A16Sfloat) ||
        !Shape(history_target, OutputWidth, OutputHeight, vk::Format::eR16G16B16A16Sfloat) ||
        !Shape(scene_target, InputWidth, InputHeight, vk::Format::eB10G11R11UfloatPack32) ||
        colors[0].target_slot != 0 || colors[1].target_slot != 1 ||
        colors[0].image_id == pixel.images[3].image_id ||
        colors[1].image_id == pixel.images[2].image_id)
        return s.Reject("not the verified 1080p temporal resources");

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
        saved[i].layout = pixel.images[indices[i]].layout;
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
    s.last_time = now;
    s.reset = false;
    s.active = true;
    if (Report(s.evaluations)) LOGF("GT7 DLSS: evaluated #%llu 1920x1080 -> 3840x2160, jitter=(%.6f,%.6f), reset=%u\n",
        static_cast<unsigned long long>(s.evaluations), evaluate.jitter_x, evaluate.jitter_y, result.reset_used);
    return true;
}

#include "gt7DlssComposite.inl"

} // namespace Libs::Graphics

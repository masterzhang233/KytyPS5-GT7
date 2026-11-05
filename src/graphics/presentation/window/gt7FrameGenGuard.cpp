#include "graphics/presentation/window/gt7FrameGenGuard.h"
#include "gpu_dlss_shaders/gt7_fsr_occlusion_mask_spv.h"
#include "gpu_dlss_shaders/gt7_fsr_occlusion_resolve_spv.h"

#include <limits>
#include <span>

namespace Libs::Graphics::Gt7FrameGen {
namespace {
constexpr vk::ImageSubresourceRange ColorRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
constexpr vk::ImageSubresourceLayers ColorLayer {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
constexpr auto Compute = vk::PipelineStageFlagBits2::eComputeShader;
constexpr auto Transfer = vk::PipelineStageFlagBits2::eTransfer;
constexpr auto All = vk::PipelineStageFlagBits2::eAllCommands;
constexpr auto Read = vk::AccessFlagBits2::eShaderRead;
constexpr auto Write = vk::AccessFlagBits2::eShaderWrite;

void Barrier(vk::CommandBuffer command, vk::Image image,
             vk::ImageLayout before, vk::ImageLayout after,
             vk::PipelineStageFlags2 source_stage, vk::AccessFlags2 source_access,
             vk::PipelineStageFlags2 target_stage, vk::AccessFlags2 target_access) {
    vk::ImageMemoryBarrier2 barrier {};
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = target_stage;
    barrier.dstAccessMask = target_access;
    barrier.oldLayout = before;
    barrier.newLayout = after;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = ColorRange;
    vk::DependencyInfo dependency {};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    command.pipelineBarrier2(&dependency);
}

bool Supports(vk::PhysicalDevice physical, vk::Format format, vk::FormatFeatureFlags required) {
    vk::FormatProperties properties {};
    physical.getFormatProperties(format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}
} // namespace

bool OcclusionGuard::CreateView(vk::Image image, vk::Format format, vk::ImageView& view) {
    vk::ImageViewCreateInfo info {};
    info.image = image;
    info.viewType = vk::ImageViewType::e2D;
    info.format = format;
    info.subresourceRange = ColorRange;
    return m_device.createImageView(&info, nullptr, &view) == vk::Result::eSuccess;
}

bool OcclusionGuard::RegisterPresent(GraphicContext& graphics, vk::Image image,
                                     vk::Extent2D display, vk::Format format) {
    if (!image || !graphics.device || !graphics.allocator || !display.width || !display.height ||
        (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eB8G8R8A8Unorm)) return false;
    if (m_device && (m_device != graphics.device || m_display != display || m_format != format)) return false;
    m_device = graphics.device;
    m_physical = graphics.physical_device;
    m_allocator = graphics.allocator;
    m_display = display;
    m_format = format;
    for (uint32_t i = 0; i < m_present_count; ++i)
        if (m_present[i].image == image) return !m_initialized || CreateTargets();
    if (m_present_count == m_present.size()) return false;
    vk::ImageView view {};
    if (!CreateView(image, format, view)) return false;
    m_present[m_present_count++] = {image, view};
    return !m_initialized || CreateTargets();
}

bool OcclusionGuard::CreateImage(Image& image, vk::Extent2D extent, vk::Format format,
                                 vk::ImageUsageFlags usage) {
    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = static_cast<VkFormat>(format);
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = static_cast<VkImageUsageFlags>(usage);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation {};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage native = VK_NULL_HANDLE;
    if (vmaCreateImage(m_allocator, &info, &allocation, &native, &image.allocation, nullptr) != VK_SUCCESS) return false;
    image.image = native;
    return CreateView(image.image, format, image.view);
}

bool OcclusionGuard::CreatePipelines() {
    const std::array<std::span<const uint32_t>, 2> codes {GT7_FSR_OCCLUSION_MASK_SPV, GT7_FSR_OCCLUSION_RESOLVE_SPV};
    for (uint32_t pass = 0; pass < 2; ++pass) {
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings {};
        const uint32_t count = pass == 0 ? 3 : 4;
        for (uint32_t i = 0; i < count; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = i < 2 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }
        vk::DescriptorSetLayoutCreateInfo set {};
        set.bindingCount = count;
        set.pBindings = bindings.data();
        if (m_device.createDescriptorSetLayout(&set, nullptr, &m_set_layout[pass]) != vk::Result::eSuccess) return false;
        vk::PushConstantRange push {vk::ShaderStageFlagBits::eCompute, 0, 16};
        vk::PipelineLayoutCreateInfo layout {};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_set_layout[pass];
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        if (m_device.createPipelineLayout(&layout, nullptr, &m_pipeline_layout[pass]) != vk::Result::eSuccess) return false;
        vk::ShaderModuleCreateInfo module_info {};
        module_info.codeSize = codes[pass].size_bytes();
        module_info.pCode = codes[pass].data();
        vk::ShaderModule module {};
        if (m_device.createShaderModule(&module_info, nullptr, &module) != vk::Result::eSuccess) return false;
        vk::ComputePipelineCreateInfo pipeline {};
        pipeline.stage.stage = vk::ShaderStageFlagBits::eCompute;
        pipeline.stage.module = module;
        pipeline.stage.pName = "main";
        pipeline.layout = m_pipeline_layout[pass];
        const auto result = m_device.createComputePipelines(nullptr, 1, &pipeline, nullptr, &m_pipeline[pass]);
        m_device.destroyShaderModule(module);
        if (result != vk::Result::eSuccess) return false;
    }
    const std::array<vk::DescriptorPoolSize, 2> sizes {{
        {vk::DescriptorType::eSampledImage, 132}, {vk::DescriptorType::eStorageImage, 130}}};
    vk::DescriptorPoolCreateInfo pool {};
    pool.maxSets = 66;
    pool.poolSizeCount = uint32_t(sizes.size());
    pool.pPoolSizes = sizes.data();
    if (m_device.createDescriptorPool(&pool, nullptr, &m_pool) != vk::Result::eSuccess) return false;
    const std::array<vk::DescriptorSetLayout, 2> layouts {m_set_layout[0], m_set_layout[0]};
    vk::DescriptorSetAllocateInfo allocate {};
    allocate.descriptorPool = m_pool;
    allocate.descriptorSetCount = 2;
    allocate.pSetLayouts = layouts.data();
    if (m_device.allocateDescriptorSets(&allocate, m_mask_sets.data()) != vk::Result::eSuccess) return false;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        const std::array<vk::DescriptorImageInfo, 3> images {{
            {nullptr, m_motion[slot].view, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nullptr, m_motion[slot ^ 1].view, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nullptr, m_mask.view, vk::ImageLayout::eGeneral}}};
        std::array<vk::WriteDescriptorSet, 3> writes {};
        for (uint32_t i = 0; i < writes.size(); ++i) {
            writes[i].dstSet = m_mask_sets[slot];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i < 2 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage;
            writes[i].pImageInfo = &images[i];
        }
        m_device.updateDescriptorSets(uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
}

bool OcclusionGuard::Initialize(GraphicContext& graphics, vk::Extent2D render,
                                vk::Extent2D display, vk::Format format) {
    if (m_initialized) return Matches(render, display, format) && m_device == graphics.device;
    if (!render.width || !render.height || !display.width || !display.height || !graphics.device ||
        !graphics.allocator || (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eB8G8R8A8Unorm) ||
        (m_device && (m_device != graphics.device || m_display != display || m_format != format))) return false;
    m_device = graphics.device;
    m_physical = graphics.physical_device;
    m_allocator = graphics.allocator;
    m_render = render;
    m_display = display;
    m_format = format;
    const auto sampled = vk::FormatFeatureFlagBits::eSampledImage;
    const auto storage = vk::FormatFeatureFlagBits::eStorageImage;
    if (!Supports(m_physical, format, sampled | vk::FormatFeatureFlagBits::eBlitDst) ||
        !Supports(m_physical, vk::Format::eR8G8B8A8Unorm, storage | vk::FormatFeatureFlagBits::eBlitSrc) ||
        !Supports(m_physical, vk::Format::eR32G32Sfloat, sampled) ||
        !Supports(m_physical, vk::Format::eR32Uint, storage | vk::FormatFeatureFlagBits::eStorageImageAtomic)) {
        Destroy();
        return false;
    }
    const auto motion_usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    if (!CreateImage(m_motion[0], render, vk::Format::eR32G32Sfloat, motion_usage) ||
        !CreateImage(m_motion[1], render, vk::Format::eR32G32Sfloat, motion_usage) ||
        !CreateImage(m_mask, render, vk::Format::eR32Uint,
                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc) ||
        !CreateImage(m_scratch, display, vk::Format::eR8G8B8A8Unorm,
                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc) || !CreatePipelines()) {
        Destroy(); // No commands can use these new resources yet.
        return false;
    }
    m_initialized = true;
    return CreateTargets();
}

bool OcclusionGuard::Matches(vk::Extent2D render, vk::Extent2D display, vk::Format format) const {
    return m_initialized && m_render == render && m_display == display && m_format == format;
}

bool OcclusionGuard::RegisterOutput(vk::Image image) {
    if (!m_initialized || !image) return false;
    for (uint32_t i = 0; i < m_output_count; ++i)
        if (m_outputs[i].image == image) return CreateTargets();
    if (m_output_count == m_outputs.size()) return false;
    vk::ImageView view {};
    if (!CreateView(image, m_format, view)) return false;
    m_outputs[m_output_count++] = {image, view};
    return CreateTargets();
}

bool OcclusionGuard::CreateTargets() {
    if (!m_initialized) return false;
    for (uint32_t p = 0; p < m_present_count; ++p) for (uint32_t o = 0; o < m_output_count; ++o) {
        bool found = false;
        for (uint32_t i = 0; i < m_target_count; ++i)
            found |= m_targets[i].present == m_present[p].image && m_targets[i].output == m_outputs[o].image;
        if (found) continue;
        if (m_target_count == m_targets.size()) return false;
        vk::DescriptorSetAllocateInfo allocate {};
        allocate.descriptorPool = m_pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &m_set_layout[1];
        vk::DescriptorSet descriptor {};
        if (m_device.allocateDescriptorSets(&allocate, &descriptor) != vk::Result::eSuccess) return false;
        const std::array<vk::DescriptorImageInfo, 4> images {{
            {nullptr, m_present[p].view, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nullptr, m_outputs[o].view, vk::ImageLayout::eGeneral},
            {nullptr, m_mask.view, vk::ImageLayout::eGeneral},
            {nullptr, m_scratch.view, vk::ImageLayout::eGeneral}}};
        std::array<vk::WriteDescriptorSet, 4> writes {};
        for (uint32_t i = 0; i < writes.size(); ++i) {
            writes[i].dstSet = descriptor;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i < 2 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage;
            writes[i].pImageInfo = &images[i];
        }
        m_device.updateDescriptorSets(uint32_t(writes.size()), writes.data(), 0, nullptr);
        m_targets[m_target_count++] = {m_present[p].image, m_outputs[o].image, descriptor};
    }
    return true;
}

OcclusionGuard::Result OcclusionGuard::Record(vk::CommandBuffer command, vk::Image current_motion,
        vk::Image present, vk::Image output, uint64_t frame_id, uint64_t guest_frame, bool reset) {
    Result result {};
    result.history_frame_id = m_history_frame_id;
    result.history_guest_frame = m_history_guest_frame;
    vk::DescriptorSet target {};
    for (uint32_t i = 0; i < m_target_count; ++i)
        if (m_targets[i].present == present && m_targets[i].output == output) target = m_targets[i].set;
    if (!m_initialized || !command || !current_motion || !target || !frame_id || !guest_frame ||
        current_motion == m_motion[0].image || current_motion == m_motion[1].image ||
        current_motion == present || current_motion == output || present == output) {
        result.reason = "unregistered or invalid resources";
        InvalidateHistory();
        return result;
    }
    const bool adjacent = m_history_valid && m_history_frame_id != std::numeric_limits<uint64_t>::max() &&
        m_history_guest_frame != std::numeric_limits<uint64_t>::max() &&
        frame_id == m_history_frame_id + 1 && guest_frame == m_history_guest_frame + 1;
    const bool apply = !reset && adjacent;
    result.reason = reset ? "reset" : !m_history_valid ? "history warmup" : !adjacent ? "history discontinuity" : "applied";
    const uint32_t current_slot = m_history_slot ^ 1;
    auto& current = m_motion[current_slot];
    Barrier(command, current_motion, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            All, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            Transfer, vk::AccessFlagBits2::eTransferRead);
    Barrier(command, current.image, current.initialized ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, current.initialized ? All : vk::PipelineStageFlags2 {},
            current.initialized ? vk::AccessFlags2(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite) : vk::AccessFlags2 {},
            Transfer, vk::AccessFlagBits2::eTransferWrite);
    vk::ImageCopy copy {};
    copy.srcSubresource = copy.dstSubresource = ColorLayer;
    copy.extent = {m_render.width, m_render.height, 1};
    command.copyImage(current_motion, vk::ImageLayout::eTransferSrcOptimal,
                      current.image, vk::ImageLayout::eTransferDstOptimal, 1, &copy);
    Barrier(command, current_motion, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            Transfer, vk::AccessFlagBits2::eTransferRead, All, vk::AccessFlagBits2::eMemoryRead);
    Barrier(command, current.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            Transfer, vk::AccessFlagBits2::eTransferWrite, Compute, Read);
    current.initialized = true;
    m_history_slot = current_slot;
    m_history_frame_id = frame_id;
    m_history_guest_frame = guest_frame;
    m_history_valid = true;
    result.history_updated = true;
    if (!apply) return result;

    Barrier(command, m_mask.image, m_mask.initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, m_mask.initialized ? All : vk::PipelineStageFlags2 {},
            m_mask.initialized ? vk::AccessFlags2(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite) : vk::AccessFlags2 {},
            Transfer, vk::AccessFlagBits2::eTransferWrite);
    vk::ClearColorValue zero {};
    zero.uint32 = std::array<uint32_t, 4> {0, 0, 0, 0};
    command.clearColorImage(m_mask.image, vk::ImageLayout::eTransferDstOptimal, &zero, 1, &ColorRange);
    Barrier(command, m_mask.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
            Transfer, vk::AccessFlagBits2::eTransferWrite, Compute, Read | Write);
    const std::array<uint32_t, 4> push {m_render.width, m_render.height, m_display.width, m_display.height};
    command.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline[0]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipeline_layout[0], 0, 1, &m_mask_sets[current_slot], 0, nullptr);
    command.pushConstants(m_pipeline_layout[0], vk::ShaderStageFlagBits::eCompute, 0, 16, push.data());
    command.dispatch((m_render.width + 7) / 8, (m_render.height + 7) / 8, 1);
    Barrier(command, m_mask.image, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, Compute, Write, Compute, Read);
    Barrier(command, m_scratch.image, m_scratch.initialized ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eUndefined,
            vk::ImageLayout::eGeneral, m_scratch.initialized ? Transfer : vk::PipelineStageFlags2 {},
            m_scratch.initialized ? vk::AccessFlags2(vk::AccessFlagBits2::eTransferRead) : vk::AccessFlags2 {}, Compute, Write);
    Barrier(command, present, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            All, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite, Compute, Read);
    Barrier(command, output, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
            All, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite, Compute, Read);
    command.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline[1]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipeline_layout[1], 0, 1, &target, 0, nullptr);
    command.pushConstants(m_pipeline_layout[1], vk::ShaderStageFlagBits::eCompute, 0, 16, push.data());
    command.dispatch((m_display.width + 7) / 8, (m_display.height + 7) / 8, 1);
    Barrier(command, m_scratch.image, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
            Compute, Write, Transfer, vk::AccessFlagBits2::eTransferRead);
    Barrier(command, output, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
            Compute, Read, Transfer, vk::AccessFlagBits2::eTransferWrite);
    vk::ImageBlit blit {};
    blit.srcSubresource = blit.dstSubresource = ColorLayer;
    blit.srcOffsets[1] = blit.dstOffsets[1] = vk::Offset3D {int32_t(m_display.width), int32_t(m_display.height), 1};
    // A blit preserves RGBA components when the SDK's native output is BGRA.
    command.blitImage(m_scratch.image, vk::ImageLayout::eTransferSrcOptimal,
                      output, vk::ImageLayout::eTransferDstOptimal, 1, &blit, vk::Filter::eNearest);
    Barrier(command, output, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
            Transfer, vk::AccessFlagBits2::eTransferWrite, All,
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    m_mask.initialized = m_scratch.initialized = true;
    result.applied = true;
    return result;
}

void OcclusionGuard::InvalidateHistory() {
    m_history_valid = false;
    m_history_frame_id = m_history_guest_frame = 0;
}

void OcclusionGuard::Destroy() {
    if (m_device) {
        if (m_pool) m_device.destroyDescriptorPool(m_pool);
        for (auto pipeline : m_pipeline) if (pipeline) m_device.destroyPipeline(pipeline);
        for (auto layout : m_pipeline_layout) if (layout) m_device.destroyPipelineLayout(layout);
        for (auto layout : m_set_layout) if (layout) m_device.destroyDescriptorSetLayout(layout);
        for (auto& color : m_present) if (color.view) m_device.destroyImageView(color.view);
        for (auto& color : m_outputs) if (color.view) m_device.destroyImageView(color.view);
        for (Image* image : {&m_motion[0], &m_motion[1], &m_mask, &m_scratch}) {
            if (image->view) m_device.destroyImageView(image->view);
            if (image->image) vmaDestroyImage(m_allocator, static_cast<VkImage>(image->image), image->allocation);
        }
    }
    *this = OcclusionGuard {};
}
} // namespace Libs::Graphics::Gt7FrameGen

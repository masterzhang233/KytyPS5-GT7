#pragma once

#include "graphics/host_gpu/graphicContext.h"
#include <array>
#include <cstdint>

namespace Libs::Graphics::Gt7FrameGen {

// Experimental local current-frame fallback. No FFX dependency. All entrypoints
// are present-thread-only, under the caller's graphics-queue synchronization.
// Register/Initialize may allocate; Record never allocates, waits or creates objects.
class OcclusionGuard {
public:
    struct Result {
        bool applied = false;
        bool history_updated = false;
        uint64_t history_frame_id = 0;
        uint64_t history_guest_frame = 0;
        const char* reason = "not prepared";
    };

    // Register native SDR swapchain images before Generate can name them.
    bool RegisterPresent(GraphicContext& graphics, vk::Image image,
                         vk::Extent2D display, vk::Format format);
    // A different size/format requires Destroy after a checked GPU drain first.
    bool Initialize(GraphicContext& graphics, vk::Extent2D render,
                    vk::Extent2D display, vk::Format format);
    // Called outside Generate for the SDK's officially queried interpolation image.
    bool RegisterOutput(vk::Image image);
    [[nodiscard]] bool Matches(vk::Extent2D render, vk::Extent2D display, vk::Format format) const;
    [[nodiscard]] bool Initialized() const { return m_initialized; }
    // Caller validates source RG32F extent/usage and SDK color states/formats.
    // current_motion: SHADER_READ_ONLY; present: SHADER_READ_ONLY; output: GENERAL.
    // All external layouts are restored. The graphics queue is the only writer.
    Result Record(vk::CommandBuffer command, vk::Image current_motion,
                  vk::Image present, vk::Image output,
                  uint64_t frame_id, uint64_t guest_frame, bool reset);
    void InvalidateHistory();
    // Caller MUST check completion of all queued work using this helper first.
    void Destroy();
    [[nodiscard]] vk::Image MaskImage() const { return m_mask.image; } // Offline GPU validation.

private:
    struct Image {
        vk::Image image {};
        VmaAllocation allocation = nullptr;
        vk::ImageView view {};
        bool initialized = false;
    };
    struct Color { vk::Image image {}; vk::ImageView view {}; };
    struct Target {
        vk::Image present {};
        vk::Image output {};
        vk::DescriptorSet set {};
    };
    bool CreateImage(Image& image, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage);
    bool CreatePipelines();
    bool CreateTargets();
    bool CreateView(vk::Image image, vk::Format format, vk::ImageView& view);

    vk::Device m_device {};
    vk::PhysicalDevice m_physical {};
    VmaAllocator m_allocator = nullptr;
    vk::Extent2D m_render {}, m_display {};
    vk::Format m_format = vk::Format::eUndefined;
    // Two private slots avoid retaining presenter MV views after their owner retires.
    std::array<Image, 2> m_motion {};
    Image m_mask {}, m_scratch {};
    std::array<Color, 8> m_present {}, m_outputs {};
    uint32_t m_present_count = 0, m_output_count = 0;
    std::array<Target, 64> m_targets {};
    uint32_t m_target_count = 0;
    vk::DescriptorPool m_pool {};
    std::array<vk::DescriptorSetLayout, 2> m_set_layout {};
    std::array<vk::PipelineLayout, 2> m_pipeline_layout {};
    std::array<vk::Pipeline, 2> m_pipeline {};
    std::array<vk::DescriptorSet, 2> m_mask_sets {};
    uint64_t m_history_frame_id = 0, m_history_guest_frame = 0;
    uint32_t m_history_slot = 0;
    bool m_history_valid = false;
    bool m_initialized = false;
};

} // namespace Libs::Graphics::Gt7FrameGen

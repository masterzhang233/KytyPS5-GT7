#pragma once

#include "graphics/host_gpu/graphicContext.h"
#include "graphics/presentation/window/gt7FrameGenStatus.h"

#include <cstdint>

namespace Libs::Graphics::Gt7FrameGen {

// Private copies owned by one presenter frame. The GPU copy must precede its
// ready semaphore; keep the images alive until that frame's present completes.
// Depth is reversed device depth. Motion is current-to-previous displacement in
// render pixels, with projection jitter removed (motionVectorScale = 1, 1).
// The wrapper never owns or destroys these allocations.
struct FrameInputs {
    VulkanImage depth {};
    VulkanImage motion {};
    // Native-resolution, final tonemapped scene before the first HUD draw.
    // This is a same-guest-frame copy, not the DLSS HDR reconstruction output.
    // The presenter converts it exactly like the final image into its own
    // display-sized HUD-less image and passes that image to RecordPrepare.
    VulkanImage hudless_color {};
    vk::Extent2D render_extent {};
    uint64_t guest_frame = 0;
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
    float frame_time_ms = 0.0f;
    float near_plane = 0.0f;
    float far_plane = 0.0f;
    float fov_y = 0.0f;
    float camera_position[3] {};
    float camera_up[3] {};
    float camera_right[3] {};
    float camera_forward[3] {};
    bool depth_infinite = true;
    bool camera_valid = false;
    bool camera_pose_valid = false;
    bool reset = true;
    bool valid = false;
    bool hudless_valid = false;
    StatusReason rejection_reason = StatusReason::WaitingInputs;
};

// Opt-in KYTY_GT7_FSR_FG=1. Requested loads only the adjacent AMD runtime,
// once, before device creation; missing dependencies leave frame generation off.
[[nodiscard]] bool Requested();
void OnQueues(GraphicContext& graphics);
[[nodiscard]] bool OwnsSwapchain();
// True only after a recent successful, non-reset interpolation dispatch.
// Safe from the window/status thread; this does not alter the guest FPS count.
[[nodiscard]] bool IsGenerating();
// Snapshot of atomics only; also reports initialization failures when Requested
// returns false. Active/generated refer to dispatches, not measured display FPS.
[[nodiscard]] StatusSnapshot GetStatus();
// F10 diagnostic, opt-in KYTY_GT7_FSR_FG_CAPTURE_OUTPUTS=1. Thread-safe and
// nonblocking: request four non-reset presentColor/interpolation-output pairs,
// with same-frame HUD-less display color and private depth/motion SDK inputs.
// These are recorded before SDK presentation/UI composition, not proof of display.
void RequestOutputCapture();

// All swapchain calls are made on the present thread. QueuePresent must hold
// graphics.queue_mutex, as the runtime's game queue is graphics.queue. Creation,
// preparation, drain and destruction acquire that mutex internally; callers
// must not hold it when invoking those functions.
[[nodiscard]] vk::Result CreateSwapchain(GraphicContext& graphics,
                                        const vk::SwapchainCreateInfoKHR& info,
                                        vk::SwapchainKHR* swapchain);
// Drain interpolation/pacing work before freeing per-frame images or views.
void WaitForPresents(GraphicContext& graphics);
// Wait for the device/presenter to be idle before destroying the proxy.
void DestroySwapchain(GraphicContext& graphics, vk::SwapchainKHR swapchain);
[[nodiscard]] vk::Result GetSwapchainImages(vk::Device device, vk::SwapchainKHR swapchain,
                                           uint32_t* count, vk::Image* images);
[[nodiscard]] vk::Result AcquireNextImage(vk::Device device, vk::SwapchainKHR swapchain,
                                         uint64_t timeout, vk::Semaphore semaphore,
                                         uint32_t* index);
[[nodiscard]] vk::Result QueuePresent(vk::Queue queue, const vk::PresentInfoKHR& present);

// Records preparation on the present command buffer. Images arrive and leave
// shader-read-only. `hudless` must match the swapchain extent, format and color
// conversion, and correspond to inputs->hudless_color from this exact frame.
// It needs Sampled and TransferSrc usage: FSR copies it into temporal history.
// Its GPU lifetime extends through the generation submitted by QueuePresent,
// beyond the prepare submission's completion; retire it with a post-present
// queue signal. Null/invalid HUD-less, duplicate or unverified camera inputs
// disable interpolation for this present; the ordinary frame remains visible.
void RecordPrepare(GraphicContext& graphics, vk::CommandBuffer command,
                   const FrameInputs* inputs, vk::Rect2D region,
                   const VulkanImage* hudless = nullptr);
// A presenter frame was skipped: invalidate interpolation history, not guest time.
void NoteDroppedFrame();

} // namespace Libs::Graphics::Gt7FrameGen

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace Libs::Graphics::Vr {

struct Status {
    bool ok = false;
    std::string message;
    int runtime_error = 0;
    explicit operator bool() const { return ok; }
    static Status Success() { return {true, {}, 0}; }
    static Status Failure(std::string message, int code = 0) { return {false, std::move(message), code}; }
};

enum class TrackingOrigin { Seated, Standing };
enum class PoseSource { RuntimeTracking, GuestRendered };
struct Pose {
    // OpenVR coordinates: right handed, metres, +Y up, -Z forward. No guest axis conversion.
    float device_to_absolute[3][4] {};
    float position_m[3] {};
    float orientation_xyzw[4] {};
    float linear_velocity_m_s[3] {};
    float angular_velocity_rad_s[3] {};
    // Host steady-clock observation time, NOT a hardware sensor timestamp.
    uint64_t sample_monotonic_ns = 0;
    float prediction_seconds = 0;
    bool prediction_known = true; // WaitGetPoses prediction is compositor-controlled.
    int tracking_result = 0;
    bool connected = false;
    bool valid = false;
    PoseSource source = PoseSource::RuntimeTracking;
};

struct EyeGeometry {
    float eye_to_head[3][4] {};
    // Raw projection tangents returned by OpenVR; not degrees or a projection matrix.
    float left = 0, right = 0, top = 0, bottom = 0;
};
struct HeadsetInfo {
    bool connected = false;
    uint32_t recommended_eye_width = 0, recommended_eye_height = 0;
    float display_frequency_hz = 0;
    bool display_frequency_known = false;
    float ipd_m = 0;
    bool ipd_known = false;
    int activity_level = 0; // Does not imply a reliable physical proximity sensor.
    std::string model;
    std::array<EyeGeometry, 2> eyes;
};
struct DisplayTiming {
    float frequency_hz = 0;
    float seconds_since_last_vsync = 0;
    float seconds_from_vsync_to_photons = 0;
    float prediction_seconds = 0;
    uint64_t vsync_counter = 0;
    uint64_t sample_monotonic_ns = 0; // Observation clock, never a guest timestamp.
};

struct VulkanBinding {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
};
struct TextureBounds { float u_min = 0, v_min = 0, u_max = 1, v_max = 1; };
struct EyeTexture {
    VkImage image = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t samples = 1, mip_levels = 1, array_layers = 1;
    uint32_t array_index = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    TextureBounds bounds;
    // Gamma=true for encoded RGB; false for linear RGB. Must describe actual image contents.
    bool gamma = true;
};
enum class StereoSource { Unverified, GuestSeparateEyes, GuestSideBySide, GuestArrayLayers };
struct FramePose { uint64_t token = 0; Pose pose; };
struct SubmitResult {
    Status status;
    // Even a failed/partial Submit may have enqueued work. Caller must retire those copies.
    bool queue_may_have_work = false;
    bool left_accepted = false, right_accepted = false;
};

// Narrow injectable boundary. Production uses the official OpenVR SDK via the runtime DLL.
// Tests substitute only runtime I/O, and exercise the same Bridge implementation.
// Tracking calls are serialized with each other, but can run while the compositor
// waits/submits. Backends must protect shared event/property state at that boundary.
class RuntimeBackend {
public:
    virtual ~RuntimeBackend() = default;
    virtual Status Initialize(const std::filesystem::path&, TrackingOrigin) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual Status ReadHeadset(HeadsetInfo&) = 0;
    virtual Status ReadDisplayTiming(DisplayTiming&) = 0;
    virtual Status ReadPose(float prediction_seconds, Pose&) = 0;
    // One bridge critical section for a real pose and the corresponding eye
    // geometry. Production overrides this to poll once and reuse static data.
    virtual Status ReadTrackingSample(float prediction_seconds, Pose& pose, std::array<EyeGeometry, 2>& eyes) {
        pose = {}; eyes = {};
        HeadsetInfo headset;
        auto status = ReadHeadset(headset);
        if (!status || !headset.connected) return status ? Status::Failure("Headset disconnected") : status;
        status = ReadPose(prediction_seconds, pose);
        if (!status) { pose = {}; return status; }
        eyes = headset.eyes;
        return status;
    }
    virtual Status WaitPose(Pose&) = 0;
    virtual Status InstanceExtensions(std::vector<std::string>&) = 0;
    virtual Status OutputDevice(VkInstance, VkPhysicalDevice&) = 0;
    virtual Status DeviceExtensions(VkPhysicalDevice, std::vector<std::string>&) = 0;
    virtual Status SubmitEye(uint32_t eye, const VulkanBinding&, const EyeTexture&, const Pose*) = 0;
    virtual void Handoff() = 0;
    virtual void ClearFrame() = 0;
};

class OpenVrBridge {
public:
    OpenVrBridge();
    explicit OpenVrBridge(std::unique_ptr<RuntimeBackend>);
    ~OpenVrBridge();
    OpenVrBridge(const OpenVrBridge&) = delete;
    OpenVrBridge& operator=(const OpenVrBridge&) = delete;

    // Explicit absolute path to the locally installed runtime's openvr_api.dll. No DLL search.
    Status Initialize(const std::filesystem::path&, TrackingOrigin = TrackingOrigin::Seated);
    bool Active() const;
    Status ReadHeadset(HeadsetInfo&);
    Status ReadDisplayTiming(DisplayTiming&);
    Status ReadPredictedPose(float prediction_seconds, Pose&);
    Status ReadTrackingSample(float prediction_seconds, Pose&, std::array<EyeGeometry, 2>&);
    Status RequiredInstanceExtensions(std::vector<std::string>&);
    Status RequiredPhysicalDevice(VkInstance, VkPhysicalDevice&);
    Status RequiredDeviceExtensions(VkPhysicalDevice, std::vector<std::string>&);
    // Call after enabling ALL runtime extension requirements during Vulkan creation.
    // Checks runtime-required GPU again; rejects a different GPU rather than cross-device copying.
    Status AttachVulkan(const VulkanBinding&);

    // DEFAULT OpenVR timing: WaitGetPoses can access VkQueue too. Caller MUST hold
    // the bound queue's mutex for WaitFrame/SubmitStereo/PostPresentHandoff/Shutdown
    // after AttachVulkan (vr_queue_mutex for a dedicated queue, otherwise queue_mutex).
    // Never acquire renderer.GetMutex() while holding a queue mutex.
    Status WaitFrame(FramePose&);
    // Producer rendering and TRANSFER_SRC_OPTIMAL transitions must already be submitted
    // on binding.queue. These are real eye images, not a desktop image duplicated twice.
    // optional_render_pose is the actual pose used to render, if different from WaitFrame.
    SubmitResult SubmitStereo(const FramePose&, const EyeTexture&, const EyeTexture&,
                              StereoSource, const Pose* optional_render_pose = nullptr);
    Status PostPresentHandoff();
    // Stop all bridge callers first. Call BEFORE destroying submitted images, VkDevice or
    // VkInstance. Retire queue work even following Submit errors; this module owns no images.
    void Shutdown() noexcept;

private:
    std::unique_ptr<RuntimeBackend> m_backend;
    // Shared runtime lifetime spans every external call; shutdown/reinitialize
    // acquire it exclusively. Display waits never own the tracking lock.
    mutable std::shared_mutex m_lifetime_mutex;
    mutable std::mutex m_tracking_mutex;
    mutable std::mutex m_mutex;
    bool m_active = false, m_attached = false, m_frame_ready = false, m_handoff_pending = false;
    uint64_t m_next_token = 0, m_frame_token = 0;
    uint32_t m_diagnostic_submissions = 0; // Protected by m_mutex; bounded across reconnects.
    VulkanBinding m_binding;
};

OpenVrBridge& HostBridge();
// Exposed pure validation/math used by both backend and focused tests.
bool NormalizePose(Pose&);
// Constructs only a render-pose record from a proven guest RH metres/xyzw recipe.
// Does not claim a connected tracker or manufacture a runtime tracking result/timestamp.
bool MakeRenderPose(const float position[3], const float orientation[4], Pose& out);
// Magnitude of the relative rigid rotation (degrees), and distance between origins (metres).
// Diagnostic math only: does not adjust either submitted pose or image.
bool CalculatePoseDelta(const Pose& rendered, const Pose& predicted, float& rotation_degrees,
                        float& translation_metres);
bool CalculateDisplayTiming(DisplayTiming&);
Status ValidateStereo(const EyeTexture&, const EyeTexture&, StereoSource);
Status ParseExtensionList(const char* data, size_t length, std::vector<std::string>&);

} // namespace Libs::Graphics::Vr

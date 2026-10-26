#include "graphics/vr/openvrBridge.h"
#include "../../../3rdparty/openvr/openvr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Libs::Graphics::Vr {
namespace {
constexpr size_t MaxExtensionBytes = 65536;
uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool Finite(float v) { return std::isfinite(v); }
bool SubmissionDiagnosticsEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("KYTY_VR_DIAGNOSTICS");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
void ReportSubmission(uint32_t sequence, const FramePose& frame, const EyeTexture& left,
                      const EyeTexture& right, StereoSource source, const Pose* rendered,
                      const SubmitResult& result, uint64_t begin_ns, uint64_t end_ns) noexcept {
    // Formatting/logging failure must never alter the actual Submit result. No image readback.
    try {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9) << "VRSubmitMetadata {\"sequence\":" << sequence
            << ",\"token\":" << frame.token << ",\"source\":" << static_cast<int>(source)
            << ",\"submit_begin_ns\":" << begin_ns << ",\"submit_end_ns\":" << end_ns
            << ",\"wait_observed_ns\":" << frame.pose.sample_monotonic_ns
            << ",\"render_observed_ns\":" << (rendered ? rendered->sample_monotonic_ns : 0)
            << ",\"render_time_known\":" << (rendered && rendered->sample_monotonic_ns != 0)
            << ",\"left_accepted\":" << result.left_accepted
            << ",\"right_accepted\":" << result.right_accepted
            << ",\"runtime_error\":" << result.status.runtime_error << ",\"eyes\":[";
        auto number = [&](float value) { if (Finite(value)) out << value; else out << "null"; };
        auto eye = [&](const EyeTexture& image) {
            uint64_t handle = 0; static_assert(sizeof(image.image) <= sizeof(handle));
            std::memcpy(&handle, &image.image, sizeof(image.image));
            out << "{\"image\":" << handle << ",\"width\":" << image.width << ",\"height\":" << image.height
                << ",\"format\":" << static_cast<uint32_t>(image.format) << ",\"gamma\":" << image.gamma
                << ",\"layer\":" << image.array_index << ",\"layers\":" << image.array_layers
                << ",\"layout\":" << static_cast<uint32_t>(image.layout) << ",\"usage\":" << image.usage
                << ",\"bounds\":[" << image.bounds.u_min << ',' << image.bounds.v_min << ','
                << image.bounds.u_max << ',' << image.bounds.v_max << "]}";
        };
        eye(left); out << ','; eye(right); out << "],\"wait_pose\":[";
        auto matrix = [&](const Pose& pose) {
            for (int row=0; row<3; ++row) for (int col=0; col<4; ++col) {
                if (row != 0 || col != 0) out << ',';
                number(pose.device_to_absolute[row][col]);
            }
        };
        matrix(frame.pose); out << "],\"render_pose\":";
        if (rendered) { out << '['; matrix(*rendered); out << ']'; } else out << "null";
        float angle=0, distance=0;
        const bool delta_known = rendered && CalculatePoseDelta(*rendered,frame.pose,angle,distance);
        out << ",\"delta_known\":" << delta_known << ",\"delta_rotation_degrees\":";
        if (delta_known) out << angle; else out << "null";
        out << ",\"delta_translation_metres\":";
        if (delta_known) out << distance; else out << "null";
        out << "}\n";
        const auto line = out.str();
        std::fwrite(line.data(),1,line.size(),stdout);
    } catch (...) { /* Diagnostics cannot turn an accepted frame into a failure. */ }
}
bool BoundsValid(const TextureBounds& b) {
    return Finite(b.u_min) && Finite(b.v_min) && Finite(b.u_max) && Finite(b.v_max) &&
           b.u_min >= 0 && b.v_min >= 0 && b.u_max <= 1 && b.v_max <= 1 &&
           b.u_min < b.u_max && b.v_min < b.v_max;
}
bool Disjoint(const TextureBounds& a, const TextureBounds& b) {
    return a.u_max <= b.u_min || b.u_max <= a.u_min || a.v_max <= b.v_min || b.v_max <= a.v_min;
}
bool FormatSupported(VkFormat f) {
    return f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB ||
           f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB ||
           f == VK_FORMAT_R16G16B16A16_SFLOAT;
}
Status Unavailable() { return Status::Failure("SteamVR bridge is not initialized"); }
Status CompositorError(vr::EVRCompositorError e, const char* operation) {
    return e == vr::VRCompositorError_None ? Status::Success() :
        Status::Failure(std::string(operation) + " failed (OpenVR compositor error " +
                        std::to_string(static_cast<int>(e)) + ")", static_cast<int>(e));
}

class OpenVrRuntime final : public RuntimeBackend {
public:
    ~OpenVrRuntime() override { Shutdown(); }
    Status Initialize(const std::filesystem::path& dll, TrackingOrigin origin) override;
    void Shutdown() noexcept override;
    Status ReadHeadset(HeadsetInfo&) override;
    Status ReadDisplayTiming(DisplayTiming&) override;
    Status ReadPose(float, Pose&) override;
    Status ReadTrackingSample(float, Pose&, std::array<EyeGeometry, 2>&) override;
    Status WaitPose(Pose&) override;
    Status InstanceExtensions(std::vector<std::string>&) override;
    Status OutputDevice(VkInstance, VkPhysicalDevice&) override;
    Status DeviceExtensions(VkPhysicalDevice, std::vector<std::string>&) override;
    Status SubmitEye(uint32_t, const VulkanBinding&, const EyeTexture&, const Pose*) override;
    void Handoff() override { m_compositor->PostPresentHandoff(); }
    void ClearFrame() override { m_compositor->ClearLastSubmittedFrame(); }
private:
    Status CopyPose(const vr::TrackedDevicePose_t&, float prediction, Pose&);
    // Protect IVRSystem polling and our mutable caches. Release before entering
    // any blocking IVRCompositor call; runtime lifetime is held by the bridge.
    std::mutex m_info_mutex;
    Status PollConnection();
    Status RefreshHeadsetInfo();
    void InvalidateInfoCache() {
        m_headset_info_valid = false;
        m_display_properties_valid = false;
    }
    template<class F> Status Extensions(F&& query, std::vector<std::string>& out) {
        out.clear();
        const uint32_t n = query(nullptr, 0);
        if (n == 0 || n > MaxExtensionBytes) return Status::Failure("SteamVR returned no usable Vulkan extension list");
        std::vector<char> data(n, '\0');
        const uint32_t actual = query(data.data(), n);
        if (actual == 0 || actual > n) return Status::Failure("SteamVR Vulkan extension list changed while being queried");
        return ParseExtensionList(data.data(), actual, out);
    }
    vr::IVRSystem* m_system = nullptr;
    vr::IVRCompositor* m_compositor = nullptr;
    vr::ETrackingUniverseOrigin m_origin = vr::TrackingUniverseSeated;
    bool m_claimed = false, m_initialized = false, m_quitting = false;
    // Only static runtime properties are cached. Connection, tracking pose,
    // activity and vsync samples remain live. Every runtime event conservatively
    // invalidates these caches, including IPD/property/display-setting changes.
    HeadsetInfo m_headset_info;
    bool m_headset_info_valid = false, m_display_properties_valid = false;
    float m_display_frequency = 0, m_vsync_to_photons = 0;
#ifdef _WIN32
    HMODULE m_module = nullptr;
    using InitFn = uint32_t (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
    using ShutdownFn = void (VR_CALLTYPE*)();
    using InterfaceFn = void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*);
    using ErrorFn = const char* (VR_CALLTYPE*)(vr::EVRInitError);
    ShutdownFn m_shutdown = nullptr;
#endif
};
std::atomic<bool> g_runtime_claimed {false};

Status OpenVrRuntime::Initialize(const std::filesystem::path& dll, TrackingOrigin origin) {
#ifdef _WIN32
    if (m_initialized || m_module) return Status::Failure("OpenVR runtime already initialized");
    if (!dll.is_absolute() || dll.filename() != L"openvr_api.dll")
        return Status::Failure("An absolute path to the installed openvr_api.dll is required");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec) || ec)
        return Status::Failure("The installed SteamVR openvr_api.dll was not found");
    bool expected = false;
    if (!g_runtime_claimed.compare_exchange_strong(expected, true))
        return Status::Failure("This process already owns an OpenVR runtime session");
    m_claimed = true;
    m_module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m_module) { const auto code = GetLastError(); Shutdown(); return Status::Failure("Loading the installed OpenVR DLL failed", static_cast<int>(code)); }
    const auto init = reinterpret_cast<InitFn>(GetProcAddress(m_module, "VR_InitInternal2"));
    m_shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(m_module, "VR_ShutdownInternal"));
    const auto get_interface = reinterpret_cast<InterfaceFn>(GetProcAddress(m_module, "VR_GetGenericInterface"));
    const auto error_text = reinterpret_cast<ErrorFn>(GetProcAddress(m_module, "VR_GetVRInitErrorAsEnglishDescription"));
    if (!init || !m_shutdown || !get_interface) { Shutdown(); return Status::Failure("OpenVR DLL is missing required exports"); }
    vr::EVRInitError error = vr::VRInitError_None;
    const uint32_t token = init(&error, vr::VRApplication_Scene, nullptr);
    if (error != vr::VRInitError_None || token == 0) {
        const auto detail = error_text ? error_text(error) : nullptr;
        std::string message = std::string("SteamVR scene initialization failed: ") + (detail ? detail : "unknown runtime error");
        Shutdown(); return Status::Failure(std::move(message), static_cast<int>(error));
    }
    m_initialized = true;
    m_system = static_cast<vr::IVRSystem*>(get_interface(vr::IVRSystem_Version, &error));
    if (!m_system || error != vr::VRInitError_None) { Shutdown(); return Status::Failure("SteamVR does not provide IVRSystem_022", static_cast<int>(error)); }
    m_compositor = static_cast<vr::IVRCompositor*>(get_interface(vr::IVRCompositor_Version, &error));
    if (!m_compositor || error != vr::VRInitError_None) { Shutdown(); return Status::Failure("SteamVR does not provide IVRCompositor_028", static_cast<int>(error)); }
    m_origin = origin == TrackingOrigin::Seated ? vr::TrackingUniverseSeated : vr::TrackingUniverseStanding;
    m_compositor->SetTrackingSpace(m_origin);
    m_quitting = false;
    InvalidateInfoCache();
    auto status = PollConnection();
    if (!status) Shutdown();
    return status;
#else
    (void)dll; (void)origin;
    return Status::Failure("The experimental OpenVR host bridge is currently Windows-only");
#endif
}
void OpenVrRuntime::Shutdown() noexcept {
    InvalidateInfoCache();
#ifdef _WIN32
    if (m_initialized && m_shutdown) m_shutdown();
    m_initialized = false;
    m_system = nullptr; m_compositor = nullptr; m_quitting = false;
    m_shutdown = nullptr;
    if (m_module) { FreeLibrary(m_module); m_module = nullptr; }
    if (m_claimed) { g_runtime_claimed.store(false); m_claimed = false; }
#endif
}
Status OpenVrRuntime::PollConnection() {
    if (!m_system || !m_compositor) { InvalidateInfoCache(); return Unavailable(); }
    vr::VREvent_t event {};
    // Bound a noisy event queue; connection is checked independently below.
    for (int i = 0; i != 64 && m_system->PollNextEvent(&event, sizeof(event)); ++i) {
        InvalidateInfoCache();
        if (event.eventType == vr::VREvent_Quit) {
            m_system->AcknowledgeQuit_Exiting();
            m_quitting = true;
        }
    }
    if (m_quitting) return Status::Failure("SteamVR requested application shutdown");
    if (!m_system->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd)) {
        InvalidateInfoCache();
        return Status::Failure("No connected SteamVR headset; connect Quest 3 through Steam Link first");
    }
    return Status::Success();
}
Status OpenVrRuntime::ReadHeadset(HeadsetInfo& out) {
    std::scoped_lock info_lock(m_info_mutex);
    out = {};
    auto status = PollConnection(); if (!status) return status;
    status = RefreshHeadsetInfo(); if (!status) return status;
    out = m_headset_info;
    out.activity_level = static_cast<int>(m_system->GetTrackedDeviceActivityLevel(vr::k_unTrackedDeviceIndex_Hmd));
    return Status::Success();
}
Status OpenVrRuntime::RefreshHeadsetInfo() {
    if (m_headset_info_valid) return Status::Success();
    HeadsetInfo info; info.connected = true;
    m_system->GetRecommendedRenderTargetSize(&info.recommended_eye_width, &info.recommended_eye_height);
    if (info.recommended_eye_width == 0 || info.recommended_eye_height == 0)
        return Status::Failure("SteamVR returned an empty recommended eye target");
    vr::ETrackedPropertyError error;
    info.display_frequency_hz = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
    info.display_frequency_known = error == vr::TrackedProp_Success && Finite(info.display_frequency_hz) && info.display_frequency_hz > 0;
    if (!info.display_frequency_known) info.display_frequency_hz = 0;
    info.ipd_m = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float, &error);
    info.ipd_known = error == vr::TrackedProp_Success && Finite(info.ipd_m) && info.ipd_m > 0;
    if (!info.ipd_known) info.ipd_m = 0;
    char model[1024] {};
    const auto n = m_system->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String, model, sizeof(model), &error);
    const bool model_known = error == vr::TrackedProp_Success && n > 0 && n <= sizeof(model) && model[n-1] == '\0';
    if (model_known) info.model = model;
    for (uint32_t i = 0; i != 2; ++i) {
        auto& eye = info.eyes[i]; const auto which = static_cast<vr::EVREye>(i);
        auto matrix = m_system->GetEyeToHeadTransform(which);
        std::memcpy(eye.eye_to_head, matrix.m, sizeof(eye.eye_to_head));
        m_system->GetProjectionRaw(which, &eye.left, &eye.right, &eye.top, &eye.bottom);
        for (const auto& row : eye.eye_to_head) for (const auto value : row)
            if (!Finite(value)) return Status::Failure("SteamVR returned invalid eye transforms");
        if (!Finite(eye.left) || !Finite(eye.right) || !Finite(eye.top) || !Finite(eye.bottom) ||
            eye.left >= eye.right || eye.top >= eye.bottom)
            return Status::Failure("SteamVR returned invalid eye projection tangents");
    }
    m_headset_info = std::move(info);
    // Preserve the old retry behavior for temporarily unavailable optional
    // properties. A partial observation may be returned, but is not permanent.
    m_headset_info_valid = m_headset_info.display_frequency_known && m_headset_info.ipd_known && model_known;
    return Status::Success();
}
Status OpenVrRuntime::CopyPose(const vr::TrackedDevicePose_t& source, float prediction, Pose& out) {
    Pose result;
    result.connected = source.bDeviceIsConnected; result.valid = source.bPoseIsValid;
    result.tracking_result = static_cast<int>(source.eTrackingResult);
    result.prediction_seconds = prediction; result.sample_monotonic_ns = NowNs();
    std::memcpy(result.device_to_absolute, source.mDeviceToAbsoluteTracking.m, sizeof(result.device_to_absolute));
    std::memcpy(result.linear_velocity_m_s, source.vVelocity.v, sizeof(result.linear_velocity_m_s));
    std::memcpy(result.angular_velocity_rad_s, source.vAngularVelocity.v, sizeof(result.angular_velocity_rad_s));
    if (!NormalizePose(result)) { out = {}; return Status::Failure("SteamVR headset pose is disconnected, untracked or invalid"); }
    out = result; return Status::Success();
}
Status OpenVrRuntime::ReadDisplayTiming(DisplayTiming& out) {
    std::scoped_lock info_lock(m_info_mutex);
    out = {}; auto status = PollConnection(); if (!status) return status;
    DisplayTiming value;
    if (!m_display_properties_valid) {
        vr::ETrackedPropertyError error;
        const float frequency = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
        if (error != vr::TrackedProp_Success) return Status::Failure("SteamVR display frequency is unavailable");
        const float photons = m_system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float, &error);
        if (error != vr::TrackedProp_Success) return Status::Failure("SteamVR vsync-to-photons timing is unavailable");
        if (!Finite(frequency) || frequency <= 0 || !Finite(photons) || photons < 0)
            return Status::Failure("SteamVR display timing properties are invalid");
        m_display_frequency = frequency; m_vsync_to_photons = photons;
        m_display_properties_valid = true;
    }
    value.frequency_hz = m_display_frequency;
    value.seconds_from_vsync_to_photons = m_vsync_to_photons;
    if (!m_system->GetTimeSinceLastVsync(&value.seconds_since_last_vsync, &value.vsync_counter))
        return Status::Failure("SteamVR has no usable vsync sample");
    value.sample_monotonic_ns = NowNs();
    if (!CalculateDisplayTiming(value)) return Status::Failure(
        "SteamVR display timing is non-finite, stale or out of range: hz=" + std::to_string(value.frequency_hz) +
        " since_vsync=" + std::to_string(value.seconds_since_last_vsync) +
        " vsync_to_photons=" + std::to_string(value.seconds_from_vsync_to_photons) +
        " vsync_counter=" + std::to_string(value.vsync_counter));
    out = value; return Status::Success();
}
Status OpenVrRuntime::ReadPose(float prediction, Pose& out) {
    std::scoped_lock info_lock(m_info_mutex);
    out = {}; auto status = PollConnection(); if (!status) return status;
    vr::TrackedDevicePose_t pose {};
    m_system->GetDeviceToAbsoluteTrackingPose(m_origin, prediction, &pose, 1);
    return CopyPose(pose, prediction, out);
}
Status OpenVrRuntime::ReadTrackingSample(float prediction, Pose& out, std::array<EyeGeometry, 2>& eyes) {
    std::scoped_lock info_lock(m_info_mutex);
    out = {}; eyes = {};
    auto status = PollConnection(); if (!status) return status;
    status = RefreshHeadsetInfo(); if (!status) return status;
    vr::TrackedDevicePose_t pose {};
    m_system->GetDeviceToAbsoluteTrackingPose(m_origin, prediction, &pose, 1);
    status = CopyPose(pose, prediction, out);
    if (!status) return status;
    // No model string or activity query is needed by the per-frame tracker.
    eyes = m_headset_info.eyes;
    return Status::Success();
}
Status OpenVrRuntime::WaitPose(Pose& out) {
    out = {};
    {
        std::scoped_lock info_lock(m_info_mutex);
        auto status = PollConnection(); if (!status) return status;
    }
    vr::TrackedDevicePose_t pose {};
    auto status = CompositorError(m_compositor->WaitGetPoses(&pose, 1, nullptr, 0), "WaitGetPoses");
    if (!status) return status;
    status = CopyPose(pose, 0, out);
    if (status) out.prediction_known = false;
    return status;
}
Status OpenVrRuntime::InstanceExtensions(std::vector<std::string>& out) {
    return Extensions([&](char* p, uint32_t n) { return m_compositor->GetVulkanInstanceExtensionsRequired(p,n); }, out);
}
Status OpenVrRuntime::OutputDevice(VkInstance instance, VkPhysicalDevice& out) {
    std::scoped_lock info_lock(m_info_mutex);
    out = VK_NULL_HANDLE;
    uint64_t raw = 0; m_system->GetOutputDevice(&raw, vr::TextureType_Vulkan, instance);
    if (!raw) return Status::Failure("SteamVR did not identify a Vulkan output device");
    out = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(raw)); return Status::Success();
}
Status OpenVrRuntime::DeviceExtensions(VkPhysicalDevice device, std::vector<std::string>& out) {
    return Extensions([&](char* p, uint32_t n) { return m_compositor->GetVulkanDeviceExtensionsRequired(device,p,n); }, out);
}
Status OpenVrRuntime::SubmitEye(uint32_t eye, const VulkanBinding& b, const EyeTexture& image, const Pose* pose) {
    {
        std::scoped_lock info_lock(m_info_mutex);
        auto status = PollConnection(); if (!status) return status;
    }
    vr::VRVulkanTextureArrayData_t data {};
    static_assert(sizeof(data.m_nImage) == sizeof(image.image));
    std::memcpy(&data.m_nImage, &image.image, sizeof(image.image));
    data.m_pInstance = b.instance; data.m_pPhysicalDevice = b.physical_device;
    data.m_pDevice = b.device; data.m_pQueue = b.queue; data.m_nQueueFamilyIndex = b.queue_family;
    data.m_nWidth = image.width; data.m_nHeight = image.height;
    data.m_nFormat = static_cast<uint32_t>(image.format); data.m_nSampleCount = image.samples;
    data.m_unArrayIndex = image.array_index; data.m_unArraySize = image.array_layers;
    vr::VRTextureWithPose_t texture {};
    texture.handle = &data; texture.eType = vr::TextureType_Vulkan;
    texture.eColorSpace = image.gamma ? vr::ColorSpace_Gamma : vr::ColorSpace_Linear;
    if (pose) std::memcpy(texture.mDeviceToAbsoluteTracking.m, pose->device_to_absolute, sizeof(pose->device_to_absolute));
    vr::VRTextureBounds_t bounds {image.bounds.u_min,image.bounds.v_min,image.bounds.u_max,image.bounds.v_max};
    const auto flags = static_cast<vr::EVRSubmitFlags>(
        (pose ? vr::Submit_TextureWithPose : vr::Submit_Default) |
        (image.array_layers > 1 ? vr::Submit_VulkanTextureWithArrayData : vr::Submit_Default));
    return CompositorError(m_compositor->Submit(static_cast<vr::EVREye>(eye), &texture, &bounds,
        flags), eye == 0 ? "Submit(left)" : "Submit(right)");
}
} // namespace

bool NormalizePose(Pose& p) {
    // A rotation-only fallback must not be advertised as tracked six-degree-of-freedom pose.
    if (!p.valid || (p.source != PoseSource::RuntimeTracking && p.source != PoseSource::GuestRendered)) return false;
    if (p.source == PoseSource::RuntimeTracking && (!p.connected || p.tracking_result != vr::TrackingResult_Running_OK)) return false;
    for (const auto& row : p.device_to_absolute) for (const auto value : row) if (!Finite(value)) return false;
    for (float v : p.linear_velocity_m_s) if (!Finite(v)) return false;
    for (float v : p.angular_velocity_rad_s) if (!Finite(v)) return false;
    const auto& m = p.device_to_absolute;
    // Reject malformed/reflected matrices, not merely NaNs, before quaternion conversion.
    for (int i=0;i<3;++i) for (int j=i;j<3;++j) {
        float dot=0; for (int k=0;k<3;++k) dot += m[i][k]*m[j][k];
        if (std::abs(dot - (i==j ? 1.0f : 0.0f)) > 0.02f) return false;
    }
    const float det = m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) -
        m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (std::abs(det-1.0f)>0.03f) return false;
    float x,y,z,w;
    const float trace=m[0][0]+m[1][1]+m[2][2];
    if (trace>0) { const float s=std::sqrt(trace+1.0f)*2; w=.25f*s; x=(m[2][1]-m[1][2])/s; y=(m[0][2]-m[2][0])/s; z=(m[1][0]-m[0][1])/s; }
    else if (m[0][0]>m[1][1] && m[0][0]>m[2][2]) { const float s=std::sqrt(1+m[0][0]-m[1][1]-m[2][2])*2; w=(m[2][1]-m[1][2])/s; x=.25f*s; y=(m[0][1]+m[1][0])/s; z=(m[0][2]+m[2][0])/s; }
    else if (m[1][1]>m[2][2]) { const float s=std::sqrt(1+m[1][1]-m[0][0]-m[2][2])*2; w=(m[0][2]-m[2][0])/s; x=(m[0][1]+m[1][0])/s; y=.25f*s; z=(m[1][2]+m[2][1])/s; }
    else { const float s=std::sqrt(1+m[2][2]-m[0][0]-m[1][1])*2; w=(m[1][0]-m[0][1])/s; x=(m[0][2]+m[2][0])/s; y=(m[1][2]+m[2][1])/s; z=.25f*s; }
    const float len=std::sqrt(x*x+y*y+z*z+w*w);
    if (!Finite(len) || len<0.001f) return false;
    p.orientation_xyzw[0]=x/len; p.orientation_xyzw[1]=y/len; p.orientation_xyzw[2]=z/len; p.orientation_xyzw[3]=w/len;
    for(int i=0;i<3;++i) p.position_m[i]=m[i][3];
    return true;
}
bool MakeRenderPose(const float position[3], const float orientation[4], Pose& out) {
    out={}; if(!position || !orientation) return false;
    for(int i=0;i<3;++i) if(!Finite(position[i])) return false;
    float length2=0;
    for(int i=0;i<4;++i) { if(!Finite(orientation[i])) return false; length2+=orientation[i]*orientation[i]; }
    if(!Finite(length2) || std::abs(length2-1.0f)>.02f) return false;
    const float inv=1.0f/std::sqrt(length2);
    const float x=orientation[0]*inv,y=orientation[1]*inv,z=orientation[2]*inv,w=orientation[3]*inv;
    Pose p; p.valid=true;p.source=PoseSource::GuestRendered;p.prediction_known=false;
    p.device_to_absolute[0][0]=1-2*(y*y+z*z);p.device_to_absolute[0][1]=2*(x*y-z*w);p.device_to_absolute[0][2]=2*(x*z+y*w);
    p.device_to_absolute[1][0]=2*(x*y+z*w);p.device_to_absolute[1][1]=1-2*(x*x+z*z);p.device_to_absolute[1][2]=2*(y*z-x*w);
    p.device_to_absolute[2][0]=2*(x*z-y*w);p.device_to_absolute[2][1]=2*(y*z+x*w);p.device_to_absolute[2][2]=1-2*(x*x+y*y);
    for(int i=0;i<3;++i)p.device_to_absolute[i][3]=position[i];
    if(!NormalizePose(p))return false;
    out=p;return true;
}
bool CalculatePoseDelta(const Pose& rendered, const Pose& predicted, float& angle, float& distance) {
    angle=0; distance=0;
    Pose a=rendered,b=predicted;
    if (!NormalizePose(a) || !NormalizePose(b)) return false;
    // trace(Ra^T Rb) equals the Frobenius inner product; stable for arbitrary axes.
    double trace=0, separation2=0;
    for (int row=0;row<3;++row) {
        for (int col=0;col<3;++col) trace+=double(a.device_to_absolute[row][col])*b.device_to_absolute[row][col];
        const double d=double(a.position_m[row])-b.position_m[row]; separation2+=d*d;
    }
    angle=static_cast<float>(std::acos(std::clamp((trace-1.0)*.5,-1.0,1.0))*180.0/3.14159265358979323846);
    distance=static_cast<float>(std::sqrt(separation2));
    return Finite(angle) && Finite(distance);
}
bool CalculateDisplayTiming(DisplayTiming& t) {
    t.prediction_seconds = 0;
    if (!Finite(t.frequency_hz) || t.frequency_hz <= 0 ||
        !Finite(t.seconds_since_last_vsync) ||
        !Finite(t.seconds_from_vsync_to_photons) || t.seconds_from_vsync_to_photons < 0) return false;
    // Same signed next-frame prediction as Valve's sample. A virtual display can offset
    // its vsync into the future (observed on Steam Link); do not clamp this value to zero.
    // https://github.com/ValveSoftware/openvr/wiki/IVRSystem::GetDeviceToAbsoluteTrackingPose
    // Do not invent 90 Hz or modulo a stale sample. Validate the resulting horizon instead.
    const float prediction = 1.0f / t.frequency_hz - t.seconds_since_last_vsync + t.seconds_from_vsync_to_photons;
    if (!Finite(prediction) || prediction < 0 || prediction > .25f) return false;
    t.prediction_seconds = prediction; return true;
}
Status ParseExtensionList(const char* data, size_t length, std::vector<std::string>& out) {
    out.clear();
    if (!data || length==0 || length>MaxExtensionBytes || data[length-1]!='\0') return Status::Failure("Invalid Vulkan extension list length or termination");
    if (std::memchr(data, '\0', length-1)) return Status::Failure("Embedded NUL in Vulkan extension list");
    size_t i=0;
    while(i+1<length) {
        if(data[i]==' ') { ++i; continue; }
        const size_t start=i;
        while(i+1<length && data[i]!=' ') {
            const char c=data[i];
            if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return Status::Failure("Invalid character in Vulkan extension name");
            ++i;
        }
        const std::string value(data+start,i-start);
        if(value.size()<4 || value.size()>255 || value.rfind("VK_",0)!=0) return Status::Failure("Invalid Vulkan extension name");
        if(std::find(out.begin(),out.end(),value)==out.end()) out.push_back(value);
    }
    return Status::Success();
}
Status ValidateStereo(const EyeTexture& left, const EyeTexture& right, StereoSource source) {
    if(source!=StereoSource::GuestSeparateEyes && source!=StereoSource::GuestSideBySide && source!=StereoSource::GuestArrayLayers)
        return Status::Failure("Unverified stereo source; desktop duplication is not VR");
    for(const auto* e : {&left,&right}) {
        if(e->image==VK_NULL_HANDLE || e->width==0 || e->height==0 || !FormatSupported(e->format) ||
           e->samples!=1 || e->mip_levels!=1 || e->array_layers==0 || e->array_layers>2048 || e->array_index>=e->array_layers ||
           e->layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
           (e->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) !=
             (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) || !BoundsValid(e->bounds))
            return Status::Failure("Invalid eye image: require valid layer, single mip/sample, TRANSFER_SRC_OPTIMAL and finite bounds");
    }
    if(source==StereoSource::GuestSeparateEyes && left.image==right.image)
        return Status::Failure("Separate-eye source must contain two different images");
    if(source==StereoSource::GuestSideBySide &&
       (left.image!=right.image || left.width!=right.width || left.height!=right.height ||
        left.format!=right.format || left.gamma!=right.gamma || left.array_layers!=right.array_layers ||
        left.array_index!=right.array_index || !Disjoint(left.bounds,right.bounds)))
        return Status::Failure("Shared eye atlas must use matching image metadata and disjoint eye bounds");
    if(source==StereoSource::GuestArrayLayers &&
       (left.image!=right.image || left.width!=right.width || left.height!=right.height ||
        left.format!=right.format || left.gamma!=right.gamma || left.array_layers!=right.array_layers ||
        left.array_index==right.array_index))
        return Status::Failure("Array-eye source must use matching image metadata and different array layers");
    return Status::Success();
}

OpenVrBridge::OpenVrBridge(): OpenVrBridge(std::make_unique<OpenVrRuntime>()) {}
OpenVrBridge::OpenVrBridge(std::unique_ptr<RuntimeBackend> backend): m_backend(std::move(backend)) {}
OpenVrBridge::~OpenVrBridge() { Shutdown(); }
Status OpenVrBridge::Initialize(const std::filesystem::path& path, TrackingOrigin origin) {
    std::unique_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex);
    if(m_active) return Status::Failure("SteamVR bridge already initialized");
    if(!m_backend) return Status::Failure("No OpenVR runtime backend");
    auto result=m_backend->Initialize(path,origin);
    if(!result) { m_backend->Shutdown(); return result; }
    m_active=true; return result;
}
bool OpenVrBridge::Active() const { std::shared_lock lifetime(m_lifetime_mutex); return m_active; }
Status OpenVrBridge::ReadHeadset(HeadsetInfo& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_tracking_mutex); out={}; if(!m_active) return Unavailable();
    auto result=m_backend->ReadHeadset(out); if(!result) out={}; return result;
}
Status OpenVrBridge::ReadPredictedPose(float seconds, Pose& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_tracking_mutex); out={};
    if(!m_active) return Unavailable();
    if(!Finite(seconds)||seconds<0||seconds>.25f) return Status::Failure("Pose prediction must be finite and between 0 and 0.25 seconds");
    auto result=m_backend->ReadPose(seconds,out);
    if(!result || out.source!=PoseSource::RuntimeTracking || !NormalizePose(out)) { out={}; return result ? Status::Failure("Runtime returned an invalid pose") : result; }
    return result;
}
Status OpenVrBridge::ReadTrackingSample(float seconds, Pose& out, std::array<EyeGeometry, 2>& eyes) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_tracking_mutex); out = {}; eyes = {};
    if (!m_active) return Unavailable();
    if (!Finite(seconds) || seconds < 0 || seconds > .25f)
        return Status::Failure("Pose prediction must be finite and between 0 and 0.25 seconds");
    auto result = m_backend->ReadTrackingSample(seconds, out, eyes);
    if (!result || out.source != PoseSource::RuntimeTracking || !NormalizePose(out)) {
        out = {}; eyes = {};
        return result ? Status::Failure("Runtime returned an invalid pose") : result;
    }
    return result;
}
Status OpenVrBridge::ReadDisplayTiming(DisplayTiming& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_tracking_mutex); out={}; if(!m_active) return Unavailable();
    auto result=m_backend->ReadDisplayTiming(out);
    if(!result || !CalculateDisplayTiming(out)) { out={}; return result ? Status::Failure("Runtime returned invalid display timing") : result; }
    return result;
}
Status OpenVrBridge::RequiredInstanceExtensions(std::vector<std::string>& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); out.clear(); if(!m_active) return Unavailable(); return m_backend->InstanceExtensions(out);
}
Status OpenVrBridge::RequiredPhysicalDevice(VkInstance instance, VkPhysicalDevice& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); out=VK_NULL_HANDLE; if(!m_active) return Unavailable();
    if(instance==VK_NULL_HANDLE) return Status::Failure("Vulkan instance is required for SteamVR GPU matching");
    return m_backend->OutputDevice(instance,out);
}
Status OpenVrBridge::RequiredDeviceExtensions(VkPhysicalDevice device, std::vector<std::string>& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); out.clear(); if(!m_active) return Unavailable();
    if(device==VK_NULL_HANDLE) return Status::Failure("Physical device is required for SteamVR extensions");
    return m_backend->DeviceExtensions(device,out);
}
Status OpenVrBridge::AttachVulkan(const VulkanBinding& binding) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); if(!m_active) return Unavailable();
    if(m_attached) return Status::Failure("SteamVR Vulkan binding is already attached; shut down before rebinding");
    if(!binding.instance||!binding.physical_device||!binding.device||!binding.queue||binding.queue_family==UINT32_MAX)
        return Status::Failure("Incomplete SteamVR Vulkan binding");
    VkPhysicalDevice expected=VK_NULL_HANDLE; auto result=m_backend->OutputDevice(binding.instance,expected);
    if(!result) return result;
    if(!expected || expected!=binding.physical_device) return Status::Failure("Emulator GPU does not match the SteamVR output GPU");
    m_binding=binding; m_attached=true; return Status::Success();
}
Status OpenVrBridge::WaitFrame(FramePose& out) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); out={}; m_frame_ready=false;
    if(!m_active||!m_attached) return Status::Failure("SteamVR frame requires an attached Vulkan device");
    // Hand off a completed/partially submitted old frame before beginning another.
    if(m_handoff_pending) { m_backend->Handoff(); m_handoff_pending=false; }
    Pose pose; auto result=m_backend->WaitPose(pose);
    if(!result || pose.source!=PoseSource::RuntimeTracking || !NormalizePose(pose)) return result ? Status::Failure("WaitGetPoses returned an invalid pose") : result;
    if(m_next_token==UINT64_MAX) return Status::Failure("SteamVR frame token exhausted");
    m_frame_token=++m_next_token; m_frame_ready=true;
    out.token=m_frame_token; out.pose=pose; return Status::Success();
}
SubmitResult OpenVrBridge::SubmitStereo(const FramePose& frame, const EyeTexture& left,
                                      const EyeTexture& right, StereoSource source, const Pose* render_pose) {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); SubmitResult result;
    if(!m_active||!m_attached||!m_frame_ready||frame.token==0||frame.token!=m_frame_token) {
        result.status=Status::Failure("Missing, stale or already submitted SteamVR frame"); return result;
    }
    result.status=ValidateStereo(left,right,source); if(!result.status) return result;
    Pose checked;
    if(render_pose) { checked=*render_pose; if(!NormalizePose(checked)) { result.status=Status::Failure("Invalid actual render pose"); return result; } }
    // Consume once before entering external runtime: partial failure cannot resubmit an eye.
    m_frame_ready=false; m_handoff_pending=true; result.queue_may_have_work=true;
    const bool diagnostic = m_diagnostic_submissions < 32 && SubmissionDiagnosticsEnabled();
    const uint64_t begin_ns = diagnostic ? NowNs() : 0;
    if (diagnostic) ++m_diagnostic_submissions;
    result.status=m_backend->SubmitEye(0,m_binding,left,render_pose ? &checked : nullptr);
    result.left_accepted=bool(result.status);
    if(result.status) {
        result.status=m_backend->SubmitEye(1,m_binding,right,render_pose ? &checked : nullptr);
        result.right_accepted=bool(result.status);
    }
    if(!result.status) m_backend->ClearFrame();
    if (diagnostic) ReportSubmission(m_diagnostic_submissions,frame,left,right,source,
                                    render_pose ? &checked : nullptr,result,begin_ns,NowNs());
    return result;
}
Status OpenVrBridge::PostPresentHandoff() {
    std::shared_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex); if(!m_active||!m_attached) return Unavailable();
    if(m_handoff_pending) { m_backend->Handoff(); m_handoff_pending=false; }
    return Status::Success();
}
void OpenVrBridge::Shutdown() noexcept {
    std::unique_lock lifetime(m_lifetime_mutex);
    std::scoped_lock lock(m_mutex);
    if(m_backend) m_backend->Shutdown();
    m_active=false; m_attached=false; m_frame_ready=false; m_handoff_pending=false;
    m_frame_token=0; m_binding={};
    // Do not reset next_token: a stale token must not match after reconnect.
}
OpenVrBridge& HostBridge() { static OpenVrBridge bridge; return bridge; }
} // namespace Libs::Graphics::Vr

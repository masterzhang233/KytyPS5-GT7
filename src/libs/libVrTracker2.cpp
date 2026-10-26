#include "libs/libVrTracker2.h"

#include "common/emulatorConfig.h"
#include "graphics/vr/openvrBridge.h"
#include "kernel/pthread.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Libs {

LIB_VERSION("VrTracker2", 1, "VrTracker2", 1, 1);

namespace VrTracker2 {
namespace {
std::mutex g_mutex;
bool g_initialized = false;
int32_t g_registered_handle = -1;
uint32_t g_coordinate_system = 0;
std::atomic<uint32_t> g_diagnostics {0};

bool CopyFromGuest(const void* address, void* output, size_t size) {
    if (!address || !output || !size || reinterpret_cast<uintptr_t>(address) > UINTPTR_MAX - size)
        return false;
#if defined(_WIN32)
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &copied) && copied == size;
#elif defined(__linux__)
    iovec local {output, size}, remote {const_cast<void*>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#else
    return false;
#endif
}

bool WritableGuestSpan(void* address, size_t size) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    if (!begin || !size || begin > UINTPTR_MAX - size) return false;
#if defined(_WIN32)
    uintptr_t at = begin;
    while (at < begin + size) {
        MEMORY_BASIC_INFORMATION info {};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD mode = info.Protect & 0xff;
        if (mode != PAGE_READWRITE && mode != PAGE_WRITECOPY &&
            mode != PAGE_EXECUTE_READWRITE && mode != PAGE_EXECUTE_WRITECOPY) return false;
        const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (base > UINTPTR_MAX - info.RegionSize || base + info.RegionSize <= at) return false;
        at = base + info.RegionSize;
    }
    return true;
#elif defined(__linux__)
    // process_vm_writev itself enforces writable mappings, including partial spans.
    return true;
#else
    return false;
#endif
}

bool CopyToGuest(void* address, const void* input, size_t size) {
    if (!input || !WritableGuestSpan(address, size)) return false;
#if defined(_WIN32)
    // Unlike naked memcpy, this fails on an unmapped destination. Preflight is
    // necessary because WriteProcessMemory can otherwise bypass read-only pages.
    // A concurrent map change is not made atomic by this OS-leaf API.
    SIZE_T copied = 0;
    return WriteProcessMemory(GetCurrentProcess(), address, input, size, &copied) && copied == size;
#elif defined(__linux__)
    iovec local {const_cast<void*>(input), size}, remote {address, size};
    return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#else
    return false;
#endif
}

void* OutputField(void* output, size_t offset) {
    const auto address = reinterpret_cast<uintptr_t>(output);
    if (!address || address > UINTPTR_MAX - offset) return nullptr;
    return reinterpret_cast<void*>(address + offset);
}

bool WriteConsumerPoses(void* output, const ConsumerPose& head,
                        const std::array<ConsumerPose, 2>& eyes) {
    const std::array<size_t, 4> offsets {0x60, 0xa0, 0xe0, 0x120};
    const std::array<const ConsumerPose*, 4> poses {&head, &eyes[0], &eyes[1], &head};
    if (!WritableGuestSpan(OutputField(output, 0x18), sizeof(uint32_t))) return false;
    for (auto offset : offsets) {
        if (!WritableGuestSpan(OutputField(output, offset), 3 * sizeof(float)) ||
            !WritableGuestSpan(OutputField(output, offset + 0x10), 4 * sizeof(float))) return false;
    }
    const uint32_t invalid = 0, valid = 1;
    if (!CopyToGuest(OutputField(output, 0x18), &invalid, sizeof(invalid))) return false;
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (!CopyToGuest(OutputField(output, offsets[i]), poses[i]->position.data(), 3 * sizeof(float)) ||
            !CopyToGuest(OutputField(output, offsets[i] + 0x10), poses[i]->orientation.data(), 4 * sizeof(float)))
            return false;
    }
    // GT7 tests precisely ==1 for tracking-valid, and !=0 for tracked. Publish
    // validity last; unknown SDK fields and padding are never overwritten.
    return CopyToGuest(OutputField(output, 0x18), &valid, sizeof(valid));
}

void Report(const char* action, const char* reason, uint64_t a = 0, uint64_t b = 0) {
    if (g_diagnostics.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    std::fprintf(stderr, "VrTracker2: %s: %s a=0x%llx b=0x%llx\n", action, reason,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
}
} // namespace

static int32_t KYTY_SYSV_ABI TrackerQueryMemory(const MemoryQueryParameter* parameter,
                                              MemoryRequirement* requirement) {
    MemoryQueryParameter input {};
    MemoryRequirement output {};
    if (!CopyFromGuest(parameter, &input, sizeof(input))) return ERROR_INVALID_ARGUMENT;
    auto result = QueryMemory(&input, &output);
    if (result == 0 && !CopyToGuest(requirement, &output, sizeof(output))) result = ERROR_INVALID_ARGUMENT;
    Report("QueryMemory", result == 0 ? "validated 32-byte requirement" : "invalid argument");
    return result;
}

static int32_t KYTY_SYSV_ABI TrackerInitialize(const InitializeParameter* parameter) {
    InitializeParameter input {};
    if (!CopyFromGuest(parameter, &input, sizeof(input))) return ERROR_INVALID_ARGUMENT;
    if (!ValidInitialize(input)) {
        Report("Initialize", "invalid 72-byte parameter", input.size, input.work_address);
        return ERROR_INVALID_ARGUMENT;
    }
    Graphics::Vr::HeadsetInfo headset;
    const auto status = Graphics::Vr::HostBridge().ReadHeadset(headset);
    if (!Config::VrEnabled() || !status || !headset.connected) {
        Report("Initialize", "no connected real host headset");
        return ERROR_NOT_INITIALIZED;
    }
    std::lock_guard lock(g_mutex);
    // No writes to the caller's work buffer: it belongs to the guest. The host
    // adapter owns its own state and has no dependency on console work memory.
    g_initialized = true;
    g_registered_handle = -1;
    g_coordinate_system = 0;
    Report("Initialize", "host connected; GT7 consumer pose adapter enabled",
           input.work_bytes, input.work_alignment);
    return 0;
}

static int32_t KYTY_SYSV_ABI TrackerSetCoordinateSystem(uint32_t coordinate_system) {
    std::lock_guard lock(g_mutex);
    if (!g_initialized) return ERROR_NOT_INITIALIZED;
    // Only GT7's observed selector is supported. This compatibility adapter
    // maps its proved camera/eye consumers to OpenVR's right-handed metres;
    // it does not claim the complete SDK coordinate-system enumeration.
    if (coordinate_system != 1) return ERROR_INVALID_ARGUMENT;
    g_coordinate_system = coordinate_system;
    Report("SetCoordinateSystem", "recorded GT7 coordinate-system selector", coordinate_system);
    return 0;
}

static int32_t KYTY_SYSV_ABI TrackerRegisterDevice(uint32_t device_type, int32_t device_handle) {
    std::lock_guard lock(g_mutex);
    if (!g_initialized) return ERROR_NOT_INITIALIZED;
    // GT7 passes type 0 and the successful Hmd2Open handle (RVA 0x1a41076).
    if (device_type != 0 || device_handle < 0) return ERROR_INVALID_ARGUMENT;
    g_registered_handle = device_handle;
    Report("RegisterDevice", "registered guest HMD handle", device_type, device_handle);
    return 0;
}

static int32_t KYTY_SYSV_ABI TrackerUnregisterDevice(int32_t device_handle) {
    std::lock_guard lock(g_mutex);
    if (!g_initialized) return ERROR_NOT_INITIALIZED;
    if (device_handle < 0 || device_handle != g_registered_handle) return ERROR_INVALID_ARGUMENT;
    g_registered_handle = -1;
    return 0;
}

static int32_t KYTY_SYSV_ABI TrackerGetResult(const GetResultParameter* parameter, void* output) {
    if (parameter == nullptr || output == nullptr) return ERROR_INVALID_ARGUMENT;
    GetResultParameter input {};
    if (!CopyFromGuest(parameter, &input, sizeof(input))) return ERROR_INVALID_ARGUMENT;
    if (!ValidGetResult(input)) return ERROR_INVALID_ARGUMENT;
    std::lock_guard lock(g_mutex);
    if (!g_initialized || input.device_handle != g_registered_handle || g_coordinate_system != 1)
        return ERROR_NOT_INITIALIZED;
    const uint32_t invalid = 0;
    const std::array<float, 4> invalid_orientation {};
    // GT7 ignores this function's return code and copies the render quaternion
    // even when tracking-valid is zero. A zero (non-unit) quaternion makes the
    // Hmd2 render-pose validator reject stale output on any later failure.
    if (!WritableGuestSpan(OutputField(output, 0x70), sizeof(invalid_orientation)) ||
        !CopyToGuest(OutputField(output, 0x18), &invalid, sizeof(invalid)) ||
        !CopyToGuest(OutputField(output, 0x70), invalid_orientation.data(), sizeof(invalid_orientation)))
        return ERROR_INVALID_ARGUMENT;
    const uint64_t now_us = LibKernel::KernelGetProcessTime();
    const uint64_t horizon_us = input.predicted_display_time > now_us ? input.predicted_display_time - now_us : 0;
    if (horizon_us > 250000) {
        Report("GetResult", "prediction target outside host supported horizon", now_us, input.predicted_display_time);
        return ERROR_INVALID_ARGUMENT;
    }
    Graphics::Vr::Pose pose;
    std::array<Graphics::Vr::EyeGeometry, 2> eye_geometry;
    const auto pose_status = Graphics::Vr::HostBridge().ReadTrackingSample(
        static_cast<float>(horizon_us) / 1000000.0f, pose, eye_geometry);
    if (!pose_status || !pose.connected || !pose.valid) {
        Report("GetResult", "host disconnected or tracking unavailable; result invalidated");
        return ERROR_NOT_INITIALIZED;
    }
    ConsumerPose head;
    std::array<ConsumerPose, 2> eyes;
    if (!PoseFromRigidTransform(pose.device_to_absolute, head) ||
        !ComposeEyePose(pose.device_to_absolute, eye_geometry[0].eye_to_head, eyes[0]) ||
        !ComposeEyePose(pose.device_to_absolute, eye_geometry[1].eye_to_head, eyes[1])) {
        Report("GetResult", "invalid host rigid transform");
        return ERROR_NOT_INITIALIZED;
    }
    // GT7-specific consumer contract, not a reconstructed complete SDK struct:
    // +60/+70 is the render pose submitted by RVA 116682..11671b;
    // +a0/+b0 and +e0/+f0 are the two eye poses (the consumer's non-device
    // branch uses -/+0.03m, but here actual host EyeToHead transforms are used).
    // +120/+130 drives the main camera at RVA 117d26. Its precise SDK time/
    // coordinate distinction from the render pose is unknown: the experiment
    // deliberately supplies the SAME genuine predicted head pose to both.
    // +160/+170 is consumed by gaze processing: leave it untouched; no eye
    // tracking is fabricated. Unknown +10/+58 time fields also stay untouched.
    if (!WriteConsumerPoses(output, head, eyes)) return ERROR_INVALID_ARGUMENT;
    Report("GetResult", "published real head, camera and two eye consumer poses",
           input.device_handle, horizon_us);
    return 0;
}

} // namespace VrTracker2

LIB_DEFINE(InitVrTracker2_1) {
    LIB_FUNC("TwqZnaIjWv4", VrTracker2::TrackerQueryMemory);
    LIB_FUNC("6Jy73SRfG-o", VrTracker2::TrackerInitialize);
    LIB_FUNC("UVCMLmS-Eas", VrTracker2::TrackerSetCoordinateSystem);
    LIB_FUNC("Dog+g25QYjw", VrTracker2::TrackerRegisterDevice);
    LIB_FUNC("kFt4MB3SUEk", VrTracker2::TrackerUnregisterDevice);
    LIB_FUNC("J4Vh3VVX0iU", VrTracker2::TrackerGetResult);
}

} // namespace Libs

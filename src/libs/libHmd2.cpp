#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "graphics/guest_gpu/tile.h"
#include "graphics/vr/openvrBridge.h"
#include "kernel/pthread.h"
#include "libs/agc.h"
#include "libs/errno.h"
#include "libs/hmd2.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"

#include <atomic>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Libs {

LIB_VERSION("Hmd2", 1, "Hmd2", 1, 1);

namespace Hmd2 {

constexpr int32_t HMD_ERROR_ALREADY_INITIALIZED = -1972240383; // 0x8a720001
constexpr int32_t ERROR_NOT_INITIALIZED     = -1972240382; // 0x8a720002
constexpr int32_t ERROR_PARAMETER_NULL      = -1972240376; // 0x8a720008
constexpr int32_t ERROR_REPROJECTION_ALREADY_INITIALIZED = -1972240372; // 0x8a72000c
constexpr int32_t ERROR_REPROJECTION_IN_VR_MODE = -1972240356; // 0x8a72001c

struct SceHmd2InitializeParam {
	void*   reserved0;
	uint8_t reserved[8];
};

struct SceHmd2DeviceInformation {
	uint32_t status;
	uint32_t reserve0;
	struct {
		struct {
			uint32_t width;
			uint32_t height;
		} panelResolution;
		struct {
			uint16_t refreshRate90Hz;
			uint16_t refreshRate120Hz;
		} flipToDisplayLatency;
	} deviceInfo;
	uint8_t hmuMount;
	uint8_t lensSeparationDistance;
	uint8_t reserve1[2];
	float   virtualImageDistance;
};

struct SceHmd2FieldOfView {
	float tanOut;
	float tanIn;
	float tanTop;
	float tanBottom;
};

struct SceHmd2ReprojectionInitializeParam {
	void*    pReprojectionBuff;
	void*    pDisplayBuff;
	int32_t  threadPriority;
	int32_t  cpuAffinityMask;
	uint32_t pipeId;
	uint32_t queueId;
	uint32_t type;
	void*    pSeeThroughBuff;
	int32_t  reprojectionTiming;
	uint32_t reserved[5];
};

static_assert(sizeof(SceHmd2InitializeParam) == 16);
static_assert(offsetof(SceHmd2InitializeParam, reserved) == 8);
static_assert(sizeof(SceHmd2DeviceInformation) == 28);
static_assert(offsetof(SceHmd2DeviceInformation, deviceInfo) == 8);
static_assert(offsetof(SceHmd2DeviceInformation, hmuMount) == 20);
static_assert(offsetof(SceHmd2DeviceInformation, virtualImageDistance) == 24);
static_assert(sizeof(SceHmd2FieldOfView) == 16);
static_assert(alignof(SceHmd2FieldOfView) == 4);
static_assert(sizeof(SceHmd2ReprojectionInitializeParam) == 72);
static_assert(alignof(SceHmd2ReprojectionInitializeParam) == 8);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, threadPriority) == 16);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, type) == 32);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, pSeeThroughBuff) == 40);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, reprojectionTiming) == 48);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, reserved) == 52);

static std::atomic<bool> g_initialized = false;
static std::mutex g_reprojection_mutex;
static std::atomic<ReprojectionState*> g_reprojection_state {nullptr};
// Keep HLE state host-owned. The guest work allocation is not a lifetime guarantee
// for a pointer accessed later by another emulated thread.
static ReprojectionState g_owned_reprojection_state;
static bool g_open = false;
static uint64_t g_epoch = 0;
static uint64_t g_generation = 0;
struct PendingStereo {
	StereoFrameSnapshot snapshot;
	uint64_t ticket = 0;
	bool ready = false;
};
static std::array<PendingStereo, MAX_FRAME_BUFFERS> g_stereo;
struct FrameTime {
	uint64_t token = 0;
	uint64_t predicted_us = 0;
	bool valid = false;
};
static std::array<FrameTime, 64> g_frame_times;
// Process-wide diagnostic budget, protected by g_reprojection_mutex. Clearing or
// reopening the HMD must not replenish it.
static uint32_t g_layer_diagnostic_records = 0;
static uint64_t g_layer_diagnostic_last_ns = 0;

// CPU-facing HLE metadata may live on a guest stack. GPU-only backing readers are
// deliberately unsuitable. An OS self-read fails cleanly on inaccessible bytes.
static bool ReadHmdMemory(uint64_t address, void* destination, size_t bytes) {
	if (address == 0 || destination == nullptr || bytes == 0 || bytes > 1024 ||
	    UINT64_MAX - address < bytes) {
		return false;
	}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	SIZE_T copied = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
	                         destination, bytes, &copied) != 0 && copied == bytes;
#elif defined(__APPLE__)
	mach_vm_size_t copied = 0;
	return mach_vm_read_overwrite(mach_task_self(), address, bytes,
	    reinterpret_cast<mach_vm_address_t>(destination), &copied) == KERN_SUCCESS && copied == bytes;
#else
	iovec local {destination, bytes};
	iovec remote {reinterpret_cast<void*>(address), bytes};
	const auto copied = syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0);
	return copied >= 0 && static_cast<size_t>(copied) == bytes;
#endif
}

static bool WriteHmdMemory(void* destination, const void* source, size_t bytes) {
	const auto address = reinterpret_cast<uint64_t>(destination);
	if (address == 0 || source == nullptr || bytes == 0 || bytes > 1024 ||
	    UINT64_MAX - address < bytes) {
		return false;
	}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	// WriteProcessMemory can temporarily bypass read-only protection. Refuse those
	// pages first, including guest write-observation pages; do not silently defeat it.
	for (uint64_t cursor = address; cursor < address + bytes;) {
		MEMORY_BASIC_INFORMATION region {};
		if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) == 0 ||
		    region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0) {
			return false;
		}
		const DWORD access = region.Protect & 0xff;
		if (access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
		    access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) {
			return false;
		}
		const auto end = reinterpret_cast<uint64_t>(region.BaseAddress) + region.RegionSize;
		if (end <= cursor) {
			return false;
		}
		cursor = end;
	}
	SIZE_T copied = 0;
	return WriteProcessMemory(GetCurrentProcess(), destination, source, bytes, &copied) != 0 &&
	       copied == bytes;
#elif defined(__APPLE__)
	return mach_vm_write(mach_task_self(), address,
	    reinterpret_cast<vm_offset_t>(source), static_cast<mach_msg_type_number_t>(bytes)) == KERN_SUCCESS;
#else
	iovec local {const_cast<void*>(source), bytes};
	iovec remote {destination, bytes};
	const auto copied = syscall(SYS_process_vm_writev, getpid(), &local, 1, &remote, 1, 0);
	return copied >= 0 && static_cast<size_t>(copied) == bytes;
#endif
}

static bool ValidateStereoDescriptors(const std::array<Graphics::ShaderTextureResource, 2>& eyes) {
	const auto& left = eyes[0];
	const auto& right = eyes[1];
	if (left.IsNull() || right.IsNull() ||
	    left.Type() != Graphics::Prospero::ImageType::kColor2DArray ||
	    right.Type() != Graphics::Prospero::ImageType::kColor2DArray ||
	    static_cast<uint32_t>(left.Format()) == 0 || left.BaseLevel() > left.LastLevel() ||
	    left.BaseArray5() != 0 || left.Depth() != 0 ||
	    right.BaseArray5() != 1 || right.Depth() != 1) {
		return false;
	}
	// GT7 constructs exactly these two views by masking the two 13-bit slice fields.
	// Do not accept an unrelated second texture, a same-eye duplicate, or an array span.
	for (size_t i = 0; i < 8; ++i) {
		const uint32_t mask = i == 4 ? ~uint32_t {0x1fff1fff} : UINT32_MAX;
		if ((left.fields[i] & mask) != (right.fields[i] & mask)) {
			return false;
		}
	}
	return true;
}

bool ReadRegisteredStereo(const void* list, std::array<Graphics::ShaderTextureResource, 2>& out) {
	out = {};
	struct RegistrationList { uint64_t layer; uint32_t count; uint32_t reserved; } header {};
	std::array<uint8_t, 68> layer {};
	if (!ReadHmdMemory(reinterpret_cast<uint64_t>(list), &header, sizeof(header)) ||
	    header.count != 1 || header.reserved != 0 ||
	    !ReadHmdMemory(header.layer, layer.data(), layer.size())) {
		return false;
	}
	uint32_t type = 0;
	std::memcpy(&type, layer.data(), sizeof(type));
	if (type != 0x10) {
		return false;
	}
	std::array<Graphics::ShaderTextureResource, 2> eyes;
	std::memcpy(eyes.data(), layer.data() + 4, sizeof(eyes));
	if (!ValidateStereoDescriptors(eyes)) {
		return false;
	}
	out = eyes;
	return true;
}

static void ClearStereoFramesLocked() {
	++g_epoch;
	for (auto& slot: g_stereo) {
		slot = {};
	}
	g_frame_times = {};
}

void ClearStereoFrames() {
	std::scoped_lock lock {g_reprojection_mutex};
	ClearStereoFramesLocked();
}

bool TakeStereoFrame(int buffer_index, StereoFrameSnapshot& out) {
	out = {};
	std::scoped_lock lock {g_reprojection_mutex};
	if (buffer_index < 0 || buffer_index >= MAX_FRAME_BUFFERS || !g_open ||
	    g_reprojection_state.load(std::memory_order_acquire) == nullptr ||
	    g_owned_reprojection_state.output_mode.load(std::memory_order_acquire) == 0) {
		return false;
	}
	auto& slot = g_stereo[static_cast<size_t>(buffer_index)];
	if (!slot.ready) {
		return false;
	}
	out = slot.snapshot;
	slot.ready = false;
	return true;
}

static bool ReadConnectedHeadset(Graphics::Vr::HeadsetInfo& info) {
	return Config::VrEnabled() && Graphics::Vr::HostBridge().ReadHeadset(info) && info.connected &&
	       info.recommended_eye_width != 0 && info.recommended_eye_width <= 8192 &&
	       info.recommended_eye_height != 0 && info.recommended_eye_height <= 16384;
}

ReprojectionState* GetReprojectionState() {
	return g_reprojection_state.load(std::memory_order_acquire);
}

static Graphics::SizeAlign ReprojectionDisplaySize() {
	// The host display target is one uncompressed tiled 10-bit panel surface.
	Graphics::Vr::HeadsetInfo info;
	const bool connected = ReadConnectedHeadset(info);
	const auto width = connected ? info.recommended_eye_width * 2 : PANEL_WIDTH;
	const auto height = connected ? info.recommended_eye_height : PANEL_HEIGHT;
	Graphics::TileSizeAlign size {};
	Graphics::TileGetTextureTotalSize(Graphics::Prospero::BufferFormat::k10_10_10_2UNorm,
	                                  width, height, 1, 1,
	                                  Graphics::Prospero::TileMode::kRenderTarget, false, size);
	EXIT_IF(size.size == 0 || size.align == 0);
	return {size.size, size.align};
}

static int32_t KYTY_SYSV_ABI Hmd2Initialize(const SceHmd2InitializeParam* param) {
	PRINT_NAME();
	if (param == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	if (g_initialized.exchange(true)) {
		return HMD_ERROR_ALREADY_INITIALIZED;
	}
	LOGF("Hmd2: initialized, SteamVR requested=%s\n", Config::VrEnabled() ? "yes" : "no");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2GetDeviceInformation(SceHmd2DeviceInformation* info) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	if (info == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	SceHmd2DeviceInformation result {};
	Graphics::Vr::HeadsetInfo host;
	if (ReadConnectedHeadset(host) && host.ipd_known && std::isfinite(host.ipd_m) &&
	    host.ipd_m > 0 && host.ipd_m < 0.255f) {
		// This is the actual runtime's recommended render extent, not a claim about
		// physical panel pixels. Activity is a runtime proxy, not a proximity sensor.
		result.hmuMount = host.activity_level == 1 || host.activity_level == 2;
		result.lensSeparationDistance = static_cast<uint8_t>(std::lround(host.ipd_m * 1000.0f));
		result.deviceInfo.panelResolution = {host.recommended_eye_width * 2, host.recommended_eye_height};
		// No host API provides this optical focal distance or the guest scanout latency.
		// Leave unknown quantities zero rather than reporting the former invented 2 m.
	} else {
		result.status = 2;
	}
	return WriteHmdMemory(info, &result, sizeof(result)) ? OK : ERROR_PARAMETER_NULL;
}

static int32_t KYTY_SYSV_ABI Hmd2GetFieldOfViewWithoutHandle(SceHmd2FieldOfView* fov) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	if (fov == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	Graphics::Vr::HeadsetInfo host;
	if (!ReadConnectedHeadset(host)) {
		return VideoOut::VIDEO_OUT_ERROR_NO_DEVICE;
	}
	// This API has one outward/inward pair. GT7 mirrors it for the right eye.
	// Preserve the actual left-eye tangents; do not invent a symmetric average.
	// OpenVR's raw names are reversed vertically: "bottom" is the +Y top edge,
	// "top" the -Y bottom edge. GT7 needs positive physical top/bottom magnitudes.
	// https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md
	const auto& eye = host.eyes[0];
	const SceHmd2FieldOfView result {-eye.left, eye.right, eye.bottom, -eye.top};
	if (!std::isfinite(result.tanOut) || result.tanOut <= 0 ||
	    !std::isfinite(result.tanIn) || result.tanIn <= 0 ||
	    !std::isfinite(result.tanTop) || result.tanTop <= 0 ||
	    !std::isfinite(result.tanBottom) || result.tanBottom <= 0) {
		return VideoOut::VIDEO_OUT_ERROR_NO_DEVICE;
	}
	return WriteHmdMemory(fov, &result, sizeof(result)) ? OK : ERROR_PARAMETER_NULL;
}

static Graphics::SizeAlign KYTY_SYSV_ABI Hmd2ReprojectionQueryBufferSizeAlign() {
	const Graphics::SizeAlign size {sizeof(ReprojectionState), alignof(ReprojectionState)};
	LOGF("Hmd2: reprojection work size=%" PRIu64 " alignment=%zu\n", size.m_size, size.m_align);
	return size;
}

static Graphics::SizeAlign KYTY_SYSV_ABI Hmd2ReprojectionQueryDisplayBufferSizeAlign() {
	const auto size = ReprojectionDisplaySize();
	LOGF("Hmd2: reprojection display size=%" PRIu64 " alignment=%zu\n", size.m_size,
	     size.m_align);
	return size;
}

static int32_t KYTY_SYSV_ABI
Hmd2ReprojectionInitialize(const SceHmd2ReprojectionInitializeParam* param, void*) {
	SceHmd2ReprojectionInitializeParam input {};
	if (!ReadHmdMemory(reinterpret_cast<uint64_t>(param), &input, sizeof(input)) ||
	    input.pReprojectionBuff == nullptr || input.pDisplayBuff == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	if (input.pSeeThroughBuff != nullptr || input.reprojectionTiming < 0) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	const auto display_size = ReprojectionDisplaySize();
	std::scoped_lock lock {g_reprojection_mutex};
	if (GetReprojectionState() != nullptr) {
		return ERROR_REPROJECTION_ALREADY_INITIALIZED;
	}
	auto* state = &g_owned_reprojection_state;
	if (input.reprojectionTiming != 0) {
		state->timing_us.store(static_cast<uint32_t>(input.reprojectionTiming),
		                      std::memory_order_relaxed);
	}
	g_reprojection_state.store(state, std::memory_order_release);
	LOGF("Hmd2: reprojection initialized work=%p display=%p size=%" PRIu64 " timing=%u\n",
	     input.pReprojectionBuff, input.pDisplayBuff, display_size.m_size,
	     state->timing_us.load(std::memory_order_relaxed));
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionEnableVrMode(uint64_t output_mode) {
	Graphics::Vr::HeadsetInfo host;
	if (!ReadConnectedHeadset(host)) {
		return VideoOut::VIDEO_OUT_ERROR_NO_DEVICE;
	}
	std::scoped_lock lock {g_reprojection_mutex};
	auto* state = GetReprojectionState();
	if (state == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	if (output_mode != OUTPUT_MODE_89_91HZ && output_mode != OUTPUT_MODE_119_88HZ) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	uint64_t expected = 0;
	if (!state->output_mode.compare_exchange_strong(expected, output_mode)) {
		return ERROR_REPROJECTION_IN_VR_MODE;
	}
	LOGF("Hmd2: VR output mode=0x%" PRIx64 "\n", output_mode);
	return OK;
}

// The observed GT7 Open call passes a user id and three zero options, and uses
// the nonnegative result as Tracker's HMD handle. This is an HLE-owned handle,
// not an OpenVR device index and not the separate VideoOut bus-0x20 handle.
static int32_t KYTY_SYSV_ABI Hmd2Open(int32_t, uint64_t option1, uint64_t option2, uint64_t option3) {
	if (!g_initialized.load(std::memory_order_acquire)) {
		return ERROR_NOT_INITIALIZED;
	}
	if (option1 != 0 || option2 != 0 || option3 != 0) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	Graphics::Vr::HeadsetInfo host;
	if (!ReadConnectedHeadset(host)) {
		return VideoOut::VIDEO_OUT_ERROR_NO_DEVICE;
	}
	std::scoped_lock lock {g_reprojection_mutex};
	if (g_open) {
		return HMD_ERROR_ALREADY_INITIALIZED;
	}
	ClearStereoFramesLocked();
	g_open = true;
	return 1;
}

static int32_t KYTY_SYSV_ABI Hmd2Close(int32_t handle) {
	std::scoped_lock lock {g_reprojection_mutex};
	if (handle != 1 || !g_open) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_HANDLE;
	}
	g_open = false;
	g_owned_reprojection_state.output_mode.store(0, std::memory_order_release);
	ClearStereoFramesLocked();
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionDisableVrMode() {
	std::scoped_lock lock {g_reprojection_mutex};
	if (GetReprojectionState() == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	g_owned_reprojection_state.output_mode.store(0, std::memory_order_release);
	ClearStereoFramesLocked();
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetAllowPositionalReprojection(uint32_t allow) {
	// OpenVR owns the final compositor's reprojection policy. This preference does
	// not grant us permission to manufacture depth or motion data.
	if (allow > 1) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetRenderConfig(uint32_t mode, uint64_t reserved1,
	                                                       uint64_t reserved2) {
	if (mode != 1 || reserved1 != 0 || reserved2 != 0) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	return GetReprojectionState() != nullptr ? OK : ERROR_REPROJECTION_NOT_INITIALIZED;
}

static uint64_t HmdSteadyNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
	    std::chrono::steady_clock::now().time_since_epoch()).count());
}

static bool HmdLayerDiagnosticsEnabled();

enum class TimingReason : size_t {
	Inactive, HostTiming, ClockBackwards, ClockUncertain, InvalidSample,
	SampleFuture, SampleExpired, Overflow, EpochChanged, LookupInactive,
	LookupMissing, OutputUnwritable, Count
};
// Per-reason budgets keep a noisy early failure from hiding a different later
// cause. Process-wide: close/open does not replenish the maximum 12*4 lines.
static std::array<std::atomic<uint64_t>, static_cast<size_t>(TimingReason::Count)> g_timing_diagnostic_counts {};
struct TimingDiagnostic {
	Graphics::Vr::DisplayTiming host {};
	uint64_t epoch = 0, before_ns = 0, after_ns = 0, guest_us = 0;
	FrameTime lookup {};
};

static int32_t TimingFailure(TimingReason reason, int32_t result, uint64_t token,
	const TimingDiagnostic& diagnostic, const char* host_error = "") noexcept {
	if (!HmdLayerDiagnosticsEnabled()) {
		return result;
	}
	const size_t index = static_cast<size_t>(reason);
	const auto occurrence = g_timing_diagnostic_counts[index].fetch_add(1, std::memory_order_relaxed) + 1;
	if (occurrence > 4) {
		return result;
	}
	try {
		constexpr std::array<const char*, static_cast<size_t>(TimingReason::Count)> names {
		    "inactive", "host_timing", "clock_backwards", "clock_uncertain", "invalid_sample",
		    "sample_future", "sample_expired", "overflow", "epoch_changed", "lookup_inactive",
		    "lookup_missing", "output_unwritable"};
		const auto& t = diagnostic.host;
		// All values are owned snapshots. Callers release the metadata mutex before
		// logging; this function performs no clock query or guest memory access.
		Log::WriteToConsoleAndLog(fmt::sprintf(
		    "Hmd2 timing failure: reason=%s occurrence=%" PRIu64 " token=%" PRIu64
		    " result=%d epoch=%" PRIu64 " before_ns=%" PRIu64 " after_ns=%" PRIu64
		    " guest_us=%" PRIu64 " sample_ns=%" PRIu64 " prediction_s=%.9g"
		    " hz=%.9g since_vsync_s=%.9g photons_s=%.9g vsync=%" PRIu64
		    " lookup_valid=%u lookup_token=%" PRIu64 " lookup_prediction_us=%" PRIu64
		    " host_error=%.240s\n",
		    names[index], occurrence, token, result, diagnostic.epoch,
		    diagnostic.before_ns, diagnostic.after_ns, diagnostic.guest_us,
		    t.sample_monotonic_ns, t.prediction_seconds, t.frequency_hz,
		    t.seconds_since_last_vsync, t.seconds_from_vsync_to_photons, t.vsync_counter,
		    static_cast<unsigned>(diagnostic.lookup.valid), diagnostic.lookup.token,
		    diagnostic.lookup.predicted_us, host_error));
	} catch (...) {
		// Diagnostics must preserve the original error and timing state.
	}
	return result;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionBeginFrame(uint64_t frame_token) {
	uint64_t epoch = 0;
	bool active = false;
	{
		std::scoped_lock lock {g_reprojection_mutex};
		epoch = g_epoch;
		active = g_open && g_owned_reprojection_state.output_mode.load(std::memory_order_acquire) != 0;
		if (active) {
			g_frame_times[frame_token % g_frame_times.size()] = {};
		}
	}
	TimingDiagnostic diagnostic {};
	diagnostic.epoch = epoch;
	if (!active) {
		return TimingFailure(TimingReason::Inactive, ERROR_REPROJECTION_NOT_INITIALIZED, frame_token, diagnostic);
	}
	// Retry a transient query/mapping failure once using a new runtime sample.
	// Never advance an old vsync by guessed periods or reuse its expired target.
	// The ordinary successful path still performs exactly one runtime query.
	for (bool may_retry = true;; may_retry = false) {
		if (!may_retry) {
			bool still_active = false;
			{
				std::scoped_lock lock {g_reprojection_mutex};
				still_active = epoch == g_epoch && g_open &&
				    g_owned_reprojection_state.output_mode.load(std::memory_order_acquire) != 0;
			}
			if (!still_active) {
				return TimingFailure(TimingReason::EpochChanged, ERROR_REPROJECTION_NOT_INITIALIZED, frame_token, diagnostic);
			}
		}
		// Bridge holds its own lock. Never call it while holding the HMD metadata lock.
		Graphics::Vr::DisplayTiming timing;
		const auto timing_status = Graphics::Vr::HostBridge().ReadDisplayTiming(timing);
		// WaitGetPoses may legitimately hold the bridge mutex for a whole refresh.
		// Map the runtime's actual sample time, not the midpoint of that lock wait.
		const auto before_ns = HmdSteadyNs();
		const auto guest_us = LibKernel::KernelGetProcessTime();
		const auto after_ns = HmdSteadyNs();
		diagnostic.host = timing;
		diagnostic.before_ns = before_ns;
		diagnostic.after_ns = after_ns;
		diagnostic.guest_us = guest_us;
		if (!timing_status) {
			if (may_retry) {
				continue;
			}
			return TimingFailure(TimingReason::HostTiming, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE,
			                     frame_token, diagnostic, timing_status.message.c_str());
		}
		if (after_ns < before_ns) {
			return TimingFailure(TimingReason::ClockBackwards, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE, frame_token, diagnostic);
		}
		if (after_ns - before_ns > 2000000) {
			if (may_retry) {
				continue;
			}
			return TimingFailure(TimingReason::ClockUncertain, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE, frame_token, diagnostic);
		}
		if (timing.sample_monotonic_ns == 0 ||
		    !std::isfinite(timing.prediction_seconds) || timing.prediction_seconds < 0 ||
		    timing.prediction_seconds > 0.25f) {
			return TimingFailure(TimingReason::InvalidSample, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE, frame_token, diagnostic);
		}
		const auto middle_ns = before_ns + (after_ns - before_ns) / 2;
		const auto prediction_ns = static_cast<uint64_t>(std::llround(timing.prediction_seconds * 1000000000.0));
		if (timing.sample_monotonic_ns > middle_ns) {
			return TimingFailure(TimingReason::SampleFuture, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE, frame_token, diagnostic);
		}
		if (middle_ns - timing.sample_monotonic_ns > prediction_ns) {
			if (may_retry) {
				continue;
			}
			return TimingFailure(TimingReason::SampleExpired, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE, frame_token, diagnostic);
		}
		const auto remaining_us = (prediction_ns - (middle_ns - timing.sample_monotonic_ns)) / 1000;
		if (UINT64_MAX - guest_us < remaining_us) {
			return TimingFailure(TimingReason::Overflow, VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE, frame_token, diagnostic);
		}
		bool published = false;
		{
			std::scoped_lock lock {g_reprojection_mutex};
			if (epoch == g_epoch && g_open) {
				g_frame_times[frame_token % g_frame_times.size()] = {frame_token, guest_us + remaining_us, true};
				published = true;
			}
		}
		return published ? OK : TimingFailure(TimingReason::EpochChanged, ERROR_REPROJECTION_NOT_INITIALIZED, frame_token, diagnostic);
	}
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionGetPredictedDisplayTime(uint64_t frame_token, uint64_t* out) {
	TimingDiagnostic diagnostic {};
	bool open = false;
	{
		std::scoped_lock lock {g_reprojection_mutex};
		open = g_open;
		diagnostic.epoch = g_epoch;
		diagnostic.lookup = g_frame_times[frame_token % g_frame_times.size()];
	}
	if (!open) {
		return TimingFailure(TimingReason::LookupInactive, ERROR_REPROJECTION_NOT_INITIALIZED, frame_token, diagnostic);
	}
	const auto& frame = diagnostic.lookup;
	if (!frame.valid || frame.token != frame_token) {
		return TimingFailure(TimingReason::LookupMissing, ERROR_REPROJECTION_NOT_INITIALIZED, frame_token, diagnostic);
	}
	return WriteHmdMemory(out, &frame.predicted_us, sizeof(frame.predicted_us)) ? OK :
	       TimingFailure(TimingReason::OutputUnwritable, ERROR_PARAMETER_NULL, frame_token, diagnostic);
}

static int32_t KYTY_SYSV_ABI Hmd2GazeGetResultForFoveatedRendering(const void*, void*) {
	// Steam Link/OpenVR does not expose a verified eye-gaze ABI here. GT7 has a
	// checked failure branch at 0x11406e; do not report a fabricated centered gaze.
	return VideoOut::VIDEO_OUT_ERROR_NO_DEVICE;
}

static bool HmdLayerDiagnosticsEnabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_VR_DIAGNOSTICS");
		return value != nullptr && std::strcmp(value, "1") == 0;
	}();
	return enabled;
}

struct LayerDiagnosticTiming {
	uint32_t record = 0;
	uint64_t steady_ns = 0;
	uint64_t predicted_guest_us = 0;
	bool prediction_known = false;
};

// Called only while holding g_reprojection_mutex, after a valid publication.
// Disabled and exhausted diagnostics do not query either clock.
static bool ReserveLayerDiagnostic(uint64_t frame_token, LayerDiagnosticTiming& out) {
	if (!HmdLayerDiagnosticsEnabled() || g_layer_diagnostic_records >= 32) {
		return false;
	}
	const uint64_t now = HmdSteadyNs();
	if (g_layer_diagnostic_records != 0 &&
	    (now < g_layer_diagnostic_last_ns || now - g_layer_diagnostic_last_ns < 250000000)) {
		return false;
	}
	out.record = ++g_layer_diagnostic_records;
	out.steady_ns = now;
	g_layer_diagnostic_last_ns = now;
	const auto& frame = g_frame_times[frame_token % g_frame_times.size()];
	if (frame.valid && frame.token == frame_token) {
		out.prediction_known = true;
		out.predicted_guest_us = frame.predicted_us;
	}
	return true;
}

static void ReportLayerDiagnostic(const std::array<uint8_t, 0x1d8>& layer,
	const StereoFrameSnapshot& snapshot, const LayerDiagnosticTiming& timing) noexcept {
	// The complete layer was already copied by ReadHmdMemory. Do not follow the
	// optional stack pointer at +0x1d0 or read the caller again for diagnostics.
	try {
		const auto word = [&](size_t offset) {
			uint32_t value = 0;
			std::memcpy(&value, layer.data() + offset, sizeof(value));
			return value;
		};
		std::array<float, 8> fov {};
		std::memcpy(fov.data(), layer.data() + 0x148, sizeof(fov));
		uint64_t optional_pointer = 0;
		std::memcpy(&optional_pointer, layer.data() + 0x1d0, sizeof(optional_pointer));
		// Preserve raw bits for fields whose SDK meaning is not established. Omit
		// the optional pointer value itself; its presence is sufficient evidence.
		constexpr char hex[] = "0123456789abcdef";
		std::array<char, 0x1d0 * 2 + 1> raw {};
		for (size_t i = 0; i < 0x1d0; ++i) {
			raw[i * 2] = hex[layer[i] >> 4];
			raw[i * 2 + 1] = hex[layer[i] & 15];
		}
		const uint64_t observed_guest_us = LibKernel::KernelGetProcessTime();
		// An explicit opt-in must remain visible with the normal Silent launcher.
		Log::WriteToConsoleAndLog(fmt::sprintf(
		     "Hmd2 layer diagnostic: record=%u slot=%d token=%" PRIu64 " generation=%" PRIu64
		     " steady_ns=%" PRIu64 " observed_guest_us=%" PRIu64
		     " prediction_known=%u predicted_guest_us=%" PRIu64
		     " texture=%ux%u,%ux%u logical=%ux%u,%ux%u"
		     " fov=[%.9g,%.9g,%.9g,%.9g],[%.9g,%.9g,%.9g,%.9g]"
		     " head=[%.9g,%.9g,%.9g] xyzw=[%.9g,%.9g,%.9g,%.9g]"
		     " flags_1a0_1a2=%02x,%02x,%02x optional_present=%u raw_000_1cf=%s\n",
		     timing.record, snapshot.buffer_index, snapshot.frame_token, snapshot.generation,
		     timing.steady_ns, observed_guest_us, static_cast<unsigned>(timing.prediction_known),
		     timing.predicted_guest_us,
		     static_cast<unsigned>(snapshot.eyes[0].Width5()) + 1,
		     static_cast<unsigned>(snapshot.eyes[0].Height5()) + 1,
		     static_cast<unsigned>(snapshot.eyes[1].Width5()) + 1,
		     static_cast<unsigned>(snapshot.eyes[1].Height5()) + 1,
		     word(0x128), word(0x12c), word(0x138), word(0x13c),
		     fov[0], fov[1], fov[2], fov[3], fov[4], fov[5], fov[6], fov[7],
		     snapshot.guest_position[0], snapshot.guest_position[1], snapshot.guest_position[2],
		     snapshot.guest_orientation[0], snapshot.guest_orientation[1],
		     snapshot.guest_orientation[2], snapshot.guest_orientation[3],
		     static_cast<unsigned>(layer[0x1a0]), static_cast<unsigned>(layer[0x1a1]),
		     static_cast<unsigned>(layer[0x1a2]), static_cast<unsigned>(optional_pointer != 0),
		     raw.data()));
	} catch (...) {
		// Optional diagnostics must not change a successfully published frame.
	}
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetParamWithBuffer(int32_t buffer_index,
	uint64_t frame_token, const void* params, const void* reserved) {
	if (buffer_index < 0 || buffer_index >= MAX_FRAME_BUFFERS) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_INDEX;
	}
	uint64_t epoch = 0;
	uint64_t ticket = 0;
	{
		std::scoped_lock lock {g_reprojection_mutex};
		if (!g_open || g_owned_reprojection_state.output_mode.load(std::memory_order_acquire) == 0) {
			return ERROR_REPROJECTION_NOT_INITIALIZED;
		}
		epoch = g_epoch;
		ticket = ++g_generation;
		auto& slot = g_stereo[static_cast<size_t>(buffer_index)];
		slot.ready = false;
		slot.ticket = ticket;
	}
	struct LayerList { uint64_t layer; uint64_t count; uint64_t reserved; } header {};
	// Exact observed type-0x10 extent (GT7 0x11652d..0x116a02). The final optional
	// pointer is retained in neither the published snapshot nor a deferred callback.
	std::array<uint8_t, 0x1d8> layer {};
	uint32_t type = 0;
	if (reserved != nullptr ||
	    !ReadHmdMemory(reinterpret_cast<uint64_t>(params), &header, sizeof(header)) ||
	    header.count != 1 || header.reserved != 0 ||
	    !ReadHmdMemory(header.layer, &type, sizeof(type))) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	if (type != 0x10) {
		// Type 0x12 is a single image. Leave this slot empty; never duplicate it into
		// two eyes or accidentally re-use the previous stereo frame for this slot.
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	if (!ReadHmdMemory(header.layer, layer.data(), layer.size())) {
		return ERROR_PARAMETER_NULL;
	}
	std::memcpy(&type, layer.data(), sizeof(type));
	StereoFrameSnapshot snapshot;
	snapshot.buffer_index = buffer_index;
	snapshot.frame_token = frame_token;
	snapshot.generation = ticket;
	std::memcpy(snapshot.eyes.data(), layer.data() + 8, sizeof(snapshot.eyes));
	std::memcpy(snapshot.guest_position.data(), layer.data() + 0x168, sizeof(snapshot.guest_position));
	std::memcpy(snapshot.guest_orientation.data(), layer.data() + 0x174, sizeof(snapshot.guest_orientation));
	if (type != 0x10 || !ValidateStereoDescriptors(snapshot.eyes)) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	LayerDiagnosticTiming diagnostic;
	bool report = false;
	{
		std::scoped_lock lock {g_reprojection_mutex};
		auto& slot = g_stereo[static_cast<size_t>(buffer_index)];
		if (epoch != g_epoch || !g_open || slot.ticket != ticket) {
			return ERROR_REPROJECTION_NOT_INITIALIZED;
		}
		slot.snapshot = snapshot;
		slot.ready = true;
		report = ReserveLayerDiagnostic(frame_token, diagnostic);
	}
	if (report) {
		ReportLayerDiagnostic(layer, snapshot, diagnostic);
	}
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetTiming(int32_t timing) {
	auto* state = GetReprojectionState();
	if (state == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	if (timing < 2000 || timing > 7000) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	const auto rounded = static_cast<uint32_t>((timing + 99) / 100 * 100);
	state->timing_us.store(rounded, std::memory_order_release);
	LOGF("Hmd2: reprojection timing=%u us\n", rounded);
	return OK;
}

} // namespace Hmd2

LIB_DEFINE(InitHmd2_1) {
	LIB_FUNC("c812oYs7Vsc", Hmd2::Hmd2Initialize);
	LIB_FUNC("bIi4YUfSRys", Hmd2::Hmd2GetDeviceInformation);
	LIB_FUNC("gF8+lvc7GuQ", Hmd2::Hmd2GetFieldOfViewWithoutHandle);
	LIB_FUNC("U-CnbmeyYaA", Hmd2::Hmd2ReprojectionQueryBufferSizeAlign);
	LIB_FUNC("-C2nkoEYOnU", Hmd2::Hmd2ReprojectionQueryDisplayBufferSizeAlign);
	LIB_FUNC("C0rPwER-yxg", Hmd2::Hmd2ReprojectionInitialize);
	LIB_FUNC("VVvFh51o20s", Hmd2::Hmd2ReprojectionEnableVrMode);
	LIB_FUNC("FkQX7rjFomk", Hmd2::Hmd2ReprojectionSetTiming);
	LIB_FUNC("f3kPeoTZnIE", Hmd2::Hmd2Open);
	LIB_FUNC("oPhtjySuHa8", Hmd2::Hmd2Close);
	LIB_FUNC("uv0Ae+jCeWY", Hmd2::Hmd2ReprojectionSetAllowPositionalReprojection);
	LIB_FUNC("hA9LshbSkzw", Hmd2::Hmd2ReprojectionSetRenderConfig);
	LIB_FUNC("wj1kOyNF4vM", Hmd2::Hmd2ReprojectionDisableVrMode);
	LIB_FUNC("Ocf081WpBpA", Hmd2::Hmd2ReprojectionBeginFrame);
	LIB_FUNC("SVEG+1D7qHA", Hmd2::Hmd2ReprojectionGetPredictedDisplayTime);
	LIB_FUNC("retc+-uRMhk", Hmd2::Hmd2GazeGetResultForFoveatedRendering);
	LIB_FUNC("QxBJF-G2v5k", Hmd2::Hmd2ReprojectionSetParamWithBuffer);
}

} // namespace Libs

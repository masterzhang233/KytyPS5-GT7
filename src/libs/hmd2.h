#ifndef EMULATOR_SRC_LIBS_HMD2_H_
#define EMULATOR_SRC_LIBS_HMD2_H_

#include <atomic>
#include <array>
#include <cstdint>

#include "graphics/shader/shaderBindings.h"

namespace Libs::Hmd2 {

inline constexpr uint32_t PANEL_WIDTH  = 4000;
inline constexpr uint32_t PANEL_HEIGHT = 2040;
inline constexpr uint64_t OUTPUT_MODE_89_91HZ  = 0x2000c;
inline constexpr uint64_t OUTPUT_MODE_119_88HZ = 0x2000f;
inline constexpr int32_t ERROR_REPROJECTION_NOT_INITIALIZED = -1972240373; // 0x8a72000b

struct ReprojectionState {
	std::atomic<uint64_t> output_mode {0};
	std::atomic<uint32_t> timing_us {3000};
};

[[nodiscard]] ReprojectionState* GetReprojectionState();

inline constexpr int VIDEO_OUT_BUS = 0x20;
inline constexpr int MAX_FRAME_BUFFERS = 16;

// Owned metadata from GT7's type-0x10 layer. The descriptors retain the two distinct
// array views. They describe GPU resources, whose lifetime/synchronization belongs
// to the renderer; copying this struct does not make their pixel memory CPU-readable.
struct StereoFrameSnapshot {
	int32_t buffer_index = -1;
	uint64_t frame_token = 0;
	uint64_t generation = 0;
	std::array<Graphics::ShaderTextureResource, 2> eyes {};
	// Observed submitted head pose, deliberately not relabelled as OpenVR coordinates.
	std::array<float, 3> guest_position {};
	std::array<float, 4> guest_orientation {};
};

// Take once, on the GPU preparation path for this exact buffer slot. Never falls
// back to an older frame, a mono layer, or the ordinary television surface.
[[nodiscard]] bool TakeStereoFrame(int buffer_index, StereoFrameSnapshot& out);
void ClearStereoFrames();

// HMD VideoOut category 0x10 uses a distinct registration layout: a 16-byte list,
// followed by type@0 and two 32-byte AGC texture descriptors at +4 and +0x24.
// This reads/copies the caller's stack now; no pointer is retained.
[[nodiscard]] bool ReadRegisteredStereo(const void* list,
	std::array<Graphics::ShaderTextureResource, 2>& out);

} // namespace Libs::Hmd2

#endif // EMULATOR_SRC_LIBS_HMD2_H_

#include "graphics/host_gpu/renderer/gt7DlssDrawChecks.h"

#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/shader/shader.h"
#include "kernel/memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Libs::Graphics::Gt7DlssDrawChecks {
namespace {

constexpr uint64_t ScaleVertexHash = 0xdf39fc07bb648d84ull;
constexpr uint64_t ScalePixelHash  = 0x939a0b13ee9531c5ull;

// All four captured scale draws have this 64-byte quad (XXH3 f1449c8f25fc6fcb).
// The negative-height Vulkan viewport maps the first row to the top of the image.
constexpr std::array<float, 16> ScaleQuad {
    -1.0f,  1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
};
constexpr std::array<float, 16> Identity {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};
constexpr std::array<float, 4> White {1.0f, 1.0f, 1.0f, 1.0f};

template <size_t N>
bool CleanBytesEqual(RenderContext& context, uint64_t address,
                     const std::array<float, N>& expected) {
	constexpr auto size = sizeof(expected);
	if (address == 0 || address > std::numeric_limits<uint64_t>::max() - size ||
	    Libs::LibKernel::Memory::TryClampRangeSize(address, size) != size ||
	    context.GetBufferCache().HasGpuDirtyBytes(address, size)) {
		return false;
	}
	std::array<float, N> actual {};
	if (!Libs::LibKernel::Memory::TryReadBacking(address, actual.data(), size)) {
		return false;
	}
	// Deliberately exact, including the captured zero signs. No tolerance should turn an
	// unverified crop, shifted UV, tint or projection into a claimed full-screen scale.
	return std::memcmp(actual.data(), expected.data(), size) == 0;
}

bool AttributeMatches(const ShaderVertexInputInfo& vertex, int index, uint64_t address,
                      uint32_t stride, uint32_t records, uint32_t format, uint32_t swizzle,
                      int buffer, int first_register, int registers) {
	const auto& resource = vertex.resources[index];
	const auto& destination = vertex.resources_dst[index];
	return resource.Base48() == address && resource.Stride() == stride &&
	       resource.NumRecords() == records && resource.RawFormat() == format &&
	       resource.DstSelXYZW() == swizzle && !resource.AddTid() &&
	       !resource.SwizzleEnabled() && resource.Type() == 0 &&
	       destination.buffer_index == buffer && destination.fetch_index == 0 &&
	       destination.register_start == first_register && destination.registers_num == registers;
}

bool ValidSampler(const PreparedBindings& pixel) {
	if (pixel.runtime == nullptr || !*pixel.runtime || pixel.normalize_unnormalized ||
	    !pixel.bindless_heaps.empty() || !pixel.bindless_patches.empty() ||
	    pixel.images.size() != 1 || pixel.samplers.size() != 1) {
		return false;
	}
	const auto& program = *pixel.runtime->program;
	const auto& snapshot = *pixel.runtime->resources;
	if (program.shader_hash != ScalePixelHash || program.stage != ShaderType::Pixel ||
	    program.info.samplers.size() != 1 || !program.info.buffers.empty() ||
	    program.info.images.size() != 1 || program.info.images[0].written ||
	    !snapshot.bindless_heaps.empty() || !snapshot.bindless_sampler_heaps.empty() ||
	    !snapshot.gpu_fills.empty()) {
		return false;
	}
	const auto& sampler = program.info.samplers[0];
	if (sampler.bindless || sampler.force_point_filtering || sampler.depth_compare ||
	    sampler.integer_border || sampler.gather_lod ||
	    sampler.snapshot_index >= snapshot.samplers.size()) {
		return false;
	}
	const auto& descriptor = snapshot.samplers[sampler.snapshot_index];
	// Exact captured S#: normalized XY, linear min/mag and clamp-to-edge XY, zero LOD
	// bias, no anisotropy or compare. The caller verifies the sole mip/layer image view.
	constexpr std::array<uint32_t, 4> expected {0x00000012, 0x00fff000, 0x0a500000, 0};
	return descriptor.dword_count == expected.size() &&
	       std::equal(expected.begin(), expected.end(), descriptor.dwords.begin());
}

} // namespace

bool ValidScaleQuad(RenderContext& context, const ShaderVertexInputInfo& vertex,
                    const PreparedBindings& pixel) {
	if (!vertex.stage || vertex.stage.program->shader_hash != ScaleVertexHash ||
	    vertex.logical_stage != ShaderType::Vertex || vertex.prepare_failed ||
	    !vertex.fetch_embedded || vertex.fetch_external || vertex.clip_space.enabled ||
	    vertex.buffers_num != 2 || vertex.resources_num != 3 || !ValidSampler(pixel)) {
		return false;
	}
	const auto& quad = vertex.buffers[0];
	const auto& tint = vertex.buffers[1];
	if (quad.addr == 0 || quad.addr > std::numeric_limits<uint64_t>::max() - 64 ||
	    quad.stride != 16 || quad.num_records != 4 || quad.fetch_index != 0 ||
	    tint.stride != 0 || tint.num_records != 1 || tint.fetch_index != 0 ||
	    !AttributeMatches(vertex, 0, quad.addr, 16, 4, 64, 0x22c, 0, 9, 4) ||
	    !AttributeMatches(vertex, 1, tint.addr, 0, 1, 77, 0xfac, 1, 13, 4) ||
	    !AttributeMatches(vertex, 2, quad.addr + 8, 16, 4, 64, 0x22c, 0, 17, 2)) {
		return false;
	}
	const auto& program = *vertex.stage.program;
	const auto& snapshot = *vertex.stage.resources;
	if (program.info.buffers.size() != 1 || program.info.buffers[0].written ||
	    !program.info.images.empty() || snapshot.buffers.size() != 1 ||
	    !snapshot.bindless_heaps.empty() || !snapshot.bindless_sampler_heaps.empty() ||
	    !snapshot.gpu_fills.empty() || snapshot.buffers[0].dword_count != 4) {
		return false;
	}
	const auto matrix = DecodeNativeDescriptor<ShaderBufferResource>(snapshot.buffers[0]);
	if (matrix.Stride() != 16 || matrix.NumRecords() != 4 || matrix.RawFormat() != 77 ||
	    matrix.DstSelXYZW() != 0xfac || matrix.AddTid() || matrix.SwizzleEnabled() ||
	    matrix.Type() != 0) {
		return false;
	}
	// A stride-zero float4 tint still consumes 16 bytes, not stride*records (zero) or the
	// byte-sized GetSize() fallback (one). Its value was not in the old capture hash, so
	// explicitly require white now. GPU-written constants are intentionally unsupported.
	return CleanBytesEqual(context, quad.addr, ScaleQuad) &&
	       CleanBytesEqual(context, tint.addr, White) &&
	       CleanBytesEqual(context, matrix.Base48(), Identity);
}

} // namespace Libs::Graphics::Gt7DlssDrawChecks

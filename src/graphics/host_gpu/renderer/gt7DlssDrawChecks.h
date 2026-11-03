#pragma once

namespace Libs::Graphics {

class RenderContext;
struct ShaderVertexInputInfo;
struct PreparedBindings;

namespace Gt7DlssDrawChecks {

// GPU thread only, after the original draw's bindings have been prepared. This checks the
// captured scale shader's geometry, transform, tint and sampler. The caller must separately
// check direct draw/count/topology, viewport/scissor, image identities and render state.
// Unknown or GPU-written backing is rejected; this never downloads or waits for the GPU.
[[nodiscard]] bool ValidScaleQuad(RenderContext& context, const ShaderVertexInputInfo& vertex,
                                  const PreparedBindings& pixel);

} // namespace Gt7DlssDrawChecks
} // namespace Libs::Graphics

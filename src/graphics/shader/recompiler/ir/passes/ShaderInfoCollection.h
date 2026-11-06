#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

namespace Libs::Graphics::ShaderRecompiler::IR {

// Checks live, well-formed pixel attribute references against the runtime input contract.
// Run after dead-code elimination. Malformed IR remains CollectShaderInfo's responsibility.
[[nodiscard]] const char* UnsupportedPixelInputReferences(const Program& program,
                                                         ShaderStageInputInfo input_info);

// Completes the immutable shader interface after resource tracking. On failure Program::info and
// all completion state remain unchanged.
void CollectShaderInfo(Program& program, ShaderStageInputInfo input_info);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERINFOCOLLECTION_H_ */

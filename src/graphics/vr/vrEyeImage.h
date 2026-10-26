#pragma once

#include "graphics/host_gpu/renderer/image/imageInfo.h"
#include "graphics/shader/shaderBindings.h"
#include <array>

namespace Libs::Graphics::Vr {

// Narrow GT7 type-0x10 reprojection layer recipe, established from the guest builder:
// two otherwise identical T#s select array slices [0,0] and [1,1]. Both descriptions
// retain the complete two-layer allocation; layers[] selects the eye at copy/view time.
// Unknown formats, swizzles, layouts or nonzero mips fail closed, never become null eyes.
bool DescribeEyeImages(const std::array<ShaderTextureResource, 2>& textures,
                       std::array<ImageInfo, 2>& images, std::array<uint32_t, 2>& layers);

} // namespace Libs::Graphics::Vr

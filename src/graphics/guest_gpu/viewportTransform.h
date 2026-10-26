#ifndef KYTY_GRAPHICS_GUEST_GPU_VIEWPORT_TRANSFORM_H_
#define KYTY_GRAPHICS_GUEST_GPU_VIEWPORT_TRANSFORM_H_

#include <cstdint>

namespace Libs::Graphics::ViewportTransform {
// AMD PA_CL_VTE_CNTL: XY/Z_FMT mean already divided by W; W0_FMT means
// incoming W (otherwise reciprocal W). Bits 0..5 independently enable each
// viewport scale/offset. AMD Southern Islands Register Reference, PA:0x28818.
inline constexpr uint32_t DefaultControl = 0x43f;
inline constexpr uint32_t PositionFormatMask = 0x700;
inline constexpr uint32_t PredividedXY = 0x100;
inline constexpr uint32_t PredividedZ = 0x200;
inline constexpr uint32_t IncomingW = 0x400;

template<unsigned Axis>
constexpr float Scale(uint32_t control, float value) {
    static_assert(Axis < 3);
    return (control & (1u << (Axis * 2))) != 0 ? value : 1.0f;
}
template<unsigned Axis>
constexpr float Offset(uint32_t control, float value) {
    static_assert(Axis < 3);
    return (control & (2u << (Axis * 2))) != 0 ? value : 0.0f;
}

struct Viewport {
    float x, y, width, height, min_depth, max_depth;
};
constexpr Viewport Resolve(uint32_t control, float sx, float sy, float sz,
                           float ox, float oy, float oz, bool zero_to_one) {
    sx = Scale<0>(control, sx); sy = Scale<1>(control, sy); sz = Scale<2>(control, sz);
    ox = Offset<0>(control, ox); oy = Offset<1>(control, oy); oz = Offset<2>(control, oz);
    return {ox - sx, oy - sy, sx * 2.0f, sy * 2.0f,
            oz - (zero_to_one ? 0.0f : sz), sz + oz};
}
} // namespace Libs::Graphics::ViewportTransform
#endif

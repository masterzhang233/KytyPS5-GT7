#ifndef KYTY_VR_QUEUE_SELECTION_H_
#define KYTY_VR_QUEUE_SELECTION_H_

#include <cstdint>

namespace Libs::Graphics::Vr {

// Queue 0 belongs to the renderer; optional same-family readback owns queue 1.
// Keeping SteamVR in this family avoids transferring eye-image queue ownership.
constexpr uint32_t SelectDedicatedQueue(bool enabled, uint32_t family_queue_count,
                                       bool readback_in_graphics_family) noexcept {
    const uint32_t candidate = readback_in_graphics_family ? 2u : 1u;
    return enabled && candidate < family_queue_count ? candidate : UINT32_MAX;
}

} // namespace Libs::Graphics::Vr
#endif

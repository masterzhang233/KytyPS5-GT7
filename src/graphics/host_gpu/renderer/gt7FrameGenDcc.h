#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace Libs::Graphics::Gt7FrameGenDcc {
inline constexpr uint64_t CopyShader = 0xde16587e8056360aULL;
inline constexpr uint32_t Bytes = 0x28000;

// The captured byte-reordering shader reads and writes every byte exactly once
// for this dispatch. Integer U8 -> U8x2 format conversion preserves 0xff.
// Other descriptors/shapes are deliberately outside this proof.
struct CopyContract {
    uint64_t shader = 0;
    std::array<uint32_t, 4> source {}, destination {};
    std::array<uint32_t, 3> groups {}, threads {};
    uint32_t mode = 0, pitch = 0;
    bool thread_dimensions = false;
};
inline uint64_t Address(const std::array<uint32_t, 4>& descriptor) {
    return descriptor[0] | (uint64_t(descriptor[1] & 0xffffu) << 32u);
}
inline bool Matches(const CopyContract& copy) {
    const auto source = Address(copy.source), destination = Address(copy.destination);
    return copy.shader == CopyShader && copy.groups == std::array<uint32_t, 3>{8, 40, 1} &&
        copy.threads == std::array<uint32_t, 3>{8, 8, 1} && copy.mode == 0x41 &&
        !copy.thread_dimensions && copy.pitch == 8 && source != 0 && destination != 0 &&
        (source & 0xffu) == 0 && (destination & 0xffu) == 0 &&
        (source + Bytes <= destination || destination + Bytes <= source) &&
        (copy.source[1] & 0xffff0000u) == 0x00010000u &&
        copy.source[2] == Bytes && copy.source[3] == 0x00005204u &&
        (copy.destination[1] & 0xffff0000u) == 0x00020000u &&
        copy.destination[2] == Bytes / 2 && copy.destination[3] == 0x0001222cu;
}
inline bool FullyExpanded(std::span<const uint8_t> bytes) {
    return bytes.size() == Bytes && std::all_of(bytes.begin(), bytes.end(),
        [](uint8_t byte) { return byte == 0xff; });
}
}

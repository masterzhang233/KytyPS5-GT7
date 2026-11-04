#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace Libs::Graphics {
class RenderContext;
class CommandBuffer;
class Image;
struct PreparedBindings;
struct RenderColorInfo;
struct TextureBinding;

// Experimental, opt-in GT7 1080p -> 1440p reconstruction. The owner must finish
// all GPU work before destruction. No guest pixels are read back by this helper.
class Gt7Dlss {
public:
    explicit Gt7Dlss(RenderContext& context);
    ~Gt7Dlss();
    Gt7Dlss(const Gt7Dlss&) = delete;
    Gt7Dlss& operator=(const Gt7Dlss&) = delete;
    bool BeginTaa(CommandBuffer& buffer, const PreparedBindings& pixel,
                  std::span<const RenderColorInfo> colors);
    [[nodiscard]] bool Active() const;
    [[nodiscard]] bool MatchesScene(const RenderColorInfo& color) const;
    [[nodiscard]] Image* EffectLayer();
    bool Composite(CommandBuffer& buffer, const TextureBinding& low_final,
                   const RenderColorInfo& high_target, uint32_t effect_count);
    void CancelFrame(const char* reason, bool invalidate_history = false);
    void FrameBoundary();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

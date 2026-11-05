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
struct RenderDepthInfo;
struct ShaderStageRuntime;
struct ShaderVertexInputInfo;
struct ShaderComputeInputInfo;
struct TextureBinding;
struct GuestRange;
namespace Gt7FrameGen { struct FrameInputs; }

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
    // Includes metadata and cached aliases, so an unrelated clear may proceed
    // without accepting a write that changes a watched scene indirectly.
    [[nodiscard]] bool MatchesWatchedWriteRange(GuestRange range) const;
    [[nodiscard]] bool PreservesFrameGenerationMetadata(const ShaderComputeInputInfo& input,
        const PreparedBindings& bindings, uint32_t group_x, uint32_t group_y,
        uint32_t group_z, uint32_t mode);
    [[nodiscard]] Image* EffectLayer();
    bool Composite(CommandBuffer& buffer, const TextureBinding& low_final,
                   const RenderColorInfo& high_target, uint32_t effect_count);
    void CancelFrame(const char* reason, bool invalidate_history = false);
    // GPU recording thread only. Associate a verified ordinary motion producer's
    // camera with its depth/MV targets, then snapshot at the actual guest flip.
    [[nodiscard]] static constexpr bool IsFrameGenerationCameraShader(uint64_t hash) {
        // Verified position exports use object position * object matrix * the
        // current view-projection loaded from guest s[10:11] + 80.
        return hash == 0x48a3c58d37f8582fULL || hash == 0xd24c8423cd8ac4f3ULL ||
               hash == 0x38971d1e2fbabfc8ULL;
    }
    void ObserveFrameGenerationCamera(const ShaderStageRuntime& vertex,
                                      const RenderDepthInfo& depth,
                                      std::span<const RenderColorInfo> colors);
    // Bounded diagnostics only; never accepts an additional producer shader.
    void DescribeFrameGenerationDraw(const ShaderStageRuntime& vertex, uint64_t pixel_shader,
                                     const RenderDepthInfo& depth,
                                     std::span<const RenderColorInfo> colors,
                                     bool full_viewport, bool storage_writes);
    void NoteFrameGenerationSceneTarget(const RenderColorInfo& target);
    [[nodiscard]] bool FrameGenerationTonemapMatches(const ShaderVertexInputInfo& vertex,
                                                     const PreparedBindings& pixel,
                                                     const RenderColorInfo& target);
    void CaptureFrameGenerationHudless(CommandBuffer& buffer, const RenderColorInfo& target);
    [[nodiscard]] bool WatchesFrameGenerationOutput() const;
    [[nodiscard]] bool MatchesFrameGenerationOutput(const RenderColorInfo& target) const;
    void CheckFrameGenerationOutputDraw(std::span<const RenderColorInfo> targets,
                                        const PreparedBindings* pixel, uint64_t vertex_shader,
                                        uint64_t pixel_shader, bool ordinary_alpha_overlay);
    // Call before FrameBoundary, after waiting for this frame's prior present.
    // Allocations belong to the caller and must outlive its GPU present work.
    bool CopyFrameGenerationInputs(CommandBuffer& buffer, Gt7FrameGen::FrameInputs& inputs,
                                    const Image& presented_source);
    void FrameBoundary();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

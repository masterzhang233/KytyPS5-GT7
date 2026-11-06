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
namespace Gt7FrameGenCamera { struct RasterState; }

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
    // Same cancellation, with bounded owned diagnostics for the actual writer.
    // Details are reported only by the existing rate-limited scene gate.
    void CancelSceneWriterFrame(const char* reason, uint64_t vertex_shader,
                                uint64_t pixel_shader, const char* details);
    // GPU recording thread only. Associate a verified ordinary motion producer's
    // camera with its depth/MV targets, then snapshot at the actual guest flip.
    // Accept the original independently verified producers and programs whose
    // final position calculation has been proved by the shader compiler.
    [[nodiscard]] static bool IsFrameGenerationCameraShader(const ShaderStageRuntime& vertex);
    void ObserveFrameGenerationCamera(const ShaderStageRuntime& vertex,
                                      const RenderDepthInfo& depth,
                                      std::span<const RenderColorInfo> colors);
    // Bounded diagnostics only; never accepts an additional producer shader.
    void DescribeFrameGenerationDraw(const ShaderStageRuntime& vertex, uint64_t pixel_shader,
                                     const RenderDepthInfo& depth,
                                     std::span<const RenderColorInfo> colors,
                                     bool full_viewport, bool storage_writes,
                                     const Gt7FrameGenCamera::RasterState& raster);
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

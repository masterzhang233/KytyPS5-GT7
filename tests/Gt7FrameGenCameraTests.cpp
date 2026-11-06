#include "graphics/host_gpu/renderer/gt7FrameGenCamera.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <array>
#include <fstream>
#include <string_view>

using namespace Libs::Graphics::Gt7FrameGenCamera;

// Optional local regression corpus: a u32 count followed by records of 48
// current-matrix float32 values and 8 previously decoded values (near, far,
// FOV, jitter XY, position XYZ). No game/shader corpus is shipped with tests.
int ProbeRegression(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    uint32_t count = 0;
    if (!stream.read(reinterpret_cast<char*>(&count), sizeof(count)) || !count || count > 4096) return 1;
    for (uint32_t i = 0; i < count; ++i) {
        std::array<Matrix, 3> matrices {};
        std::array<float, 8> expected {};
        if (!stream.read(reinterpret_cast<char*>(matrices.data()), sizeof(matrices)) ||
            !stream.read(reinterpret_cast<char*>(expected.data()), sizeof(expected))) return 1;
        Parameters camera;
        const char* reason = nullptr;
        if (!Decode(matrices[0], matrices[1], matrices[2], 1920, 1080, camera, &reason)) {
            std::fprintf(stderr, "FAIL: captured camera %u: %s\n", i, reason); return 1;
        }
        const std::array<float, 8> actual {camera.near_plane, camera.far_plane, camera.fov_y,
            camera.jitter_ndc_x, camera.jitter_ndc_y, camera.position[0], camera.position[1], camera.position[2]};
        if (actual != expected) {
            std::fprintf(stderr, "FAIL: captured camera %u parameters changed\n", i); return 1;
        }
    }
    if (stream.peek() != std::char_traits<char>::eof()) return 1;
    std::printf("GT7 camera probe regression: %u captures, identical parameters\n", count);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--probe-regression") return ProbeRegression(argv[2]);
    if (argc != 1) return 1;
    // Captured ordinary motion producer, capture 1 / draw 1513. The first
    // three matrices are the CPU bytes from camera +80, +144, +208, respectively.
    const Matrix vp {
        1.1349549293518066f, -0.3600919246673584f, -8.845429277926087e-8f, 0.02653620019555092f,
        0.20283201336860657f, 2.018294095993042f, 2.3088702505447145e-8f, -0.006926587782800198f,
        0.028776023536920547f, -0.023857396095991135f, 3.3320907277811784e-6f, -0.9996238946914673f,
        -304.8881530761719f, 92.25137329101562f, 0.009789979085326195f, 63.016109466552734f};
    const Matrix view {
        0.9840980768203735f, -0.17563261091709137f, -0.02653620019555092f, 0.0f,
        0.17587128281593323f, 0.9843887686729431f, 0.006926587782800198f, 0.0f,
        0.024905405938625336f, -0.011483397334814072f, 0.9996238946914673f, 0.0f,
        -264.35955810546875f, 44.984375f, -63.016109466552734f, 1.0f};
    const Matrix projection {
        1.1532959938049316f, 0, 0, 0, 0, 2.0503039360046387f, 0, 0,
        5.2738749218406156e-5f, -0.0003130598342977464f, 3.3333444662275724e-6f, -1,
        0, 0, 0.010000033304095268f, 0};
    int failures = 0;
    const auto check = [&](bool good, const char* name) {
        if (!good) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    Parameters camera;
    check(Decode(vp, view, projection, 1920, 1080, camera), "captured camera accepted");
    check(std::abs(camera.near_plane - 0.01f) < 0.000001f &&
          std::abs(camera.far_plane - 3000.0f) < 0.01f, "captured depth endpoints");
    check(std::abs(camera.fov_y - 52.0f * 3.14159265f / 180.0f) < 0.000001f, "captured 52-degree vertical FOV");
    // Independently confirm camera position against the capture's world-position
    // header at +4,+8,+12; those header values are not inputs to Decode.
    check(std::abs(camera.position[0] - 266.38427734375f) < 0.001f &&
          std::abs(camera.position[1] - 2.647624969482422f) < 0.001f &&
          std::abs(camera.position[2] - 70.09295654296875f) < 0.001f, "captured world-position header");
    for (const float depth : {0.0f, 0.1f, 0.5f, 1.0f}) {
        const double distance = projection[14] / (depth + projection[10]);
        const double reconstructed = double(camera.near_plane) * camera.far_plane /
            (double(camera.near_plane) + depth * (camera.far_plane - camera.near_plane));
        check(std::abs(distance - reconstructed) <= 0.00001 * distance, "reverse-Z reconstruction across range");
    }
    // BB draw 1510 (20261105-002330 capture): DX clip, both Z planes enabled,
    // viewport Z = 1*z + 0, incoming W, no polygon bias or clip-space rewrite.
    // 489 paired raster records have this same state. The 105 sampled Monza
    // program keys also use incoming W with clip-space conversion disabled.
    const RasterState captured_raster {
        1.0f, 0.0f, Libs::Graphics::ViewportTransform::IncomingW,
        true, true, true, false, false, false, false, false};
    check(CompatibleRaster(captured_raster), "captured BB camera raster accepted");
    check(!CompatibleRaster({}), "missing camera raster state rejected");
    const auto rejected_raster = [&](auto mutate, const char* name) {
        auto raster = captured_raster;
        mutate(raster);
        check(!CompatibleRaster(raster), name);
    };
    // An otherwise valid camera does not justify accepting remapped depth.
    // A common half-range remap sends the far plane from depth 0 to 0.5; the
    // same FSR camera would reconstruct about 2 cm instead of the actual 3 km.
    const double remapped_far_distance = double(camera.near_plane) * camera.far_plane /
        (double(camera.near_plane) + 0.5 * (camera.far_plane - camera.near_plane));
    check(remapped_far_distance < 0.021 && camera.far_plane > 2999.0f,
          "raster remapping invalidates a valid camera's depth reconstruction");
    rejected_raster([](auto& r) { r.z_scale = 0.5f; }, "viewport depth scale rejected");
    rejected_raster([](auto& r) { r.z_offset = 0.5f; }, "viewport depth offset rejected");
    rejected_raster([](auto& r) { r.z_scale = -1.0f; r.z_offset = 1.0f; },
                    "second reverse-Z conversion rejected");
    rejected_raster([](auto& r) { r.z_scale = std::nextafter(1.0f, 2.0f); },
                    "depth mapping is exact, not an epsilon match");
    for (const float nonfinite : {std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity()}) {
        rejected_raster([&](auto& r) { r.z_scale = nonfinite; }, "nonfinite raster scale rejected");
        rejected_raster([&](auto& r) { r.z_offset = nonfinite; }, "nonfinite raster offset rejected");
    }
    auto negative_zero_offset = captured_raster;
    negative_zero_offset.z_offset = -0.0f;
    check(CompatibleRaster(negative_zero_offset), "signed zero preserves depth mapping");
    rejected_raster([](auto& r) { r.dx_clip_space = false; }, "unverified clip convention rejected");
    rejected_raster([](auto& r) { r.min_z_clip_enabled = false; }, "near depth clamp rejected");
    rejected_raster([](auto& r) { r.max_z_clip_enabled = false; }, "far depth clamp rejected");
    rejected_raster([](auto& r) { r.poly_offset_front = true; }, "front-face depth bias rejected");
    rejected_raster([](auto& r) { r.poly_offset_back = true; }, "back-face depth bias rejected");
    rejected_raster([](auto& r) { r.clip_space_conversion = true; }, "post-proof clip conversion rejected");
    rejected_raster([](auto& r) { r.position_format = 0; }, "reciprocal W conversion rejected");
    rejected_raster([](auto& r) { r.position_format |= Libs::Graphics::ViewportTransform::PredividedZ; },
                    "predivided depth conversion rejected");
    rejected_raster([](auto& r) { r.position_format |= Libs::Graphics::ViewportTransform::PredividedXY; },
                    "predivided XY conversion rejected");
    rejected_raster([](auto& r) { r.indexed_viewport = true; }, "unverified indexed viewport rejected");
    rejected_raster([](auto& r) { r.layer_output = true; }, "unverified render layer rejected");
    auto single_layer = captured_raster;
    single_layer.layer_output = true;
    single_layer.layer_limit = 1;
    check(CompatibleRaster(single_layer), "Layer export constrained to the sole attachment layer accepted");
    // Match the emitter's actual packed Layer semantics, including values that
    // would route away from the scene without the bound-layer specialization.
    for (uint32_t raw : {0u, 1u, 2u, 2047u, 2048u, UINT32_MAX}) {
        const auto masked = raw & 0x7ffu;
        const auto emitted = (single_layer.layer_limit == 0 || masked < single_layer.layer_limit)
            ? masked : 0u;
        check(emitted == 0, "one-layer specialization always writes the scene layer");
    }
    for (uint32_t limit : {0u, 2u, 2048u, UINT32_MAX}) {
        auto unverified_layers = single_layer;
        unverified_layers.layer_limit = limit;
        check(!CompatibleRaster(unverified_layers), "unbounded or multi-layer routing rejected");
    }
    single_layer.indexed_viewport = true;
    check(!CompatibleRaster(single_layer), "one-layer attachment does not prove viewport routing");
    auto wrong_vp = vp;
    wrong_vp[12] += 1.0f;
    check(!Decode(wrong_vp, view, projection, 1920, 1080, camera), "stale view-projection rejected");
    check(camera.near_plane == 0.0f, "rejection clears old camera");
    wrong_vp = vp;
    wrong_vp[10] = 0.0f;
    check(!Decode(wrong_vp, view, projection, 1920, 1080, camera), "tiny stale reverse-Z coefficient rejected");
    auto wrong_view = view;
    wrong_view[0] *= 1.1f;
    check(!Decode(vp, wrong_view, projection, 1920, 1080, camera), "scaled/non-rigid view rejected");
    auto dirty_projection = projection;
    dirty_projection[14] = std::numeric_limits<float>::quiet_NaN();
    check(!Decode(vp, view, dirty_projection, 1920, 1080, camera), "nonfinite constants rejected");
    // The decomposition is tied to the shader's actual VP, never to bytes in
    // an unrelated +976 block. Nonfinite/stale values in any current matrix
    // must still fail, including cases where the old tail would have matched.
    for (size_t matrix = 0; matrix < 3; ++matrix) {
        for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()}) {
            std::array<Matrix, 3> current {vp, view, projection};
            current[matrix][0] = bad;
            check(!Decode(current[0], current[1], current[2], 1920, 1080, camera),
                  "nonfinite current matrix rejected independently of tail");
        }
    }
    auto wrong_projection = projection;
    wrong_projection[8] += 0.0001f;
    check(!Decode(vp, view, wrong_projection, 1920, 1080, camera),
          "different current jitter without matching VP rejected");
    wrong_projection = projection;
    wrong_projection[14] *= 1.1f;
    check(!Decode(vp, view, wrong_projection, 1920, 1080, camera),
          "different current depth mapping without matching VP rejected");
    wrong_view = view;
    wrong_view[12] += 1.0f;
    check(!Decode(vp, wrong_view, projection, 1920, 1080, camera),
          "different current camera pose without matching VP rejected");
    check(!Decode(vp, view, projection, 1440, 1080, camera), "wrong render aspect rejected");
    check(!Decode(vp, view, projection, 0, 1080, camera), "zero dimensions rejected");
    std::printf("GT7 frame-generation camera checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}

#include "graphics/host_gpu/renderer/gt7FrameGenCamera.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Libs::Graphics::Gt7FrameGenCamera;

int main() {
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
    auto unjittered = projection;
    unjittered[8] = unjittered[9] = 0;
    int failures = 0;
    const auto check = [&](bool good, const char* name) {
        if (!good) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    Parameters camera;
    check(Decode(vp, view, projection, unjittered, 1920, 1080, camera), "captured camera accepted");
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
    auto wrong_vp = vp;
    wrong_vp[12] += 1.0f;
    check(!Decode(wrong_vp, view, projection, unjittered, 1920, 1080, camera), "stale view-projection rejected");
    check(camera.near_plane == 0.0f, "rejection clears old camera");
    wrong_vp = vp;
    wrong_vp[10] = 0.0f;
    check(!Decode(wrong_vp, view, projection, unjittered, 1920, 1080, camera), "tiny stale reverse-Z coefficient rejected");
    auto wrong_view = view;
    wrong_view[0] *= 1.1f;
    check(!Decode(vp, wrong_view, projection, unjittered, 1920, 1080, camera), "scaled/non-rigid view rejected");
    auto dirty_projection = projection;
    dirty_projection[14] = std::numeric_limits<float>::quiet_NaN();
    check(!Decode(vp, view, dirty_projection, unjittered, 1920, 1080, camera), "nonfinite constants rejected");
    auto mismatched_unjittered = unjittered;
    mismatched_unjittered[10] *= 2.0f;
    check(!Decode(vp, view, projection, mismatched_unjittered, 1920, 1080, camera), "mismatched unjittered projection rejected");
    check(!Decode(vp, view, projection, unjittered, 1440, 1080, camera), "wrong render aspect rejected");
    check(!Decode(vp, view, projection, unjittered, 0, 1080, camera), "zero dimensions rejected");
    std::printf("GT7 frame-generation camera checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}

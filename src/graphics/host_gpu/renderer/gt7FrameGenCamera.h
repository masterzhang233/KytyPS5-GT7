#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Libs::Graphics::Gt7FrameGenCamera {

using Matrix = std::array<float, 16>;

struct Parameters {
    float near_plane = 0.0f;
    float far_plane = 0.0f;
    float fov_y = 0.0f;
    float jitter_ndc_x = 0.0f;
    float jitter_ndc_y = 0.0f;
    std::array<float, 3> position {}, right {}, up {}, forward {};
};

// Observed in the ordinary GT7 motion producer (VS 48a3c58d37f8582f):
// s[10:11]+80 is its current view-projection; +144 is view; +208 is projection.
// +976 is the corresponding unjittered projection. Validate all four matrices,
// rather than treating arbitrary CPU data as camera parameters. This first
// implementation accepts the captured finite reverse-Z perspective layout only.
inline bool Decode(const Matrix& view_projection, const Matrix& view,
                   const Matrix& projection, const Matrix& unjittered,
                   uint32_t width, uint32_t height, Parameters& output,
                   const char** failure = nullptr) {
    output = {};
    if (failure) *failure = nullptr;
    const auto reject = [&](const char* reason) { if (failure) *failure = reason; return false; };
    if (!width || !height) return reject("zero extent");
    for (const auto* matrix : {&view_projection, &view, &projection, &unjittered})
        for (float x : *matrix) if (!std::isfinite(x)) return reject("nonfinite matrix");
    for (uint32_t i : {1u, 2u, 3u, 4u, 6u, 7u, 12u, 13u, 15u})
        if (projection[i] != 0.0f) return reject("perspective zero pattern");
    if (projection[11] != -1.0f || projection[0] <= 0.0f || projection[5] <= 0.0f ||
        projection[10] <= 0.0f || projection[14] <= 0.0f ||
        view[3] != 0.0f || view[7] != 0.0f || view[11] != 0.0f || view[15] != 1.0f)
        return reject("finite reverse-Z/view layout");
    for (size_t i = 0; i < projection.size(); ++i)
        if (unjittered[i] != ((i == 8 || i == 9) ? 0.0f : projection[i])) return reject("unjittered projection mismatch");
    if (std::abs(projection[8] * float(width) * 0.5f) > 2.0f ||
        std::abs(projection[9] * float(height) * 0.5f) > 2.0f ||
        std::abs(double(projection[5]) / projection[0] - double(width) / height) > 0.0001)
        return reject("jitter/aspect contract");

    // Row-vector convention, right-handed camera looking down -Z. Its rigid
    // view transform and view*projection must agree with what the shader uses.
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (size_t k = 0; k < 3; ++k) dot += double(view[i * 4 + k]) * view[j * 4 + k];
            if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 0.0001) return reject("view not orthonormal");
        }
    }
    const double determinant =
        double(view[0]) * (double(view[5]) * view[10] - double(view[6]) * view[9]) -
        double(view[1]) * (double(view[4]) * view[10] - double(view[6]) * view[8]) +
        double(view[2]) * (double(view[4]) * view[9] - double(view[5]) * view[8]);
    if (std::abs(determinant - 1.0) > 0.0001) return reject("view handedness");
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            double product = 0.0, magnitude = 0.0;
            for (size_t k = 0; k < 4; ++k) {
                const double term = double(view[i * 4 + k]) * projection[k * 4 + j];
                product += term;
                magnitude += std::abs(term);
            }
            // Relative to the contributing terms, including cancellation. An
            // absolute epsilon would wrongly accept a stale reverse-Z column,
            // whose far-plane coefficients are only a few millionths.
            const double tolerance = 0.00001 * (magnitude > 1e-12 ? magnitude : 1e-12);
            if (std::abs(product - view_projection[i * 4 + j]) > tolerance) return reject("view-projection product mismatch");
        }
    }
    const double a = projection[10], b = projection[14];
    const auto near_plane = float(b / (1.0 + a));
    const auto far_plane = float(b / a);
    const auto fov_y = float(2.0 * std::atan(1.0 / projection[5]));
    if (!std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        near_plane <= 0.0f || far_plane <= near_plane || fov_y <= 0.0f || fov_y >= 3.14159265f)
        return reject("near/far/FOV range");
    output.near_plane = near_plane;
    output.far_plane = far_plane;
    output.fov_y = fov_y;
    output.jitter_ndc_x = projection[8];
    output.jitter_ndc_y = projection[9];
    for (size_t i = 0; i < 3; ++i) {
        double position = 0.0;
        for (size_t j = 0; j < 3; ++j) position -= double(view[12 + j]) * view[i * 4 + j];
        output.position[i] = float(position);
        output.right[i] = view[i * 4];
        output.up[i] = view[i * 4 + 1];
        output.forward[i] = -view[i * 4 + 2];
        if (!std::isfinite(output.position[i])) { output = {}; return reject("nonfinite camera position"); }
    }
    return true;
}

} // namespace Libs::Graphics::Gt7FrameGenCamera

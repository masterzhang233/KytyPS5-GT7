#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace Libs::VrTracker2 {

constexpr int32_t ERROR_NOT_INITIALIZED = static_cast<int32_t>(0x8a740001u);
constexpr int32_t ERROR_INVALID_ARGUMENT = static_cast<int32_t>(0x8a740006u);
constexpr uint32_t WORK_BYTES = 32u * 1024u * 1024u;
constexpr uint32_t WORK_ALIGNMENT = 64u * 1024u;

// Layouts confirmed at GT7's actual SysV call sites, not inferred from the
// argument-free unresolved-import stubs. See tracker-abi/evidence.json.
struct MemoryQueryParameter {
    uint32_t size;
    std::array<uint8_t, 28> reserved;
};
struct MemoryRequirement {
    uint32_t size;
    uint32_t bytes;
    uint32_t alignment;
    std::array<uint8_t, 20> reserved;
};
struct InitializeParameter {
    uint32_t size;
    uint32_t reserved04;
    uint32_t thread_parameter08;
    uint32_t thread_parameter0c;
    uint64_t thread_parameter10;
    uint64_t thread_parameter18;
    uint64_t work_address;
    uint32_t work_bytes;
    uint32_t work_alignment;
    std::array<uint8_t, 24> reserved30;
};
struct GetResultParameter {
    uint32_t size;
    int32_t device_handle;
    uint64_t reserved08;
    uint64_t predicted_display_time;
    std::array<uint8_t, 24> reserved18;
};

static_assert(sizeof(MemoryQueryParameter) == 0x20);
static_assert(sizeof(MemoryRequirement) == 0x20);
static_assert(sizeof(InitializeParameter) == 0x48);
static_assert(offsetof(InitializeParameter, work_address) == 0x20);
static_assert(offsetof(InitializeParameter, work_bytes) == 0x28);
static_assert(offsetof(InitializeParameter, work_alignment) == 0x2c);
static_assert(sizeof(GetResultParameter) == 0x30);
static_assert(offsetof(GetResultParameter, predicted_display_time) == 0x10);

template <size_t N> inline bool IsZero(const std::array<uint8_t, N>& bytes) {
    for (auto value : bytes) if (value != 0) return false;
    return true;
}

inline int32_t QueryMemory(const MemoryQueryParameter* parameter, MemoryRequirement* requirement) {
    if (parameter == nullptr || requirement == nullptr) return ERROR_INVALID_ARGUMENT;
    // Copy before writing: overlapping input/output must not change validation.
    MemoryQueryParameter input;
    std::memcpy(&input, parameter, sizeof(input));
    if (input.size != sizeof(input) || !IsZero(input.reserved)) return ERROR_INVALID_ARGUMENT;
    const MemoryRequirement output {sizeof(MemoryRequirement), WORK_BYTES, WORK_ALIGNMENT, {}};
    std::memcpy(requirement, &output, sizeof(output));
    return 0;
}

inline bool ValidInitialize(const InitializeParameter& parameter) {
    return parameter.size == sizeof(parameter) && parameter.reserved04 == 0 &&
           IsZero(parameter.reserved30) && parameter.work_address != 0 &&
           parameter.work_address % WORK_ALIGNMENT == 0 &&
           parameter.work_bytes >= WORK_BYTES && parameter.work_alignment == WORK_ALIGNMENT &&
           parameter.work_address <= UINT64_MAX - parameter.work_bytes;
}

inline bool ValidGetResult(const GetResultParameter& parameter) {
    return parameter.size == sizeof(parameter) && parameter.device_handle >= 0 &&
           parameter.reserved08 == 0 && IsZero(parameter.reserved18);
}

// A consumer-defined pose, deliberately not a guessed complete SDK result.
// GT7 uses xyz followed by a 4-byte gap, then an xyzw quaternion.
struct ConsumerPose {
    std::array<float, 3> position;
    std::array<float, 4> orientation;
};

inline bool PoseFromRigidTransform(const float (&matrix)[3][4], ConsumerPose& output) {
    for (const auto& row : matrix)
        for (float value : row) if (!std::isfinite(value)) return false;
    // Do not turn a bad tracking matrix into an apparently valid orientation.
    for (unsigned a = 0; a < 3; ++a) {
        for (unsigned b = 0; b < 3; ++b) {
            float dot = 0;
            for (unsigned k = 0; k < 3; ++k) dot += matrix[a][k] * matrix[b][k];
            if (std::abs(dot - (a == b ? 1.0f : 0.0f)) > 0.02f) return false;
        }
    }
    const float determinant =
        matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
        matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
        matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
    if (std::abs(determinant - 1.0f) > 0.03f) return false;

    ConsumerPose pose {};
    for (unsigned k = 0; k < 3; ++k) pose.position[k] = matrix[k][3];
    auto& q = pose.orientation;
    const float trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
    if (trace > 0) {
        const float s = 2.0f * std::sqrt(trace + 1.0f);
        q = {(matrix[2][1] - matrix[1][2]) / s, (matrix[0][2] - matrix[2][0]) / s,
             (matrix[1][0] - matrix[0][1]) / s, 0.25f * s};
    } else {
        unsigned i = matrix[1][1] > matrix[0][0] ? 1 : 0;
        if (matrix[2][2] > matrix[i][i]) i = 2;
        const unsigned j = (i + 1) % 3, k = (i + 2) % 3;
        const float s = 2.0f * std::sqrt(1.0f + matrix[i][i] - matrix[j][j] - matrix[k][k]);
        if (!(s > 0)) return false;
        q[i] = 0.25f * s;
        q[j] = (matrix[j][i] + matrix[i][j]) / s;
        q[k] = (matrix[k][i] + matrix[i][k]) / s;
        q[3] = (matrix[k][j] - matrix[j][k]) / s;
    }
    float norm = 0;
    for (float v : q) norm += v * v;
    if (!std::isfinite(norm) || norm < 0.5f) return false;
    norm = std::sqrt(norm);
    for (float& v : q) v /= norm;
    output = pose;
    return true;
}

inline bool ComposeEyePose(const float (&head)[3][4], const float (&eye_to_head)[3][4],
                           ConsumerPose& output) {
    ConsumerPose checked;
    if (!PoseFromRigidTransform(head, checked) || !PoseFromRigidTransform(eye_to_head, checked))
        return false;
    float eye[3][4] {};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column)
            for (unsigned k = 0; k < 3; ++k)
                eye[row][column] += head[row][k] * eye_to_head[k][column];
        eye[row][3] = head[row][3];
        for (unsigned k = 0; k < 3; ++k) eye[row][3] += head[row][k] * eye_to_head[k][3];
    }
    return PoseFromRigidTransform(eye, output);
}

} // namespace Libs::VrTracker2

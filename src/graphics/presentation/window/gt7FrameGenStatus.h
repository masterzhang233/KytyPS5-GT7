#pragma once

#include <atomic>
#include <cstdint>

namespace Libs::Graphics::Gt7FrameGen {

// Diagnostic state only. It never changes the renderer's acceptance contract.
enum class StatusReason : uint8_t {
    Off, Loading, WaitingSwapchain, WaitingInputs, SceneUnverified, CameraUnverified,
    HudlessUnverified, DuplicateFrame, InputUnavailable, InvalidInput, HistoryReset,
    WaitingDispatch, ActiveDispatch, DroppedFrame, RuntimeMissing, BuildUnsupported,
    QueuesUnavailable, SwapchainUnsupported, RuntimeError,
};

constexpr bool IsStatusError(StatusReason reason) {
    return reason >= StatusReason::RuntimeMissing;
}

constexpr const char* StatusReasonText(StatusReason reason) {
    switch (reason) {
    case StatusReason::Off: return "disabled by configuration";
    case StatusReason::Loading: return "loading runtime";
    case StatusReason::WaitingSwapchain: return "waiting for swapchain";
    case StatusReason::WaitingInputs: return "waiting for matched scene inputs";
    case StatusReason::SceneUnverified: return "DLSS scene not verified";
    case StatusReason::CameraUnverified: return "camera inputs not verified";
    case StatusReason::HudlessUnverified: return "HUD-less scene not verified";
    case StatusReason::DuplicateFrame: return "duplicate guest frame";
    case StatusReason::InputUnavailable: return "input copy unavailable";
    case StatusReason::InvalidInput: return "invalid frame inputs";
    case StatusReason::HistoryReset: return "resetting interpolation history";
    case StatusReason::WaitingDispatch: return "waiting for interpolation dispatch";
    case StatusReason::ActiveDispatch: return "2x interpolation dispatch active";
    case StatusReason::DroppedFrame: return "presenter dropped a frame";
    case StatusReason::RuntimeMissing: return "FSR runtime unavailable";
    case StatusReason::BuildUnsupported: return "build has no FSR support";
    case StatusReason::QueuesUnavailable: return "dedicated Vulkan queues unavailable";
    case StatusReason::SwapchainUnsupported: return "swapchain unavailable or non-SDR";
    case StatusReason::RuntimeError: return "FSR runtime error (see log)";
    }
    return "unknown";
}

struct StatusSnapshot {
    StatusReason reason = StatusReason::Off;
    // Successful non-reset interpolation command recording, NOT displayed frames.
    uint64_t generated_dispatches = 0;
};

// Status readers never acquire rendering/queue locks. Counters span swapchain
// recreations so resizing cannot make the session's generated count go backwards.
class StatusTracker {
public:
    void Set(StatusReason reason) { m_reason.store(reason, std::memory_order_release); }
    void Dropped() {
        auto reason = m_reason.load(std::memory_order_relaxed);
        while (reason != StatusReason::Off && !IsStatusError(reason) &&
               !m_reason.compare_exchange_weak(reason, StatusReason::DroppedFrame,
                                              std::memory_order_relaxed)) {}
    }
    void Generated(bool reset, int64_t now_ms) {
        if (reset) { Set(StatusReason::HistoryReset); return; }
        m_generated.fetch_add(1, std::memory_order_relaxed);
        m_last_generation.store(now_ms, std::memory_order_relaxed);
        Set(StatusReason::ActiveDispatch);
    }
    [[nodiscard]] StatusSnapshot Read(int64_t now_ms) const {
        StatusSnapshot result {m_reason.load(std::memory_order_acquire),
                               m_generated.load(std::memory_order_relaxed)};
        const auto last = m_last_generation.load(std::memory_order_relaxed);
        if (result.reason == StatusReason::ActiveDispatch &&
            (now_ms < last || now_ms - last >= 2000)) result.reason = StatusReason::WaitingDispatch;
        return result;
    }
    // Shared by all ordinary summaries: even rapidly oscillating rejection
    // reasons cannot cause per-frame logging. No sleep, lock or heap allocation.
    [[nodiscard]] bool TryReport(int64_t now_ms) {
        auto next = m_next_report.load(std::memory_order_relaxed);
        return now_ms >= next && m_next_report.compare_exchange_strong(
            next, now_ms + 5000, std::memory_order_relaxed);
    }
private:
    std::atomic<StatusReason> m_reason {StatusReason::Off};
    std::atomic<uint64_t> m_generated {0};
    std::atomic<int64_t> m_last_generation {0};
    std::atomic<int64_t> m_next_report {0};
};

} // namespace Libs::Graphics::Gt7FrameGen

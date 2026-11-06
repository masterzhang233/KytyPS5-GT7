#include "graphics/presentation/window/gt7FrameGenStatus.h"

#include <cstdio>
#include <thread>
#include <vector>

using namespace Libs::Graphics::Gt7FrameGen;

int main() {
    int failures = 0;
    const auto check = [&](bool good, const char* text) {
        if (!good) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; }
    };
    StatusTracker status;
    check(status.Read(0).reason == StatusReason::Off, "disabled until requested");
    status.Dropped();
    check(status.Read(0).reason == StatusReason::Off, "ordinary dropped frame does not request FG");
    status.Set(StatusReason::RuntimeMissing);
    status.Dropped();
    check(IsStatusError(status.Read(1).reason), "load failure visible without usable runtime");
    status.Set(StatusReason::WaitingInputs);
    status.Generated(true, 100);
    check(status.Read(100).generated_dispatches == 0 &&
          status.Read(100).reason == StatusReason::HistoryReset, "reset is not a generated interpolation");
    status.Generated(false, 200);
    check(status.Read(200).reason == StatusReason::ActiveDispatch &&
          status.Read(200).generated_dispatches == 1, "non-reset dispatch activates status");
    check(status.Read(2199).reason == StatusReason::ActiveDispatch &&
          status.Read(2200).reason == StatusReason::WaitingDispatch, "stale activity expires at two seconds");
    check(status.Read(199).reason == StatusReason::WaitingDispatch, "backwards time never claims activity");
    status.Set(StatusReason::HudlessUnverified);
    check(status.Read(201).reason == StatusReason::HudlessUnverified, "rejection clears active status immediately");
    status.Set(StatusReason::WaitingSwapchain);
    status.Generated(false, 300);
    check(status.Read(300).generated_dispatches == 2, "count survives swapchain status changes");
    status.Dropped();
    check(status.Read(301).reason == StatusReason::DroppedFrame, "dropped frame clears active status");
    unsigned reports = 0;
    for (int64_t ms = 0; ms < 60000; ++ms) {
        status.Set(ms % 2 ? StatusReason::WaitingInputs : StatusReason::ActiveDispatch);
        if (status.TryReport(ms)) ++reports;
    }
    check(reports == 12, "oscillating states limited to twelve ordinary reports per minute");
    check(!status.TryReport(59999) && status.TryReport(60000) && !status.TryReport(60000),
          "same interval cannot be claimed twice");
    StatusTracker concurrent;
    std::atomic<unsigned> claimed {0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (unsigned attempt = 0; attempt < 1000; ++attempt)
            if (concurrent.TryReport(1000)) claimed.fetch_add(1);
    });
    for (auto& worker : workers) worker.join();
    check(claimed.load() == 1, "concurrent callers share one reporting interval");
    return failures != 0;
}

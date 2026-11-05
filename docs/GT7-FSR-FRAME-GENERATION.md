# Tohi-GT7 desktop DLSS SR + Vulkan FSR 3.1 frame generation

This experimental desktop path preserves the existing fixed 1920x1080 to
2560x1440 DLSS Super Resolution configuration (Quality hint, preset K). AMD
FidelityFX supplies only 2x frame generation and its Vulkan presentation timing.
Guest rendering, flips, vblank events and input sampling retain their original
cadence. SteamVR is outside this integration.

## Activation

The Tohi-GT7 1.0.0 launcher provides one default-enabled **DLSS** checkbox. It
selects the fixed desktop 1080p-to-1440p DLSS configuration, FSR 3.1.4 2× frame
generation and the occlusion correction together. There is no DLSS quality menu.
The developer activation methods below remain opt-in; the raw emulator does not
enable frame generation merely because the dependency is present.

Build with the optional FidelityFX API dependency and place its matching
`amd_fidelityfx_vk.dll` beside `kyty_emulator.exe`. Set `KYTY_GT7_FSR_FG=1`, or
invoke `scripts/GT7-Test-DLSS.ps1 -FrameGeneration` with the same arguments as
the DLSS SR playtest. Omitting the switch leaves frame generation disabled.
The script rejects combining `-Baseline` and `-FrameGeneration`.

`-OcclusionGuard` requests an additional experimental correction and requires
frame generation. Its environment switch is `KYTY_GT7_FSR_FG_OCCLUSION_GUARD=1`;
the default is off. It identifies large motion discontinuities across the real
frames and keeps current real pixels in the affected part of the generated
image. The rule operates over the entire image, without a fixed wheel rectangle.
It preserves interpolation elsewhere. Reset or mismatched history skips the
correction until a directly adjacent motion history is available.

An offline prototype on eight captured consecutive pairs removed the prominent
old-car fragments while replacing roughly 1.9–3.5% of screen pixels. This is a
local temporal fallback, not proof of correct intermediate-time reconstruction:
road/shadow seams and repeated-motion patches remain possible. Those screenshots
do not establish real-time quality or performance. The candidate uses no extra
mask dilation, since widening it added other affected pixels with little benefit.

The production GPU helper passed 23 standalone replay checks on the local
RTX 3080: 13 corrected outputs and masks exactly matched the offline prototype,
and ten reset/warmup/history-gap cases preserved the original output bytes.
The checks include RGBA/BGRA output and three consecutive records in one command
buffer to exercise resource reuse. Isolated GPU timestamps were about 0.23 ms
per correction; this excludes game rendering and is not a real-time benchmark.
The Vulkan validation layer was unavailable, so these are output comparisons,
not validation-layer coverage.

The subsequent 2026-11-05 live race exited normally after the player reported
substantially reduced wheel ghosting with no new visible artifact. Four complete
F10 capture pairs confirmed that frame generation and the correction were active;
the session log recorded at least 4,200 corrections. This accepts the tested
car/camera/track combination, not every possible scene. Neither the capture count
nor the window's base-frame FPS establishes the final display frame rate.

Ordinary play uses silent API logging to avoid very large traces during long
sessions. Fatal messages remain in `stdout.log`. Pass `-DiagnosticLogging` for
a short run when inspecting the detailed FG counters described below. The
occlusion guard emits a limited activity summary even in silent mode.
`-CaptureTemporalInputs` automatically
enables detailed logging. Close the diagnostic session after collecting evidence.

The local `Start-GT7-DLSS-Playtest.ps1` and
`Start-GT7-DLSS-FSR-Playtest.ps1` wrappers share progress across both playtest
output roots. GT7's offline progress is stored in
`_DownloadData/PPSA01317/gt7save.bin`. Each new session copies the latest saved
progress from a normally finished, successful playtest, including its save and
trophy directories; failed or active runs are not selected automatically.
Old runs and original saves are retained. Exit normally after saving so the next
SR or FG test can resume that progress without repeating the opening sequence.

The dependency is AMD FidelityFX SDK 1.1.4 (FSR 3.1.4), rather than FSR 4.
The existing NVIDIA SR runtime remains separate. Missing runtime, unsupported
queues or failure to create the FG swapchain retain ordinary presentation.

For a targeted input diagnostic, add `-CaptureTemporalInputs`, enter the affected
scene, then press F9 once. This requests four frames of the verified TAA and
tone-map inputs, with complete 1080p depth/motion/color and a 512 MiB raw budget.
The script sets `KYTY_FG_MANUAL_ONLY=1` to prevent automatic writer diagnostics
or scheduled captures from consuming that budget before F9. Larger textures may
be sampled; inspect each raw metadata file for completeness. Capture readbacks
can stall rendering and must not be used to measure performance.

## Input and synchronization contract

- Capture the validated TAA path's reversed depth and clean current-to-previous
  motion in render pixels. Each prepared presenter frame owns independent GPU
  copies; presentation never reads a subsequently reused DLSS scratch image.
- Derive projection metadata from the recognized GT7 motion producer's camera
  constants. Camera validity, render dimensions, jitter, frame identity and
  successful DLSS composition must match. Do not substitute guessed camera
  parameters.
- Record FidelityFX prepare on the presentation queue with asynchronous
  interpolation disabled. The replacement swapchain owns the extra queues
  used for acquire and presentation pacing.
- Missing inputs, duplicate frames, discontinuities and unsupported overlays
  disable interpolation for that presentation and reset history for resumption.
- Generated frames do not generate guest flip-completion or vblank events.
- Capture the matched final tone-map target immediately before HUD draws, then
  validate that the guest flip presents the same target. Convert this private
  HUD-less image with exactly the same dimensions, format and linear blit as the
  final backbuffer. Supply it as FidelityFX `HUDLessColor` for HUD detection.
- Retain HUD-less resources through interpolation submitted by `QueuePresent`;
  each frame's retirement signal follows that submission on the graphics queue.
  Unknown writes to the tracked final target invalidate its HUD-less snapshot.
- Consumed compute clears and sparse fills check their actual destinations,
  including image aliases and compression/stencil metadata. Clearing unrelated
  resources must not discard the captured scene; overlapping or unknown writes
  still invalidate it.
- Ordinary compute checks use resolved buffer bindings, including null bindings
  and their actual accessible ranges. The verified display-DCC copy shader may
  preserve the HUD-less snapshot only for its exact dispatch/descriptor contract,
  isolated complete DCC source and destination ranges, and a full 160 KiB source proven
  CPU/GPU clean and entirely `0xff` (expanded). No GPU readback is performed.
  Mixed metadata, fast-clear values and unknown writers retain normal rejection.

## Validation and current limits

`GT7 FSR FG` log entries distinguish valid input copies, prepare calls and
successful interpolation dispatches. A loaded DLL, a requested setting or a
prepare count alone does not establish that a generated frame was displayed.
`non_reset_total` counts successful interpolation calls with valid history;
reset-only calls are recorded separately and do not establish active 2x output.
The existing window FPS counter counts host presentation calls, not the
replacement swapchain's generated images.

The window title shows `FSR FG: standby` until a successful, non-reset
interpolation dispatch has occurred recently, then shows `FSR FG: 2x`. This is
an activity indicator, not a measurement of display FPS. Unsupported scenes can
remain in standby while DLSS Super Resolution continues working.

The playtest manifest records requested options separately from verification.
In particular, requesting Vulkan validation is insufficient when the log says
`no validation layer: VK_LAYER_KHRONOS_validation`; that run has no active
validation layer and must not be reported as validation-clean.

The initial whole-backbuffer interpolation caused severe HUD flicker in the
user's race test while the 3D scene was stable. The revised path requires the
matched HUD-less image and uses FidelityFX's comparison-based UI handling.
It does not reconstruct a separate alpha UI plane. Transparent or low-contrast
HUD edges can still challenge this method and need visual race testing, as do
disocclusions, camera cuts and menu transitions. Missing or unverified HUD-less
inputs and emulator overlays suspend frame generation. No racing-quality or
performance result is implied by a successful build or graphics initialization
test.

The first HUD-less candidates did not establish sustained interpolation: the
first rejected a recognized glyph Mesh stage, and the next was invalidated by
unrelated consumed compute clears. Visual improvement during those runs is not
evidence of HUD stability with frame generation active. The subsequent race
recorded at least 600 non-reset dispatches and the user reported no UI flicker,
but frequent compute-buffer rejections interrupted interpolation. A subsequent
race with resolved-buffer guards recorded at least 3,078 non-reset dispatches;
bounded scheduling coverage increased from 31.14% to 87.10%. These are callback
counts, not displayed FPS, and the runs are not a controlled performance A/B.
No expanded DCC copy acceptance or rejection was logged in that race, so the
improvement cannot be attributed to that exception. Periodic resets remain.

Before the occlusion correction, the observed artifact was ghosting around the
user's own wheels and lower bumper in an external view. It disappeared or was
noticeably reduced with FG off.
Matched real/generated/HUDless images and depth/motion captures reproduce old
car fragments appearing over the road. In the sampled wheel regions, real and
HUDless colors are identical, excluding HUD classification as the local cause.
Depth and motion are finite and aligned; eight consecutive transitions support
the existing motion sign, scale and time interval.

A CPU reconstruction of the fixed SDK's primary game-vector field and depth
clip reproduces the prominent ghost in two transitions to within one average
8-bit channel level. Fast road motion samples previous car color, and the
depth test permits blending it into current road color. Other captured frames
are only partly reproduced because the reconstruction omits optical flow and
inpainting. This motivated the motion-discontinuity correction described above.
The subsequent live acceptance run confirmed reduced wheel ghosting in the tested
car/camera/track combination. This does not establish a universal cause or a
complete correction across other scenes. Sustained display-level 2× output and
measured display FPS remain unverified.

The separate `-CaptureGeneratedFrames` diagnostic mode requests SDK output
pairs with F10. It requires frame generation and cannot be combined with
`-CaptureTemporalInputs` or a graphics-only check. F10 requests four pairs of
the real backbuffer and the SDK interpolation output, plus matching HUDless
display color, private R32_FLOAT depth and RG32_FLOAT backward pixel motion.
The v2 manifest records the same frame identity, image format, raw byte layout,
camera planes and per-file completion. Raw inputs are tightly packed little-endian
floats; do not treat them as PNGs or normalize their values before analysis.
There are at most four batches per process. At the supported maximum dimensions,
readback is bounded to 276,480,000 bytes per batch (about 263.7 MiB), and all four
batches contain at most 80 image/raw data files. Results and frame metadata are
written under `fg-output-captures`.
It does not enable the full API trace; `-DiagnosticLogging` remains a separate
opt-in. Do not press F9 at the same time: input dumping can stall the frames being
examined. Finishing a batch may pause briefly for readback and image encoding.
These images are taken after generation dispatch and before final presentation,
so a captured interpolation output does not prove that it reached the display.
When the occlusion guard is requested, capture follows that correction and each
pair records whether it was applied and which previous motion frame it used.
Check frame IDs, guest-frame IDs, reset status and batch completion before
comparing wheel silhouettes. This diagnostic mode is not a wheel-ghosting fix.

The 2026-11-05 output-only capture produced 16 non-reset pairs in four completed
batches, with 11 usable adjacent-real/generated/current-real triplets. Generated
images contain displaced lower-wheel/bumper contours absent in both adjacent
real frames. Batch 2's last saved pair crosses a gap and cannot use the preceding
saved image as its previous real frame. The subsequent v2 matching-input capture
completed three batches with 12 non-reset pairs (36 PNGs and 24 raw inputs),
including eight adjacent transitions. Its analysis supplies the evidence above.
These captures still do not measure actual display FPS.

For local acceptance, verify ordinary SR with FG disabled, then FG proxy
initialization, sustained successful interpolation, no Vulkan validation or
device-loss errors, clean exit and resize recovery. Finally drive the same
race/camera with FG enabled and disabled to compare HUD stability, motion and
input response. Preserve the prior stable SR-only installation for comparison.

## Sources

- [AMD FSR 3](https://gpuopen.com/fidelityfx-super-resolution-3/)
- [AMD FidelityFX SDK 1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55)
- Local Vulkan presentation reference: `demons-launcher-reference/src/local/frame-gen.cpp`.

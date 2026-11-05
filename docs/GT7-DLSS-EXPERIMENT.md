# Tohi-GT7 desktop DLSS integration

This is an experimental integration for the verified GT7 1.71 desktop render path.
The Tohi-GT7 launcher exposes one **DLSS** checkbox, enabled by default for desktop
GT7. It selects the fixed configuration below together with the separate
[Vulkan FSR 3.1 frame-generation path](GT7-FSR-FRAME-GENERATION.md) and its
occlusion correction. There are no adjustable DLSS quality settings in the GUI.
Direct emulator launches still require the explicit opt-in environment variables.
The normal emulator build does not link NVIDIA NGX. Its optional C ABI backend is
built separately in `tools/dlss_backend` with an external NVIDIA SDK/runtime.
No game data, SDK files or generated captures belong in this repository.
The local test runtime inspected on 2026-11-04 reports DLL product version
`310.9.16.0`. The current renderer requests transformer preset K with the Quality
hint, using a fixed 1920 by 1080 input and 2560 by 1440 reconstruction. This is a
custom 75% input scale, not the standard recommended Quality input resolution.
The NVIDIA backend implements Super Resolution only; frame generation is supplied
by AMD FidelityFX, not NVIDIA DLSS Frame Generation.

## Evidence and limits

The 1080p desktop profile produces genuine 1920 by 1080 color, reversed depth and
motion inputs. Its temporal history has a 3840 by 2160 allocation but a 1920 by
1080 active rectangle. History allocation size must not determine jitter or
motion scale. A persistent NGX feature has sequentially evaluated four captured
real 1080p frames to 3840 by 2160 on an RTX 3080, with reset only on the first evaluation.
Every output RGB component was finite. Native-resolution v1 DLAA replay still
passes. These are offline GPU results, not game FPS or temporal-quality results.

The independent backend also passed a host-owned Vulkan-device smoke test across
four frames. The host submits and waits; the backend only records GPU commands.
Its ABI header compiles as C and carries no NVIDIA SDK types or C++ allocations.

The fixed 1080p-to-1440p Quality configuration also passed a four-frame host smoke
and four real captured frames on the RTX 3080. The runtime recommends 1707 by 960
but accepts input sizes from 1280 by 720 through 2560 by 1440. GPU preparation
matched the CPU conversion bit for bit. Existing Performance and DLAA replays
still pass. The composite passes eight synthetic GPU cases, including separate
1080:1440:2160 input/reconstruction/guest proportions, within one FP16 ULP of the
reference. Reconstruction resampling uses explicit FP32 bilinear weights to
avoid hardware filter precision differences at the 1.5x guest composition ratio.

The 2026-11-04 race playtest exited normally after 586 seconds. Its periodic logs
prove at least 13,551 evaluations and 5,400 composites, but also at least 8,100
cancelled frames. A long interval continued evaluating without any composites;
this is intermittent coverage, not full-race DLSS support. The player reported
no obvious visual artifacts. Background shader compilation was still active,
so this run does not establish performance or complete visual correctness.

The following 346-second diagnostic race also exited normally, but the player
reported flicker. Captures identified two additional alpha-over full-screen
postprocess variants and a scene overlay whose quad count changes: 12 and 18
vertices were captured, and 24/36 were logged. Requiring exactly 30 vertices
rejected these draws and repeatedly reset temporal history. Their shader,
texture, blend and depth conditions still need to match; quad count alone must
not invalidate this side-effect-free replay. This explains a concrete source of
intermittent fallback, but does not prove that all visible flicker has one cause.

## Experimental render path

1. Match the verified TAA shader pair and complete input/output shapes. Validate
   current constants, viewport and scissor. Prepare private RGBA16F color, R32F
   depth, RG32F clean backward motion and an invalid-motion mask on the GPU.
2. Evaluate NGX into a private 1440p HDR image with the Quality hint. Keep the original guest TAA/history
   and low-resolution postprocessing chain running.
3. Replay the verified full-screen postprocess blend and depth-tested scene-overlay draw
   into a cleared 1080p RGBA16F effect layer. Preserve original geometry,
   textures, viewport, scissor and depth tests; do not repeat guest queries.
   Accumulate premultiplied RGB `P` and coverage `A` with straight-source RGB
   blending and `One / OneMinusSrcAlpha` alpha blending.
4. At the verified 1080p-to-4K scale draw, combine `DLSS * (1-A) + P` in linear
   HDR. Preserve the original low-resolution final color as the spatial fallback
   for invalid-motion regions. Sample the private 1440p reconstruction with
   normalized linear coordinates into this original 4K guest surface. The
   existing 4K tone mapping and UI then run, with final presentation to the
   2560 by 1440 window. The guest pipeline has not been converted wholly to 1440p.

The prototype accepts a narrow observed sequence: an optional verified full-screen
postprocess draw, an optional following overlay made of triangle-list quads and
one four-vertex scale draw. Every
scene writer that actually occurs must be verified; the scale draw may follow
zero, one or two accepted effects. An untouched effect layer has `P=A=0`, so
omitting effects is compatible with the same composite equation. The scale quad, white tint,
identity matrix and linear normalized clamp sampler are checked at runtime.
Unexpected scene writers or shapes must discard the private result. A missing
backend, unsupported device or unmatched pass leaves the original render path.
Storage writes in graphics shaders and compute clears/fills that bypass binding
validation conservatively discard the private result. Incomplete sequences end
at the GPU recording thread's presentation preparation, independently of the
display thread's frame counter.
Skipping a guest frame without evaluation resets NGX history before the next
accepted temporal pass, because the motion vectors refer to the previous guest frame.
Only a `composited` log entry confirms an SR result reached the game's 4K scene;
an `evaluated` entry alone does not.
Composite diagnostics also count successful frames with zero, one and two scene
effects, including the first success in each category. Unknown scene writers and
unavailable effect pipelines are reported separately. Bounded writer diagnostics
record shader/draw/texture state and automatically request frame-graph captures
for up to four distinct rejected writer signatures per process. These diagnostics
do not relax any draw or resource validation.

The temporal hook runs after image discovery/view creation but before the guest
draw's descriptor commit. `TextureBinding::layout` is still unset at this point;
checking it rejected every temporal pass in the first race playtest. The helper
now validates and restores the actual backing layout of color, depth and motion.
The matched history image is not read by the helper and need not be initialized
on the first frame. `KYTY_GT7_DLSS_TRACE=1` requests one frame-graph capture after
the first successful evaluation to inspect the subsequent effect/scale sequence.

The effect-layer equation is exact at the same low-resolution sample in real
arithmetic. Separately interpolating `P` and `A` at 4K is not pixel-identical to
blending and quantizing the original low-resolution RGB target before scaling.
Edges can differ. Game exposure remains unverified (the experiment uses NGX auto
exposure and pre-exposure 1). Preset K does not support BiasCurrentColor; fallback
compositing does not repair NGX's internal history in invalid-motion regions.
Camera cuts, other views/races, long-term stability and FPS require in-game tests.
The four-frame offline preview is not native-4K ground truth.

## Running a local playtest

Build/install the emulator normally; build the external backend separately using
its README. `scripts/GT7-Test-DLSS.ps1` takes explicit game, emulator, backend,
runtime, prior save/cache and output paths. It runs the existing compatibility
preflight, creates a private save/cache copy and sets environment variables only
on its child process. `-CheckOnly` runs preflight without starting a game or
creating a session. It does not initialize graphics. `-GraphicsCheckOnly` instead
runs preflight followed by the real window/Vulkan initialization and shutdown,
without loading guest code or copying user saves. Its separate session manifest
and exit code distinguish it from a compatibility-only check; these two check
switches are mutually exclusive. `-Baseline` uses the original 1080p path. Both use a 2560 by
1440 desktop window while the final scene/UI render surface remains 4K.

Offline progress lives in `_DownloadData/PPSA01317/gt7save.bin`. The script
copies `_SaveData/PPSA01317`, `_Trophies/PPSA01317` and
`_DownloadData/PPSA01317` from the latest saved, normally finished successful
playtest; shader cache still comes from the supplied seed. `-ProgressSearchRoots`
defaults to the current output root. The local `Start-GT7-DLSS-Playtest.ps1` and
`Start-GT7-DLSS-FSR-Playtest.ps1` wrappers search both SR and FG output roots, so
tests share the latest completed progress without repeating the opening
sequence. Active, failed and graphics-only runs are not selected automatically.
If no eligible saved playtest exists, the script uses the supplied seed. Old
runs and original saves are retained. Exit normally after saving to make a
session eligible for the next test. `-CheckOnly` shows the selected progress
source and save hash without creating a session.

Ordinary playtests use `--printf-direction Silent` to avoid an unbounded per-API
trace while driving or waiting in menus. Fatal messages remain in `stdout.log`.
Use `-DiagnosticLogging` for a short session when DLSS/FG counters or detailed
API traces are needed; `-CaptureTemporalInputs` enables this logging automatically.
The manifest records the selected logging mode. Close diagnostic sessions after
collecting the needed evidence.

No F9 or F10 capture is necessary for ordinary play. Keep the session manifest and
logs to verify which passes were evaluated, composited or rejected. The launcher
sets the following variables when its **DLSS** checkbox is enabled. For direct
emulator launches, the opt-in variables are `KYTY_GT7_DLSS=1`,
`KYTY_GT7_DLSS_BACKEND=<absolute DLL>` and
`KYTY_GT7_DLSS_RUNTIME=<absolute directory containing nvngx_dlss.dll>`.
VR is excluded from this experiment.

## NVIDIA device-creation startup fix

The first local playtest crashed at `vkCreateDevice`, before any DLSS evaluation,
on the tested RTX 3080 / 596.49 setup. Disabling the RTSS Vulkan layer changed the
access violation into `VK_ERROR_INITIALIZATION_FAILED`; it did not fix startup.
Independent device probes with the same extensions and features passed, showing
that those probes alone did not cover the emulator's initialization environment.

Opting into DLSS now also runs the existing early CUDA initialization attempt
before the emulator reserves guest virtual addresses. The temporary address
reservation floor remains at least 1024 GiB, above the ordinary low guest range
(`HOST_USER_MAX`; the emulator also has extended mappings). Loading only
`nvcuda.dll` did not solve the reproduced failure; the complete early call was
needed. The tested call returned CUDA status 2 while Vulkan initialization still
succeeded, so that CUDA result is logged and is not used to silently disable
DLSS. Vulkan device creation and NGX remain independently checked. Ordinary
launches retain their previous behavior unless DLSS or `KYTY_CUDA_EARLY_INIT=1`
is explicitly enabled. No RTSS settings or global environment are changed.

This is a tested startup workaround; the NVIDIA driver's internal failure
mechanism has not been established. A successful graphics-only check does not
prove that a race has evaluated/composited DLSS frames or establish performance.

The new immediate initialization/shutdown check also exposed a presenter shutdown
deadlock: a synchronous SDL title callback waited for the main thread while that
thread joined the presenter. Shutdown now pumps the main-thread SDL callbacks
until the presenter exits, before joining and destroying the window. A separate
priority-worker condition-variable stop notification is also protected by its
wait mutex to avoid a lost wakeup. `KYTY_SHUTDOWN_TRACE=1` writes opt-in subsystem
and graphics cleanup boundaries to stderr for diagnosing early exits.

Input conversion and NGX must run outside rendering, through the scheduler's
raw-command handoff. This drains queued recording and invalidates cached Vulkan
state before foreign commands. Images and features must live through GPU
completion. Parameter validation can fall back before recording. Any NGX recording
failure other than `INVALID_ARGUMENT` can leave partial commands: the experimental
session must terminate if the shared primary buffer cannot be discarded safely.
Never silently submit a possibly poisoned buffer.

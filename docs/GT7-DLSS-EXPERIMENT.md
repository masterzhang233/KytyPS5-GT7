# GT7 desktop DLSS experiment

This is a developer experiment for the verified GT7 1.71 desktop render path.
It is disabled by default and is not part of the launcher's supported settings.
The normal emulator build does not link NVIDIA NGX. Its optional C ABI backend is
built separately in `tools/dlss_backend` with an external NVIDIA SDK/runtime.
No game data, SDK files or generated captures belong in this repository.
The local test runtime inspected on 2026-11-04 reports DLL product version
`310.9.16.0`. The backend requests transformer preset K in Performance mode.
This integration implements Super Resolution only, with no frame generation.

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

## Experimental render path

1. Match the verified TAA shader pair and complete input/output shapes. Validate
   current constants, viewport and scissor. Prepare private RGBA16F color, R32F
   depth, RG32F clean backward motion and an invalid-motion mask on the GPU.
2. Evaluate NGX into a private 4K HDR image. Keep the original guest TAA/history
   and low-resolution postprocessing chain running.
3. Replay the verified motion-blur blend and depth-tested scene-overlay draw
   into a cleared 1080p RGBA16F effect layer. Preserve original geometry,
   textures, viewport, scissor and depth tests; do not repeat guest queries.
   Accumulate premultiplied RGB `P` and coverage `A` with straight-source RGB
   blending and `One / OneMinusSrcAlpha` alpha blending.
4. At the verified 1080p-to-4K scale draw, combine `DLSS * (1-A) + P` in linear
   HDR. Preserve the original low-resolution final color as the spatial fallback
   for invalid-motion regions. The existing 4K tone mapping and UI then run.

The prototype accepts a narrow observed sequence: an optional blur draw, an
optional following 30-vertex overlay draw and one four-vertex scale draw. Every
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

No F9 capture is necessary for a normal playtest. Keep the session manifest and
logs to verify which passes were evaluated, composited or rejected. Normal
launches do not enable this path. The opt-in variables are `KYTY_GT7_DLSS=1`,
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

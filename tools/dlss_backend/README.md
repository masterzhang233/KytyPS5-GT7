# External NGX Vulkan backend

This independent Windows x64 target records DLSS SR/DLAA into a **borrowed host
Vulkan device and command buffer**. Its standalone tests exercise synthetic inputs
or captured GT7 inputs; these tests do not establish in-game integration, image
quality or performance.

`kyty_dlss_backend.h` is the complete version 1 C ABI. It includes Vulkan C types
and no NGX headers. The DLL alone includes/links the externally installed NVIDIA
SDK. The smoke host is built with `/MD`, the DLL with `/MT`; no C++ objects or CRT
allocations cross that boundary. SDK/runtime files are neither downloaded nor
copied by this target. NVIDIA's SDK/runtime terms still apply; a C ABI is a
technical boundary, not a licensing determination.

## Build and run

Use a Visual Studio developer shell with CMake/Ninja and MSVC or clang-cl:

```powershell
cmake -S tools/dlss_backend -B C:/work/dlss-backend-build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DKYTY_NGX_SDK_ROOT=C:/external/DLSS
cmake --build C:/work/dlss-backend-build
C:/work/dlss-backend-build/kyty_dlss_backend_smoke.exe `
  C:/work/dlss-backend-build/kyty_dlss_backend.dll `
  C:/external/DLSS/lib/Windows_x86_64/rel C:/work/dlss-backend-smoke
```

The SDK checkout must contain `include/nvsdk_ngx_vk.h` and
`lib/Windows_x86_64/x64/nvsdk_ngx_s.lib`. The runtime directory must contain
`nvngx_dlss.dll`. All smoke arguments and backend runtime/log paths are absolute.
The DLL checks that a loaded `nvngx_dlss.dll` is the file in the requested runtime
directory. The host should also load the backend DLL by an absolute path with
restricted DLL search flags, as the smoke does.

The smoke creates its own Vulkan instance/device/resources, loads only the C ABI,
and records four jittered synthetic 320x180 frames into a persistent 640x360
Performance/Preset K feature. It checks extension/ABI/format/unsafe-destroy
rejections, first-frame-only creation/reset, and every output RGB component for
finite, nonempty, nonconstant data after a NaN clear. Alpha is not checked because
NGX alpha upscaling is disabled. `smoke-report.json` and four RGBA16F payloads are
written to the output directory. The smoke waits on host-owned fences for
readback; the backend never submits work or waits. Validation layers are not
enabled by this smoke.

Add `--quality-1440p` to run the same four-frame checks at fixed 1920x1080 input
and 2560x1440 output with the Quality hint and Preset K. This is a custom 75%
input ratio, not the runtime's standard Quality recommendation. The report lists
the actual input, recommendation, supported minimum/maximum and range membership.
Production backend dimensions and Quality selection already use the existing ABI;
this option changes only the diagnostic host.

To validate the renderer's actual input-conversion compute shader, compile
`src/graphics/host_gpu/shaders/gt7_dlss_prepare.comp` with `glslangValidator -V`
and supply the verified four-frame 1920x1080-to-3840x2160 `gt7.ngx-replay.v2`
sequence produced by `tools/dlss_probe/prepare_capture.py`:

```powershell
C:/work/dlss-backend-build/kyty_dlss_backend_smoke.exe `
  C:/work/dlss-backend-build/kyty_dlss_backend.dll `
  C:/external/DLSS/lib/Windows_x86_64/rel C:/work/dlss-prepare-check `
  --prepare C:/captures/ngx-sr-inputs/sequence.json C:/work/gt7_dlss_prepare.spv
```

The same prepare command also accepts a four-frame 1920x1080-to-2560x1440
sequence with `quality: "quality"`, selecting the Quality hint for that replay.

This mode uploads the captured raw B10G11R11 color, RG16F motion and depth-aspect
bytes, executes that shader, then compares every converted color/depth/motion/mask
element bitwise against the independent CPU converter. A mismatch aborts before
NGX submission for that frame. Matching GPU images are fed directly to the external
backend using one persistent feature across all four captures. The report records
per-resource mismatch counts and output finite-RGB checks. This mode enables
`shaderStorageImageExtendedFormats` for the RG32F/R8 storage outputs. Raw depth
bytes are uploaded as R32F, so this verifies numerical conversion rather than the
live guest depth view's lifetime/layout. Invalid motion stays a zero placeholder;
the verified mask is not passed to Preset K and the saved NGX images have no host
fallback composite. These are diagnostic outputs, not a quality acceptance result.

The independent compositor test uses the renderer's actual
`gt7_dlss_composite.comp`, compiled with `glslangValidator -V`:

```powershell
C:/work/dlss-backend-build/kyty_dlss_backend_smoke.exe `
  C:/work/dlss-backend-build/kyty_dlss_backend.dll `
  C:/external/DLSS/lib/Windows_x86_64/rel C:/work/dlss-composite-check `
  --composite C:/work/gt7_dlss_composite.spv
```

It creates synthetic HDR base/effect/native images and masks at 17x11 and 34x22,
covering alpha 0/1/intermediate and -0.25/1.25, signed HDR output, four edges/corners,
interior mask transitions, dispatch bounds and half rounding. An independent double-precision CPU reference
evaluates normalized pixel-center linear-clamp sampling, the clamped 2x2 invalid
footprint, and `(1-A)*base+P` without alpha/RGB saturation, or native fallback. Every RGBA value must be finite,
alpha must equal one, and each RGB component must be within one FP16 ULP of the
nearest-even reference. Reports include ULP histograms; CPU/GPU raw images are
saved. The one-ULP threshold applies to these fixtures, not every possible HDR
input. This mode does not evaluate NGX or run the game's effect fragment shaders.
Use `--composite-resized` instead of `--composite` to exercise 21x15 inputs,
28x20 reconstructed color and 42x30 guest composition, preserving the live
1080:1440:2160 ratios. The independent reference linearly samples reconstructed
color as well as the effect/native images and applies the same acceptance checks.

## Device creation diagnostic

`kyty_dlss_device_probe` isolates Vulkan device creation without starting a game,
creating an NGX feature, or submitting GPU work:

```powershell
C:/work/dlss-backend-build/kyty_dlss_device_probe.exe `
  C:/work/dlss-backend-build/kyty_dlss_backend.dll `
  C:/external/DLSS/lib/Windows_x86_64/rel `
  C:/work/device-config.json C:/work/device-report.json
```

Example configuration:

```json
{
  "api_minor": 3,
  "reservations": "guest",
  "emulator_extensions": true,
  "emulator_features": true,
  "emulator_identity": true
}
```

`api_minor` accepts 2 or 3. Reservations are `none`, `guest`, or
`early_cuda_guest`; the last option reports `cuInit`'s result separately.
`guest` reserves the emulator's low and extended address intervals with its
16 MiB minimum-free-region rule, using ordinary reservations rather than mapped
guest data/placeholders. Real backend instance/device extension queries run after
these reservations. `emulator_extensions` adds the supported extension-name
snapshot from the renderer. `emulator_features` reproduces the captured RTX 3080
feature requests and two-queue configuration, requires API 1.3 and emulator
extensions, and is a device-specific diagnostic rather than a portable feature
selector. `omit_extension` can remove one extension for a controlled comparison.
`emulator_identity` uses the renderer's Vulkan application/engine names and
versions. Reports retain loaded NGX/CUDA/Vulkan/RTSS module paths at each stage
and the actual `vkCreateDevice` result. The probe creates no SDL window or Win32
surface and does not reproduce all injected-module or guest-memory-map behavior.

## Host call order

1. Load the DLL; obtain `KytyDlssGetApi` with `GetProcAddress`. Initialize
   `KytyDlssApi.struct_size` and request `KYTY_DLSS_ABI_VERSION`.
2. Before creating a Vulkan instance, call `query_instance_extensions` and merge
   its names into the enabled instance extension list. Before creating a device,
   call `query_device_extensions` for the selected physical device and enable its
   returned names. These calls depend on the supplied runtime; do not assume the
   names are permanently fixed. On the tested RTX 3080/runtime the requirements
   were instance `VK_KHR_get_physical_device_properties2`, and device
   `VK_NVX_binary_import`, `VK_NVX_image_view_handle`,
   `VK_KHR_buffer_device_address`, `VK_KHR_push_descriptor`.
3. Enable `bufferDeviceAddress` and `shaderInt64` when creating the device; use a
   graphics+compute queue family. `create_context` receives the host's actual
   enabled extension list and feature attestations, dispatch functions, fixed
   input/output extents, quality mode and paths. It checks support and initializes
   NGX without creating a Vulkan instance, device, queue, command pool or fence.
   Version 1 permits one active context per process and requires serialized API
   calls. The host must not use other NGX clients concurrently with this lifecycle.
4. `query_optimal` returns the runtime's recommendation for the requested output
   and quality. Context creation fixes extents; changing extents or mode requires
   host GPU completion and context recreation. Preset K is requested; the API does
   not claim that the runtime's effective model selection has been inspected.
5. Prepare actual pre-temporal HDR color, reversed depth and clean backward motion
   in **input render pixels**, with projection jitter removed from motion. Format
   requirements are RGBA16F, R32F and RG32F respectively; each is a one-mip,
   one-layer, single-sampled 2D color-aspect image covering exactly the input
   extent. The output is separate RGBA16F at the requested output extent. The host
   owns resource contents and must prevent aliasing, including aliased memory
   behind distinct image handles. ABI validation cannot inspect their pixels,
   image usage flags, views or memory allocations.
6. End dynamic rendering. In a recording primary command buffer on the selected
   family on the host's actual command-recording thread, synchronize input producers to shader reads and transition inputs to
   `SHADER_READ_ONLY_OPTIMAL`; synchronize prior output uses and transition output
   to `GENERAL`, with storage-image usage. Call `record_evaluate`. It records lazy
   feature creation once and evaluates the persistent feature. It neither begins
   nor ends command buffers, submits, waits, nor transfers queue ownership. All
   evaluations must execute in recording order on the same host queue. The first
   evaluation forces reset; later resets follow `reset_history`.
7. After recording, invalidate/rebind the host's cached pipeline, descriptors,
   push constants and dynamic state: NGX does not preserve Vulkan command state.
   Keep layouts tracked as input shader-read-only and output general. Add an
   appropriate shader-write-to-consumer barrier before reading/transferring the
   output. Keep all borrowed resources alive until host GPU completion. The next
   frame must not overwrite inputs or output while previous work still uses them;
   use the host scheduler's ordering/fences/timeline, without a per-frame idle.
8. At shutdown/recreation, wait in the **host** for all submitted work referencing
   the feature to complete (or discard commands that were never submitted), then
   call `destroy_context(context, 1, ...)`. Only then destroy the host device or
   unload the backend DLL. Passing zero is rejected; the flag is a host assertion,
   not a hidden backend wait.

`INVALID_ARGUMENT` from `record_evaluate` guarantees that this call recorded
nothing and did not poison the context. Every other non-OK recording status,
including internal exceptions and `POISONED`, requires abandoning the current
primary command buffer. Invalid ABI/format/dimension arguments are rejected
before recording. An NGX
creation/evaluation failure may leave partial commands in the command buffer and
marks the context poisoned. Discard that unsubmitted buffer; wait for any earlier
submitted feature work, then destroy/recreate the context. Do not submit a buffer
after a failed record call other than `INVALID_ARGUMENT`. If that buffer also
contains guest commands and the emulator cannot discard/recover it, terminate the
experimental session instead of silently continuing. Successful recording is not
a GPU-completion claim.

The fixed feature flags are HDR, reversed depth, low-resolution motion and auto
exposure. Motion scale is `(1,1)` after host conversion, MVJittered is disabled,
and no BiasCurrentColor mask is supplied for Preset K. The host explicitly supplies
frame time, pre-exposure and exposure scale; auto exposure is not proof of matching
the game's exposure pipeline. Invalid or missing game motion still needs a
separate host fallback policy.

## Integration scope

The emulator already enables `bufferDeviceAddress` and `shaderInt64` in
`src/graphics/presentation/window/vulkanWindow.cpp`. Optional NGX extension
discovery must happen before its instance/device creation; a DLL loaded after
device creation cannot add the NVX extensions. Renderer input conversion, guarded
TAA matching, effect-layer replay, fallback compositing and scheduler integration
remain host work. This target changes none of those paths and does not add a
user-facing DLSS toggle.

# Native Vulkan DLSS capability probe

This standalone Windows diagnostic creates its own Vulkan instance and device,
initializes NVIDIA NGX with a custom project ID, checks DLSS Super Resolution
availability, and queries Quality-mode rendering dimensions. An optional synthetic
mode also creates and evaluates a real DLSS feature on the GPU, then reads back
the output. It does not modify the emulator, run GT7, or establish game image
quality. The JSON report explicitly records these limits. Preset K is requested
as a hint; the report does not claim independent verification of the effective
model selected by the NVIDIA runtime or driver.

Build separately from the emulator, using an external NVIDIA DLSS SDK checkout:

```powershell
cmake -S tools/dlss_probe -B <build-directory> -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl `
  -DKYTY_NGX_SDK_ROOT=<external-sdk-directory>
cmake --build <build-directory>
& <build-directory>/kyty_dlss_probe.exe <runtime-directory> <report.json> 2560 1440
& <build-directory>/kyty_dlss_probe.exe <runtime-directory> <report.json> 640 360 --evaluate
```

Use an x64 Visual Studio developer environment. The probe uses the static CRT
and the SDK's `nvsdk_ngx_s.lib`; Debug requires `nvsdk_ngx_s_dbg.lib`. The external
SDK must contain `include/` and `lib/Windows_x86_64/x64/`. The runtime directory
must contain the genuine `nvngx_dlss.dll`. No frame-generation DLL is needed.
Without explicit dimensions, capability mode uses 2560 by 1440 and `--evaluate`
uses 640 by 360. Synthetic output is limited to 4096 by 4096. The first NVIDIA
Vulkan device is selected. Exit 0 means all requested operations succeeded;
exit 1 means a runtime/capability/evaluation failure; exit 2 means an invalid
argument count or a report-writing failure. NGX logs are stored beside the JSON
under `ngx-logs/`.

The synthetic scene contains a gradient/checker background and a foreground
rectangle with corresponding depth. It supplies zero motion and jitter, since
it evaluates one static frame with history reset. Colors are RGBA16F, depth is
R32F, motion vectors are RG16F, and output is RGBA16F. Uploads are transitioned
to shader-read-only layout and the output to general layout before evaluation.
An explicit GPU fence and transfer-to-host barrier precede readback. Output starts
as NaNs; success requires every RGB component to be finite and the output to be
nonzero and nonconstant. Alpha is not checked, because alpha upscaling is not
enabled. This does not test motion, temporal reconstruction quality, or game FPS.

Synthetic mode writes `synthetic-output.rgba16f` (little-endian, tightly packed
RGBA, dimensions in the JSON) and `synthetic-output.ppm` (RGB preview, clamped
to [0, 1], without a color-space conversion) beside the report. Use a separate
report directory for each run to retain its images. Vulkan validation is not
enabled by the probe itself; any separately requested validation layer must be
verified as loaded before interpreting the absence of validation messages.

## Real capture replay

The independent replay path accepts complete TAA input textures from a diagnostic
capture. It evaluates a persistent NGX feature over consecutive captured frames,
resetting history only on the first frame. It supports same-size DLAA, exactly
2x Performance-mode SR, and fixed 1920x1080-to-2560x1440 SR with a Quality hint,
with preset K requested. It neither enables DLSS in the
emulator nor demonstrates in-game performance.

```powershell
python tools/dlss_probe/prepare_capture.py <capture-directory> <prepared-directory> `
  --constants <capture-directory>/analysis-motion/ngx-input-constants.json
& <build-directory>/kyty_dlss_probe.exe <runtime-directory> <replay-report.json> `
  --replay <prepared-directory>/sequence.json
```

For a genuine 1920 by 1080 capture, add `--output-width 3840 --output-height 2160`
to the converter to evaluate 4K SR, or `--output-width 2560 --output-height 1440`
for fixed 1080p-to-1440p SR. The latter uses a custom 75% input ratio with the
Quality hint, not the standard recommended Quality ratio. The converter does not
downsample the inputs. Sequence v2 records input and output dimensions separately
and selects `dlaa`, `performance`, or `quality`; the replay still accepts existing
same-size v1 sequences. Its
top-level `quality` capability query is independent of the actual mode recorded
under `replay.quality`.

The converter requires NumPy. Its unit tests are in
`tests/gt7_prepare_capture_test.py`. It rejects missing, subsampled, inconsistent,
nonconsecutive, or malformed inputs. Constants schema 2 distinguishes the active
input rectangle from a larger history backing allocation; the latter is validated
but never supplied to NGX. Source R11G11B10 color is decoded to
RGBA16F, reversed depth remains R32F, and RG16F motion is converted to RG32F
backward pixel displacement. It preserves original resolution and records source
metadata and assumptions in the sequence. Shader constants must belong to the
same captured draws; values from another run are not interchangeable.

For the verified GT7 1.71 shader, let `D=(width/2,-height/2)`, `J` be TAA constant
offsets 80/84, and `M` be the captured motion texture. The candidate NGX inputs are
`motionPixels=-D*(M+J)` and `jitterPixels=-D*J`, with unit MV scales and MVJittered
disabled. Reversed depth, HDR, and MVLowRes are enabled. No external motion-vector
dilation is performed. Automatic exposure is experimental; game pre-exposure is
not yet established and is assumed to be 1 for this diagnostic.

GT7 invalid motion (`M.x < -1000`) cannot be treated as ordinary motion. The
converter writes zero placeholders and a separate `bias.r8` invalid-region mask.
The replay validates and retains that mask but does **not** send it as
BiasCurrentColor: the [NVIDIA programming guide, section 3.15](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf)
limits that feature to preset F. Invalid regions, their effect on history, and
production fallback behavior remain unresolved for preset K. A successful replay
only establishes that real inputs were submitted and finite, nonconstant RGB
outputs were produced; it does not certify motion quality, exposure, or temporal
stability. The report keeps those conclusions separate.

The implementation was validated against the official SDK at
[`9981c6350dc58c0ce21f024fd65e31e851c31689`](https://github.com/NVIDIA/DLSS/tree/9981c6350dc58c0ce21f024fd65e31e851c31689).
SDK headers, libraries, runtime DLLs, build output, and generated reports remain
outside this repository and are not included in normal emulator release packages.
The SDK and runtime retain their own NVIDIA license terms.

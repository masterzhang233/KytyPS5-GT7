# GT7 desktop internal resolution and SteamVR

This experimental Windows build adds GT7 internal scene scaling and native stereo output to
SteamVR. Supply your own unpacked game. The release contains no game executable, game resources,
save data, captured shaders, or generated caches.

The profiles support **PPSA01317, version 01.710.000**, with this exact `eboot.bin` SHA-256:

```text
9bef586d199c088b1e558333ba897507235dae2e148a9c6e3de93eedbb5f88bf
```

An unsupported executable, changed instruction bytes, missing patch or conflicting custom
patch is rejected. Patches apply only to the loaded process; the file on disk is not rewritten.
An unpacked game folder or its `eboot.bin` is required. These profiles do not support `.zar` archives.

## Launcher

The main window has a **GT7 / VR & Render** panel showing the selected mode, internal resolution
and desktop output. **Start in SteamVR** selects VR mode for the selected game and launches through
the same strict compatibility check. **VR & render settings** opens the game config at the GT7 controls.

Add your game folder to the existing launcher and select GT7. The GT7 settings offer:

- **Desktop 3D height:** Native, 1080, 1440 or 2160. Native retains the game's original controller.
- **SteamVR / Quest:** enables the game's stereo mode and the SteamVR bridge.
- **VR scale:** 100%, 110%, 120% or 125%. The desktop height selector is disabled while VR is selected.

The existing **Screen resolution** selector controls the desktop output separately. For example,
choose 1440 internal height and 2560×1440 screen output for the corresponding nominal 1:1 profile.
The internal height controls the native scene-scale input: special passes, shadow maps, LOD and
allocation sizes are not all forced to the selected dimensions.

GT7 profiles cannot be combined with a custom patch selected in the launcher's Patches dialog.
Disable that custom patch first. Existing game settings and other titles keep their original defaults.

## Chinese-first interface

Preview 2 opens the launcher in Simplified Chinese on a fresh installation. English remains
available from the global settings, and the selected launcher language is remembered after a
restart. Existing installations that explicitly selected a launcher language keep that choice.

New game configurations also default the emulated console language to Simplified Chinese. An
existing configuration with an explicitly saved console language is not changed. You can select
any supported console language in the game configuration dialog.

## Per-run diagnostics

Every GT7 launch attempt creates its own timestamped diagnostic directory under:

```text
%LOCALAPPDATA%\KytyPS5-GT7\logs
```

The launcher displays this location and provides **Open log folder**, **Copy latest log path**,
and **Export diagnostics** actions. The exported ZIP includes an allow-listed run summary,
compatibility-check output, emulator stdout/stderr and the emulator log. The summary records the
exact build revision and emulator SHA-256, the selected render mode, exit status and basic hardware
information so a report can be matched to the correct executable.

Nothing is uploaded automatically. The diagnostic bundle excludes the game, save data, shader
caches and launcher settings. Text emitted by the emulator can still contain local paths; review a
bundle before sharing it. Raw PDB and map files are intentionally omitted from the public package.

## Quest 3 through Steam Link

Start SteamVR, connect Quest 3 with Steam Link and keep the headset awake before launching VR.
The integration uses OpenVR; it does not change the system OpenXR runtime. SteamVR and Steam Link
must be installed separately. Disable in-game ray tracing if you encounter the previously observed
incorrect showroom reflections.

| VR scale | Nominal internal size per eye | Pixels relative to 100% |
| --- | --- | --- |
| 100% | 2400×2040 | 1.00× |
| 110% | 2640×2244 | 1.21× |
| 120% | 2880×2448 | 1.44× |
| 125% | 3000×2550 | 1.5625× |

These are the normal native width-factor path. The game's alternate hardware path is preserved.
These choices are independent of SteamVR's own resolution setting. Higher values require more GPU
time and memory; they do not promise a higher frame rate. The 100% profile still applies the
full-frustum compatibility patch, which avoids using an unsupported eye-tracked crop.

## Optional PowerShell entry point

The Qt launcher is the primary entry point. `scripts/GT7-Start.ps1` is a portable alternative for
Windows PowerShell 5.1 or newer. Paths below are examples; replace them with your own locations.

```powershell
# Validate only: no game, SteamVR session, cache or user-data directory is created.
.\scripts\GT7-Start.ps1 -GamePath 'D:\Games\GT7' -DesktopHeight 1440 -CheckOnly

# Desktop: Auto output is 2560x1440 for the 1440 internal preset.
.\scripts\GT7-Start.ps1 -GamePath 'D:\Games\GT7' -DesktopHeight 1440 -Fullscreen

# VR: 100% internal eye size; the desktop mirror defaults to 3840x2160.
.\scripts\GT7-Start.ps1 -GamePath 'D:\Games\GT7' -Mode VR -EyeScale 100 -Fullscreen

# Output can be chosen independently; data goes to the explicitly selected folder.
.\scripts\GT7-Start.ps1 -GamePath 'D:\Games\GT7\eboot.bin' -DesktopHeight 1080 `
  -OutputResolution 2560x1440 -UserDataRoot 'D:\GT7-user-data'
```

The script defaults to `%LOCALAPPDATA%\KytyPS5-GT7` for saves, logs and newly generated caches.
It does not import an old runtime's files. The Qt launcher retains its existing emulator working
directory behavior; extract the release into a writable folder. Existing saves are optional.
The script neither edits the game nor stores machine-specific paths in the shipped files.

Equivalent core requests are `--gt7-desktop-height native|1080|1440|2160`, or
`--vr --gt7-vr-scale 100|110|120|125`, together with `--game <eboot.bin>`.
Add `--gt7-check-only` to validate without starting the game. Desktop and VR profile requests are
mutually exclusive, and cannot be combined with `--game-patch`.

## Validation limits

The patch inputs were checked against the executable fingerprint, original instruction bytes and
existing float literals. Previous offline native-code fixtures exercised the sizing arithmetic and
descriptor construction; their GPU allocation/layout calls used recording doubles. They are not
proof of every live render target, frame rate, image quality or compatibility with another update.
This upstream integration requires its own runtime validation. No performance improvement is
claimed merely from selecting a profile.

This integration is maintained by masterzhang233 and is based on
[usa-ratt/KytyPS5-GT7](https://github.com/usa-ratt/KytyPS5-GT7), tag
`KytyPS5-2026-10-26-55a02de`, with the upstream KytyPS5 contributors' work retained.

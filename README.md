# CarbonMWVisuals 1.0.1

An ASI for **NFS Carbon v1.4 (`NFSC.exe`)** that replaces Carbon's shaders with **NFS Most Wanted**
style daytime versions: world/road/car lighting, sky and colour grading, plus dynamic sun shadows,
light tweaks and separate settings for the menus/garage (FE).

This file is for building and understanding the mod. Players: see `dist/CarbonMWVisuals_README.txt`
(also inside the release zip).

## Folder contents

| Path | Contents |
|---|---|
| `CarbonMWVisuals.sln`, `CarbonMWVisuals.vcxproj` | Visual Studio project (C++, Win32, `.asi` output) |
| `src/dllmain.cpp` | `DllMain`: initialisation order of all modules |
| `src/ShaderLoader.cpp` | Effect creation hook, loads `fx\*.fx` from the game folder |
| `src/Shadows.cpp` | Dynamic sun shadows + per-frame hook (debug hotkeys, FE) |
| `src/FrontEnd.cpp` | Menu/garage settings + MW reflection cubemaps |
| `src/Lights.cpp` | Light patches (world glows, AI headlight beams) |
| `src/TimeOfDay.cpp` | MW style time of day (moving sun + MW lighting data) |
| `src/Menu.cpp` | In-game settings overlay (Dear ImGui, hooks IDirect3DDevice9::Present/Reset) |
| `src/imgui/` | Dear ImGui 1.91.9 (MIT, see `src/imgui/LICENSE.txt`), core + DX9/Win32 backends |
| `src/Common.h/.cpp` | Shared helpers: log, hotkeys, CALL patching, ID3DXEffect access |
| `src/pch.h/.cpp`, `src/framework.h` | Precompiled header (as in the Visual Studio DLL template) |
| `dist/` | Files to copy into the game folder (`.fx` shaders + `.ini`) + the player readme |
| `tools/extract_feenv.py` | Extracts the FE reflection cubemaps from NFS Most Wanted (optional) |
| `build_mingw.sh` | Alternative build with MinGW g++ (no Visual Studio) |

## Build

**Visual Studio 2022** (toolset v143; other versions: right-click the project -> *Retarget Projects*)

1. Open `CarbonMWVisuals.sln`.
2. Select the **Release | x86** configuration. It must be x86/Win32 because the game is 32-bit.
3. *Build Solution*. The output is `bin\Release\CarbonMWVisuals.asi`.

The runtime is linked statically (`/MT`), so no VC++ Redistributable is needed. C++17.

**MinGW** (Linux / MSYS2): `./build_mingw.sh` -> `bin/mingw/CarbonMWVisuals.asi`.

Thunks and code caves are written for both compilers: `__declspec(naked)` + `__asm {}` for MSVC and
AT&T asm for GCC. See the `CMWV_MSASM` block in `src/Common.h`.

## Installation

Requires an ASI loader (e.g. Ultimate ASI Loader, usually already present if you use the Widescreen Fix).

```
NFS Carbon\
  glassreflectshader.fx             <- dist\glassreflectshader.fx (it really lives in the game folder, not fx\)
  fx\*.fx                           <- dist\fx\*.fx
  scripts\CarbonMWVisuals.asi       <- build output
  scripts\CarbonMWVisuals.ini       <- dist\scripts\CarbonMWVisuals.ini
  scripts\CarbonMWVisuals_FEEnv\    <- optional, output of tools\extract_feenv.py
```

The log is written to `CarbonMWVisuals.log` in the game folder. Every shader is listed as `[OK]`
(port version in use), `[orig]` (no replacement file) or `[FAILED]` (compile error; the original
shader is used automatically and the error message is logged).

## Main switches

`[Main] Enabled=0` turns the whole mod off (nothing is loaded or patched: vanilla Carbon).
`[Main] Mode` picks the cycle: `1` = MW day / Carbon night (24 h), `0` = MW daytime only.
`[Main] SyncTime=1` makes in-game and the menus/garage share one time (each keeps its own speed,
mode, stop and weather); needs `[FrontEndTimeOfDay] Enable=1`.

## How it works

- **Shader loader**: hooks the only `D3DXCreateEffectFromResourceA` call (0x72B6D4). When
  `fx\<name>.fx` exists, it is compiled with the game's own d3dx9_30 into the same effect pool, so
  `shared` parameters stay connected. Otherwise the original resource in the exe is used.
- **Sun shadows**: revives Carbon's shadow map code that is never called (`RenderShadowMap`
  0x72C9F0), with a fixed light camera, culling and casters.
- **Lights**: patches for the textured AI headlight beams, the headlight glow in reflections and
  the world light glows.
- **Time of day**: port of MW's TimeOfDay system (0x769360 / 0x769580). The in-game time moves
  back and forth between MinHour and MaxHour; the sun follows an arc whose noon is
  `mwSunDirection`, and the lighting blends between MW's four `timeofdaylighting` sets (midday /
  low sun, clear / overcast). The fx values are the noon look; every parameter is scaled by
  (MW value now / MW midday value). With `Mode=1` the clock runs a full 24 hours: around sunset
  every effect's `mwDayBlend` fades to 0 (Carbon's own night shading and visual treatment), the
  sun shadows fade out, and the world lights / AI headlight beams / glow come back
  (`[Lights] NightLights`; the beam/glow patches are switched at runtime). Settings in `[TimeOfDay]`.
- **FE**: while GameFlowState = 3 (menus/garage), shader parameters are replaced with the
  `[FrontEndFx]` values. The car reflection cubemap can be replaced with the MW one.

All memory addresses are hardcoded for **NFSC.exe v1.4**. Every patch checks the original bytes
first and logs a `WARNING` (without patching) if they do not match.

## In-game menu

Press **F12** (`[Menu] Key`) for an overlay with all time of day settings, adjustable with the
mouse: an *In-game* tab (`[TimeOfDay]`) and a *Menus / garage* tab (`[FrontEndTimeOfDay]`, the FE's
own clock, plus the MW reflection cubemap on/off and set), a *Global* tab (sun shadows on/off and
quality = `[Shadows] Size` for world and cars; a new size is used after restarting the game) and the
noon sun for both. Changes apply immediately;
*Save to .ini* writes everything it shows to `CarbonMWVisuals.ini` (mode to `[Main] Mode`, the
current time as `StartHour`, the stopped clock as `StopClock`, the noon sun as `[Sun] Direction`, the cubemap as `[FrontEnd] MWEnvMap` / `MWEnvMapSet`,
the shadows as `[Shadows] Enable` / `Size`).

## Debug mode

Troubleshooting tools are off by default. Set `[Debug] DebugMode=1` in the `.ini` to enable:

- the hotkeys below (all configurable in `[Debug]`);
- detailed logging of the first `LogFrames` in-game frames and an automatic diagnostics dump;
- `mwCarGlassDebug` in `car.fx` (otherwise it is forced to 0).

| Key (default) | `.ini` key | Function |
|---|---|---|
| F3 | `ReloadKey` | Re-read `[FrontEnd]`, `[FrontEndFx]` and `WorldFlares` from the `.ini` without restarting |
| F9 | `ShadowDebugViewKey` | Cycle the shadow debug view |
| F10 | `ShadowToggleKey` | Shadows on/off |
| F6 | `DiagnosticsKey` | Write shadow diagnostics to the log |
| F8 | `FlareSoloKey` | Show only one world light type at a time (logged as `SOLO type N`), to find types for `[Lights] WorldFlaresKeep` |

Key formats: `F1`..`F24`, `A`..`Z`, `0`..`9`, `NUMPAD0`..`NUMPAD9`, `INSERT`, `DELETE`, `HOME`,
`END`, `PAGEUP`, `PAGEDOWN`, `PAUSE`, `SCROLLLOCK`, `TAB`, `SPACE`, `BACKSPACE`, or a virtual-key
code (`0x72`, `114`). `NONE` disables a key.

## MW FE reflection cubemaps (optional)

The textures belong to EA and are not part of the source. The release zip includes them
(`scripts\CarbonMWVisuals_FEEnv\`); otherwise extract them from a copy of NFS Most Wanted:

```
python tools\extract_feenv.py "C:\...\NFS Most Wanted\GLOBAL\DYNTEX.BIN" "C:\...\NFS Carbon\scripts\CarbonMWVisuals_FEEnv"
```

This writes 24 DDS files. Enable them with `[FrontEnd] MWEnvMap=1`. If the files are missing, the
original Carbon cubemap is used.

## Compatibility

- ExtendedCustomization: safe. If EC already hooks the same call site, effects without an `.fx`
  file are passed on to EC.
- `[Shadows] Enable=0` only switches the shadows off (also at runtime from the menu); the per-frame
  hook (menu, FE, time of day, debug hotkeys) is installed either way.

# Performance guide

Three different places change how fast Dishonored 2 VR runs, and they are easy to mix up:

| Where | What it controls |
|---|---|
| **The mod**: launcher **Settings** page, or in game: pause menu > **VR Settings** | Render resolution per eye, anti-aliasing and upscaling (FSR, DLSS), performance mode, monitor view |
| **The game**: Options > Video and Advanced | Scene detail: shadows, fog, water, view distance, textures |
| **Your headset's PC app**: Virtual Desktop, Meta Quest Link or SteamVR | Refresh rate (72, 90 Hz), the base resolution the mod scales, frame smoothing (SSW, ASW, Motion Smoothing) |

While VR is on, the mod takes over several of the game's video options, so changing them in the game does nothing.
[Game settings](#game-settings) lists which ones still matter.

## Quick start

To hold 72 or 90 fps, start here and change one thing at a time:

1. **Headset app:** pick 72 Hz (the easiest) or 90 Hz. In Virtual Desktop, set Synchronous Spacewarp to **Disabled**.
2. **Mod, Resolution:** start at **1.00x**. Raise it only once the game holds the refresh rate.
3. **Mod, Anti-aliasing:** **DLSS Quality** (NVIDIA RTX) or **FSR 3 / FSR 4 Quality** to gain speed. DLAA and the FSR Native
   modes give a cleaner image but render every pixel, so they give no speed back.
4. **Game, Shadow Quality:** **Low** or Medium. It is the game setting most worth lowering (see below).
5. **Mod, Monitor view:** **Off** (fastest) or Right eye. Both eyes costs the most.
6. Still short? Turn **Performance mode** on in the launcher Settings. Read what it costs first ([below](#performance-mode)).
7. Turn **Save diagnostic logs** off when you are not reporting a bug. It captures images and causes short hitches.

## Why 36 or 45 fps

The headset shows a new frame every refresh. Each refresh rate gives every frame a time budget:

| Refresh rate | Budget per frame | Shown when a frame is late |
|---|---|---|
| 72 Hz | 13.9 ms | 36 fps |
| 80 Hz | 12.5 ms | 40 fps |
| 90 Hz | 11.1 ms | 45 fps |
| 120 Hz | 8.3 ms | 60 fps |

A frame that misses its slot waits for the next one. So a game that cannot quite hold 72 shows at **36 fps**, not 60 or 70.
When frames are far over budget, the mod locks itself to half rate so that motion stays even. It returns to full rate
within about half a second once frames fit again.

This means 36 or 45 fps often means "a little over budget", not "twice too slow". At 72 Hz, a 15 ms frame and a 27 ms frame
both show as 36 fps. Cutting a fifth of the work can be enough to get the full rate back.

## The mod's settings

### Resolution: the biggest lever

- Launcher: **Settings > Resolution**, 0.50x to 2.00x. In game: **VR Settings > Resolution**, 50% to 200%.
- It scales each eye's size compared with the size your headset app asks for. **The pixel count goes with the square:**

  | Resolution | Pixels compared with 1.00x |
  |---|---|
  | 1.00x | 1x |
  | 1.25x | 1.56x |
  | 1.50x | 2.25x |
  | 2.00x | **4x** |

- Going from 2.00x to 1.50x renders 56% of the pixels. Going from 1.50x to 1.25x renders 69%.
- **Your headset app has its own resolution setting, and the mod's Resolution multiplies it.** Examples: Virtual Desktop's
  VR Graphics Quality (Low to Godlike), the Meta Quest Link app's render resolution, SteamVR's Render Resolution. Raise
  resolution in one place only. Either leave the headset app at its default and use the mod's Resolution, or the other
  way round.
- Example from a bug report: Quest 3 over Link at 72 Hz with Resolution 2.00x renders 3648x3936 per eye. That is 4x the
  pixels the headset asked for, and it ran at 35.9 fps on an RTX 5080.

### Anti-aliasing and upscaling

| Anti-aliasing entry | Renders | Speed |
|---|---|---|
| Off | 100% | Fastest; edges shimmer |
| SMAA | 100% | Cheap edge smoothing |
| Game TAA | 100% | Leaves ghost trails on moving things in VR; not recommended |
| FSR 3 Native, FSR 4 Native, DLAA | 100% | Anti-aliasing only: costs extra, gives no speed |
| FSR 3 Quality, FSR 4 Quality | 77% | Faster |
| FSR 3 Balanced | 67% | Faster still, softer |
| DLSS Quality (NVIDIA RTX) | about 67% (NVIDIA picks it) | Faster |
| SSAA 125% | **sets Resolution to 1.25x** | 1.56x the pixels |
| SSAA 200% + SMAA | **sets Resolution to 2.00x** | 4x the pixels, the heaviest setting there is |

- The default follows your GPU: DLAA on NVIDIA RTX with DLSS, FSR 4 Native where FSR 4 runs, otherwise FSR 3 Native. These
  are full-resolution anti-aliasing modes. They are not speed settings.
- **The two SSAA entries also change Resolution.** If you once picked SSAA, check both rows when looking for speed.
- In game, **VR Settings > Upscaler scale** (50% to 100%) sets how much the FSR and DLSS modes render. Lower is faster and
  softer.
- DLSS needs your own `nvngx_dlss.dll` from another game (the mod does not ship it). The launcher looks through your Steam
  games for one; if it finds none, point it to one with **DLSS file**. Without it, the DLSS entries are not offered, and a
  saved DLSS choice runs as FSR 3.

### Performance mode

- **Off by default.** Launcher **Settings > Performance mode**. It takes effect at the next launch (Stop, then Launch VR).
  It was on by default only in v602 to v636. The launcher keeps the old setting, so an install from those builds can
  still have it on. Check the row.
- The game draws one eye per frame, alternating left and right, and the headset reprojects the other eye. This cuts both
  the GPU work and the game's own CPU work for each frame. In one headset test it saved about a third of the frame time
  and held 90 Hz where normal drawing ran at 45 to 60 fps.
- The cost: the two eyes show moments one frame apart. That can show as shimmer or doubling, mostly around moving hands and
  objects.
- With Off, SMAA or Game TAA selected, it runs as FSR 3 Native. FSR and DLSS work as selected.
- Menus, loading screens, takedowns and cutscenes are still drawn with both eyes.

### Monitor view

What the desktop window mirrors. **Off** is the fastest and Right eye is next; **Both eyes** costs the most. It never
changes what you see in the headset.

### Sharpening

About 0.1 ms. Set it by taste.

### Save diagnostic logs (Features)

Off by default. While it is on, the mod writes logs and captures images, which causes hitches of up to half a second.
Turn it on only to record a bug.

## Game settings

Your settings in the game's Options menu still apply in VR, apart from these:

| Game option | In VR |
|---|---|
| Screen Resolution | **Ignored.** The mod sizes the game to the headset; use the mod's Resolution instead |
| Windowed Mode | Must be Windowed or Borderless Window |
| V-Sync | No effect while the headset shows the game; the headset sets the pace |
| Field of Vision | No effect; the headset sets the field of view |
| Adaptive Resolution | **Switched off by the mod** (it shimmers between the eyes). Use the mod's Resolution or an upscaler |
| Anti-aliasing | **Replaced by the mod's Anti-aliasing** |
| Depth of Field, Lens Flare | Switched off by the mod; so are camera motion blur and the game's SSAO |

These are the ones worth lowering, in order:

| Game option | Why |
|---|---|
| **Shadow Quality** | Both eyes draw the shadows. In one measured heavy scene they were about half of the game's GPU work. In a lighter test scene, lower shadow settings saved nothing measurable, so the gain depends on where you are |
| Fog Quality, Volumetric Lighting, God Rays | Next in line; smaller savings |
| Rat Shadows, Bloodfly Shadows | Set to No |
| Nvidia HBAO+ | Leave at No: it adds ambient-occlusion work for both eyes (the mod already turns the game's SSAO off) |
| Texture, Model, Environment, Water, View Distance, Decal | Lower if you are still short on GPU; the gain varies by scene and has not been measured here |

## Your headset app

- **Virtual Desktop:** under Streaming, set the frame rate to 72 or 90 and Synchronous Spacewarp to **Disabled**. With SSW
  on, the game runs at half rate and walking looks steppy. VR Graphics Quality sets the base size that the mod's
  Resolution scales. High or Ultra with the mod at 1.00x is a good start.
- **Meta Quest Link / Air Link:** set the refresh rate and render resolution in the Meta Quest Link PC app's graphics
  preferences for your headset. The render resolution is the base size the mod's Resolution scales.
- **SteamVR / Steam Link:** Render Resolution is the base size; Motion Smoothing fills in missed frames.

Frame smoothing (SSW, ASW, Motion Smoothing) invents frames when the game misses its slot. It can show doubled or wobbly
edges in fast motion. Fix the frame rate instead of relying on it.

## Finding what limits you

1. Watch the frame rate in your headset app's performance overlay.
2. **Lower the mod's Resolution one step.** If the frame rate goes up, the GPU is the limit. Keep lowering it, use an
   upscaler, and lower Shadow Quality.
3. **If Resolution 1.00x with DLSS or FSR Quality changes nothing, the CPU is the limit.** Resolution and graphics settings
   cannot help there. The game's own CPU work for two eyes is a large part of the 90 Hz budget even on a fast CPU: v984
   measured about 10.5 ms of the 11.1 ms on a Ryzen 7 9800X3D, before later savings. Use 72 Hz, or turn on performance
   mode, which draws one view per frame.

## What to expect

- **Reference rig** (Radeon RX 9070 XT, Ryzen 7 9800X3D, Quest 3 over Virtual Desktop): holds 90 Hz with FSR 4 Quality and
  performance mode off (v1109).
- **RTX 5080-class GPUs** are faster than the reference GPU, so a steady 72 or 90 fps should be reachable with sensible
  settings. Start from Resolution 1.00x to 1.25x, DLSS Quality or DLAA, and Shadow Quality Low or Medium, with a CPU
  that keeps up. This is an estimate: no RTX 5080 has been measured yet.
- **Weaker GPUs:** use an upscaler (FSR Quality or Balanced, DLSS Quality) and lower Shadow Quality before turning on
  performance mode.

## Sources (for maintainers)

- Frame pacing and the half-rate lock: `src/frame_pacer.hpp`.
- Resolution scaling and SSAA above the runtime maximum: `src/render_size.hpp`, [VR-v976-RELEASE.md](VR-v976-RELEASE.md).
- Game options the mod overrides: `src/native_cvars.hpp`, [image-quality-research.md](image-quality-research.md).
- Shadows at about half of the game's GPU work at a heavy spot: [ENGINE-PERF-PLAN.md](ENGINE-PERF-PLAN.md), "v999 per-pass GPU".
- Engine CPU near the 90 Hz budget: [VR-v984-RELEASE.md](VR-v984-RELEASE.md).
- 90 Hz held on the reference rig: [VR-v1109-RELEASE.md](VR-v1109-RELEASE.md).
- Performance mode pacing in the headset: [superpowers/plans/2026-11-01-performance-mode-research.md](superpowers/plans/2026-11-01-performance-mode-research.md).
- Anti-aliasing modes and the SSAA presets: `launcher/Program.cs`, `src/vr_settings_native.hpp`, [VR-ANTIALIASING.md](VR-ANTIALIASING.md).

# Performance guide

Work down this list, one change at a time, until the game holds your headset's refresh rate.

1. **Headset app: 72 Hz or 90 Hz.** 72 Hz is the easiest to hold. 90 Hz is smoother but leaves less time for each
   frame.
2. **Mod, Resolution: 1.00x** (100% in game). This is the biggest lever: 1.50x is 2.25 times the pixels and 2.00x is
   4 times. Your headset app has its own resolution setting and the two multiply, so raise only one of them.
3. **Mod, Anti-aliasing.** FSR and DLSS cost some frame time of their own. The default (DLAA or FSR Native) is the
   sharpest and the slowest. There are two ways to go faster:
   - **Off or SMAA** removes that cost and can raise the frame rate. Edges shimmer more.
   - **DLSS Quality** (NVIDIA RTX) **or FSR Quality** keeps the upscaler but renders about half the pixels.
4. **Game, Shadow Quality: Low.** Shadows were about half of the game's GPU work in one heavy scene, so try this game
   option first. Then lower Fog Quality, Volumetric Lighting and God Rays, and set Rat Shadows and Bloodfly Shadows to No.
5. **Mod, Monitor view: Off.** The desktop mirror costs GPU time and does not change the headset image.
6. **Still short? Mod, Performance mode: On.** It draws one eye per frame, which saved about a third of the frame time
   in one headset test. Moving hands and objects can shimmer or double. In this mode Off and SMAA run as FSR 3, so they
   no longer save frame time.

The mod's settings are on the launcher's **Settings** page and in game under pause menu > **VR Settings**. Performance
mode is in the launcher only and applies after Stop, then Launch VR.

Keep **Save diagnostic logs** (Features) off unless you are recording a bug. It causes hitches.

## Why you see 36 or 45 fps

A late frame waits for the next refresh, so the headset shows half the refresh rate: 36 fps at 72 Hz, 45 fps at 90 Hz.
The game is usually only slightly too slow, not twice too slow, so lowering one setting can bring back the full 72 or
90 fps.

## If lowering Resolution changes nothing

The CPU is the limit, and graphics settings will not help. Drop from 90 Hz to 72 Hz, or turn on Performance mode.

## Game options that do nothing in VR

Screen Resolution, V-Sync, Field of Vision, Adaptive Resolution, Anti-aliasing, Depth of Field and Lens Flare. The mod
sets or replaces them. Use the mod's Resolution and Anti-aliasing instead.

## DLSS

DLSS needs your own `nvngx_dlss.dll` from another game. The launcher looks for one in your Steam games; if it finds
none, point it to one with **DLSS file**.

# Performance guide

Your headset shows a new image every refresh: 90 times a second at 90 Hz. A frame the game cannot finish in time waits for
the next refresh, so the frame rate drops to half: 45 fps at 90 Hz, 36 at 72 Hz. Spacewarp, on by default, fills that gap.
Below it are the settings that make each frame cheaper.

The mod's settings are on the launcher's **Settings** page and in game under pause menu > **VR Settings**. Performance mode is
in the launcher only and applies after Stop, then Launch VR.

## Quick list

Work down this list, one change at a time, until the motion looks smooth to you.

1. **Spacewarp: Auto** (the default) and the **headset at 90 Hz**. When the game falls short of 90 fps, it runs at 45 and the
   mod draws every frame in between, so the headset still gets 90 images a second.
2. **Resolution: 1.00x.** This is the biggest lever on the graphics card.
3. **Anti-aliasing: DLSS Quality** (NVIDIA RTX), **FSR 4 Quality** (AMD Radeon, where it can run) **or FSR 3 Quality**. These
   render fewer pixels and upscale them.
4. **Game, Shadow Quality: Low.** Shadows were about half of the game's GPU work in one heavy scene.
5. **Monitor view: Off.**
6. **Still not smooth with Spacewarp on?** If it keeps switching between full and half rate, set **Spacewarp: Always**. If the
   game cannot hold even 45 fps, lower the settings above further. Without Spacewarp, try **Performance mode** instead; the
   two cannot run together.

## What each setting does

### Spacewarp (Off / Auto / Always)

The game renders every second frame, and the mod draws the frame in between from the last real one. It uses that frame's depth,
your head and controller motion, your walking and turning, and your hands, weapons, held objects, body and legs. The HUD,
crosshair, prompts and markers are drawn again at the right place.

- **Auto** (default): full rate while the game keeps up; half rate with generated frames in between as soon as frames start to
  run late. Once at half rate it stays there at least five seconds, so it does not flicker between the two.
- **Always (half rate)**: generated frames all the time. Use it when Auto switches often, or when your processor cannot hold 90.
- **Off**: no generated frames. A late frame waits for the next refresh, and the headset's own reprojection, if it has one, fills
  the gap.

Things to know:

- **Set the headset to 90 Hz.** The game then needs 45 fps. At 120 Hz it would need 60, and 72 Hz leaves longer gaps to fill.
  The launcher tells you when your last session ran at another rate.
- **It needs an FSR or DLSS anti-aliasing mode.** That is the default on every GPU; while Spacewarp is on, the anti-aliasing list
  offers only those.
- **Only one frame generation at a time.** With Spacewarp on, switch off the others:
  - Virtual Desktop: Synchronous Spacewarp > Disabled (headset app > Settings > Streaming).
  - Meta Link: Asynchronous Spacewarp off (Oculus Debug Tool).
  - NVIDIA Smooth Motion and Lossless Scaling: off for Dishonored 2.
  - SteamVR's Motion Smoothing is switched off for Dishonored 2 by the launcher, but only while SteamVR is closed: close SteamVR
    before Launch VR. The launcher puts it back when you turn Spacewarp off.
- **What it cannot fix:** glass (bottles, drinking glasses) can look doubled while you move, because glass has no depth to warp.
- **Runtimes:** smoothest with Virtual Desktop's own runtime (VDXR). On SteamVR (since v1457) the frames in between go out on
  time as well. Meta Link works.

### Performance mode (Off / On)

The game draws one eye per frame, alternating, and the mod rebuilds the other eye from its last image.

- It saved about a third of the frame time in one headset test.
- Moving hands and objects can shimmer or show doubled in one eye.
- In this mode Off and SMAA anti-aliasing run as FSR 3, so they no longer save frame time.
- It does not run together with Spacewarp: turning one on turns the other off.

### Resolution (0.50x to 2.00x)

The render scale of each eye, per side. 1.50x is 2.25 times the pixels and 2.00x is 4 times; 0.75x is a little over half.
Your headset app (Virtual Desktop's quality preset, SteamVR's render resolution) has its own resolution setting, and the two
multiply, so raise only one of them.

### Anti-aliasing

Smooths jagged edges. The temporal modes (FSR, DLSS) also clean up shimmer over several frames, and their quality modes render
fewer pixels and upscale them. On first start the default follows your GPU: DLSS Quality on NVIDIA RTX, FSR 4 native on an AMD
Radeon that can run it (the mod checks), otherwise FSR 3 native.

| Mode | What it does | GPU cost | With Spacewarp |
|---|---|---|---|
| DLSS - quality (RTX) | NVIDIA's upscaler from about half the pixels | low | yes |
| DLAA (RTX) | DLSS at full resolution: the sharpest DLSS | high | yes |
| FSR 3 - balanced (67%) | AMD's upscaler from 67% of the width and height | low | yes |
| FSR 3 - quality (77%) | the same from 77% | medium | yes |
| FSR 3 - native | full resolution, no upscaling | high | yes |
| FSR 4 - quality / native | AMD's newer FSR, on Radeon cards that can run it | medium / high | yes |
| SMAA - edge anti-aliasing | edges only, no temporal pass: no ghosting, more shimmer | very low | no |
| Off | nothing: the fastest, edges shimmer | none | no |
| Game TAA | the game's own: moving things leave ghost trails in VR, not recommended | low | no |
| SSAA 125% | renders 25% more per side (about 1.6 times the pixels): very clean edges | very high | no |
| SSAA 200% + SMAA | renders twice per side (4 times the pixels): maximum clarity | extreme | no |

Choose SSAA in the launcher before you start: changing it mid-session rebuilds every VR surface, and Virtual Desktop can fault.

### Sharpening (Off / Low / Medium / High / Max)

Contrast-adaptive sharpening after upscaling. It costs little and helps most with the quality and balanced modes. Too high
makes edges grainy.

### Monitor view (Right eye / Left eye / Both eyes / Off)

What the desktop window shows while you play. Off is the fastest: the mirror costs GPU time and does not change the headset image.

### The headset's refresh rate

90 Hz is the best fit for Spacewarp. Without Spacewarp, 72 Hz is the easiest to hold, and 90 Hz is smoother but leaves less time
for each frame. Set it in your headset app:
- Virtual Desktop: Settings > Streaming > Frame rate.
- Meta Link: Meta Quest Link app > Devices > Graphics Preferences > Refresh rate.
- SteamVR: Settings > Video > Refresh rate.

### Streaming: Steam Link and Virtual Desktop

Streaming to a Quest adds its own limits on top of the game's frame rate.
- **Steam Link:** a Quest 2 can decode frames more slowly than 90 Hz allows, and Wi-Fi drops pause the stream. If it stutters,
  set Steam Link's Target Bandwidth to a fixed value (about 150 Mbit/s) instead of automatic.
- **Virtual Desktop** copes best. Its runtime (VDXR) is also the smoothest for Spacewarp.

### Save diagnostic logs (Features page)

Keep it off unless you are recording a bug. It costs frame time and causes hitches.

## Why you see 36 or 45 fps

A late frame waits for the next refresh, so the headset shows half the refresh rate: 36 fps at 72 Hz, 45 fps at 90 Hz. The game
is usually only slightly too slow, not twice too slow, so lowering one setting can bring back the full 72 or 90 fps. With
Spacewarp on, 45 real frames a second is the plan, not a problem: the frames in between are generated.

## If lowering Resolution changes nothing

The processor is the limit, and graphics settings will not help. Use Spacewarp (Auto, or Always if Auto switches often). Without
Spacewarp, drop from 90 Hz to 72 Hz or turn on Performance mode.

## Game options that do nothing in VR

Screen Resolution, V-Sync, Field of Vision, Adaptive Resolution, Anti-aliasing, Depth of Field and Lens Flare. The mod sets or
replaces them. Use the mod's Resolution and Anti-aliasing instead.

## DLSS

DLSS needs your own `nvngx_dlss.dll` from another game. The launcher looks for one in your Steam games; if it finds none, point it
to one with **DLSS file**.

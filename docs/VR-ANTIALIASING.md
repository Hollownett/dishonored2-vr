# VR anti-aliasing research and implementation (2026-11-03)

## What the public sources say

- **Multisample anti-aliasing (MSAA)** is the reference for VR clarity. Aalto University's head-mounted-display study
  (Oculus Rift CV1, deferred engine) found **4x MSAA and 2x MSAA clearly the best visual quality and the least
  fatigue**; FXAA was visually worse but not significantly more fatiguing; TAA was the blurriest and most fatiguing.
  MSAA cost 22-62% depending on sample count.
  <https://aaltodoc.aalto.fi/items/b8f4bb57-f644-4e61-8dc7-9ab5cc74dc82/full>
- **Meta recommends 4x MSAA** for Quest, and explicitly says it **requires the forward renderer**.
  <https://developers.meta.com/horizon/llmstxt/documentation/unreal/gpu-improved-algorithms.md>
- **Deferred renderers cannot use hardware MSAA.** The G-buffer/lighting pass has no per-sample geometry, so a
  multisampled G-buffer needs a different architecture; the paper's opening states the built-in MSAA cannot be used
  with deferred shading.
  <https://cescg.org/wp-content/uploads/2020/03/Fridvalszky-Optimizing-multisample-anti-aliasing-for-deferred-renderers-1.pdf>
- **Supersampling (1.2-1.5x render scale)** is the common PCVR clarity lever; 150-200% is practical on high-end GPUs.
  <https://vrcasts.com/guides/vr-steamvr-supersampling-guide/>
- **Half-Life: Alyx** is a forward-rendered Source 2 VR title with `vr_msaa` (2x/4x) and `vr_fxaa` as the cheaper
  alternative; that is the pattern "other VR games do".
  <https://www.pcgamingwiki.com/wiki/Half-Life:_Alyx>

## What this mod can and cannot do

Dishonored 2's Void engine is deferred (G-buffer + lighting; it exposes only `r_TAAAmount`, no MSAA cvar). Forcing
`SampleDesc.Count=4` on its render targets would break every deferred shader and the FSR/DLSS path. **True hardware MSAA
is not an available implementation in this engine.** The deferred-renderer equivalent is **supersampling**: render more
pixels per axis and let the XR compositor resolve them down. That is exactly what the mod's Resolution multiplier does.

## Implemented

### SMAA Ultra (v974)

Upstream SMAA 1x Ultra is one pass at threshold 0.05, tuned for 1080p. At 2688x2880 the same edge is thinner and 0.05
missed low-contrast geometry. v974 compiles the Ultra shader at **0.03**, one pass. A two-pass experiment changed 13.4%
of a real eye capture and visibly softened distant detail; one-pass 0.03 keeps the old ~2.8% footprint with the lower
threshold. SMAA is a separate mode; it is not combined with the SSAA preset.

### SSAA 125%/150% (v976)

The runtime's `maxImageRect` clamps the XR eye (Virtual Desktop High: 3120x3360 = 125%). v976 separates the XR eye from
the game render size: 150% renders 3744x4032 and `EyeResolve` box-filters it to 3120x3360 before submission. Above
125% the resolution slider now produces real supersampling; 200% is 4992x5376 on this headset, a 4x-pixel SSAA. The
launcher/AA stepper still offers SSAA 125% and 150% as labeled presets.

### Why not TAA/DLAA/FSR here

They are useful for performance and subpixel stability, but they are temporal: ghosting, stereo-history rejection and
softness. The user asked for a **clear** picture, so the spatial path is SMAA + SSAA; DLAA/FSR 4 remain separate modes.

## Recommendation for the clearest headset image

1. **SSAA 125%** first; **150%** now actually renders 3744x4032 and resolves to the XR eye, and **200%** is available
   through the Resolution slider (4992x5376, heavy).
2. Sharpening 25% (default) - SSAA does not sharpen.
3. If performance is tight: plain SMAA at native, or DLAA/FSR 4 Quality for temporal stability (accepting softness).
4. Hardware MSAA would need a forward renderer; Dishonored 2 has none.

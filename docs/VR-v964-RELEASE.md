# VR v964 (test candidate, 2026-11-03)

v963 plus the user request that SMAA be selectable, not only an edge toggle.

## SMAA in the launcher and in the game's VR settings

- The launcher's **Anti-aliasing** list now has **SMAA - edge anti-aliasing**.
- The game's **VR Settings > Anti-aliasing** stepper now has **SMAA (edge anti-aliasing)** as its own entry. The Edge
  anti-aliasing toggle remains for turning SMAA on/off inside a mode.
- The saved file format is unchanged. SMAA is stored as anti-aliasing **Off** with Edge anti-aliasing **on** (the same
  pair v962/v963 already used), so old files open on the SMAA row when they had edge AA on.
- Selecting **Off** in the list turns Edge AA off; selecting **SMAA** turns it on. Selecting a temporal mode (Game TAA,
  FSR, DLSS) leaves Edge AA as it was, but the mod skips SMAA while a temporal mode is active.
- SMAA is still the last spatial step (after sharpening), and a separate method: it is never stacked on TAA/FSR/DLSS.

## Builds

- DLL: `build/d2vr_render_probe_arm_v964.dll`, SHA-256
  `8501557ef73384c791ac1816176eddc03b118c2eb69e1a9f9e7e7895efae978c` (14,066,176 bytes), embedded in
  `DishonoredVRLauncher-v964.exe`.
- Installed in `%LOCALAPPDATA%\DishonoredVR\build` by the launcher (or by hand, with the old DLL removed: the proxy
  requires exactly one `d2vr_render_probe_arm_v*.dll`).

## Checks

- `vr_settings_check`, `vr_settings_native_check` (including the new SMAA entry applying as temporal 0 + Edge AA on, and
  Off turning Edge AA off) and `settings_file_check` (all AA modes plus SMAA round-trip) pass.
- The DLL builds; the launcher publishes with v964 embedded.
- No headset run: the launcher's AA row shows SMAA, but the in-game list and the actual edge smoothing still need the
  user's headset.

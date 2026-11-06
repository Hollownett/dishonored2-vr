# VR v1021 (2026-11-05) — VR stays in VR when the render resolution changes

The v1020 headset report: "when I change render resolution in the game VR settings then the game goes out of VR
and then goes back to VR. Game should always stay in VR."

## What the headset session showed

The v1020 session (`artifacts/xr-recoveries.log`, live at 18:52–18:57) restarted XR once, at the first gameplay
frame, exactly as the report describes:

```
18:53:14.391  empty-frame frame=2673 session_state=5 reason=no_eye_sources action=black_quad
18:53:14.578  empty-frame frame=2674 session_state=5 reason=no_eye_sources action=black_quad
18:53:14.765  SSAA resolve: texture read view (-2147024809)
18:53:15.845  empty-frame frame=1    session_state=2 ...          <- a brand-new XR session
18:53:15.864  Create game eye swapchain size=2496x2688 (replacing 2668x2930)
```

`frame=1` is a new `GameXr`: any exception out of `GameXr::present` goes to `recover_xr_session`, which destroys
the session, waits out the 1 s backoff and rebuilds it — the "out of VR and back" the headset sees. `-2147024809`
is `E_INVALIDARG` from `CreateShaderResourceView`.

The same happens on a resolution change made in the in-game VR settings: the request makes the game render above
the XR eye (the canted optical-density size, or a multiplier above the runtime's `maxImageRect` clamp), the next
frame goes through `EyeResolve`, and the resolve read view fails.

## Root cause

`EyeResolve` (internal SSAA, v976) reads the game's eye image directly:

```cpp
auto source_srv=srv(source,desc);   // CreateShaderResourceView(source, ...)
```

Neither source is shader-readable. The left eye is `paired_left`, the mod's copy target, allocated with
`BindFlags=0`; the right eye is the swapchain backbuffer, a render target. D3D11 refuses a read view on a resource
that was not created with `D3D11_BIND_SHADER_RESOURCE` and returns `E_INVALIDARG`. Verified on WARP:

```
bind=0x0 create=0x00000000 srv=0x80070057
bind=0x8 create=0x00000000 srv=0x00000000
```

`EyeSharpen` already copies an unreadable source into its own readable texture; `EyeResolve` was the one pass that
did not. The existing `eye_resolve_check` only ever resolved a source it had created *readable*, so the real
source was never exercised.

## What changed

- **`src/eye_resolve.hpp`**: `readable()` returns a direct read view when the source has the flag, otherwise it
  copies the source once per frame into a texture of its own format (so sRGB/linear sampling keeps its meaning) and
  reads that. The resolve no longer throws on the render path's real sources.
- **`src/dxgi_probe.cpp`**: `paired_left` is allocated with `D3D11_BIND_SHADER_RESOURCE` (it is still a
  `CopyResource` destination). The left eye now resolves without the extra copy, and `EyeSharpen` stops copying it
  every frame too.
- **`src/render_size.hpp`, `src/game_xr.hpp`**: `eye_source_holds()` accepts a source that is equal to or larger
  than the eye; a source still *smaller* than a just-resized eye is the frame or two before the game's own swapchain
  resize lands. That frame is now held by the existing empty-frame fallback (last projection / black quad) and
  counted (`eye_source_pending`) instead of throwing `Game eye source does not match the current XR swapchain` into
  `recover_xr_session`. A larger source still goes to the resolve.
- **`xr-live-status.json`**: new `eye_source_pending` and `eye_resolves` counters. `eye_resolves` rising is the
  SSAA/density path working; `eye_source_pending` should only tick on the frame or two after a size change.

## Checks run

- `build/compile-v1021.cmd`: MSVC 2022 `/W4`, `dll_exit=0` (warnings are the pre-existing C4456/C4267 set and the
  FidelityFX LNK4099 set).
- `eye_resolve_check` (extended, now registered in `tools/build-msvc.cmd`): resolves a readable 32x32 source, a
  source created without the read-view flag (the `paired_left` case), a render-target-only
  `R8G8B8A8_UNORM_SRGB` source (the game backbuffer's format), and a real WARP swapchain backbuffer (the right-eye
  case); the first pixel is 137,188,225 in all four, and the last three are the exact `E_INVALIDARG` the session
  log showed.
- `render_size_check` (extended): a shorter/narrower source is held, an SSAA-sized source is accepted, a foreign
  colour group is refused.
- `eye_sharpen_check`, `eye_antialiasing_check`, `native_eye_sources_check`: PASS unchanged.
- Not run: `tools/build-msvc.cmd` as a whole (it still stops on pre-existing `/WX` warnings), and no headset or game
  session of v1021.

## Build and launcher

- The isolated v1021 build of this fix (the worktree before the parallel session's v1022 edits landed):
  `build/d2vr_render_probe_arm_v1021.dll`, 14,903,296 bytes, SHA-256
  `3f400058096bae5ba9a6b459a576543a3c64fb404c6548e06d0787a15c151576`.
- Launcher: `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1021.exe`, 83,183,182 bytes, SHA-256
  `ab2eebdf7331bf2bd36a4bb7dfd52a6530c977c92e73a15706ef44c26416b6a1`, embedding that v1021 build. Its
  `--page play --screenshot --exit` run showed every setup check green and `Mod d2vr_render_probe_arm_v1021.dll
  (verified)`.
- A parallel session's v1022/v1023 builds landed in the same worktree minutes later and include this fix (its v1023
  candidate states "the v1021 stay-in-VR resolution fix"). Its v1023 is the newest candidate
  (`artifacts/repair-v1023-test-candidate.json`, SHA-256 `ebae8c22…`) and is what the parallel session installed in
  `%LOCALAPPDATA%\DishonoredVR\build`. `build/d2vr_render_probe_arm_v1021.dll` was rebuilt from that shared worktree
  afterwards (14,905,344 bytes, SHA-256 `b74f6b5c…`), so the v1021 candidate now records that build; either build
  carries the fix.

## Headset test

Launch with the launcher that embeds the build under test. The v1020 launcher (and the old installed v1020) would
reinstall v1020 over the fix. `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1021.exe` is the isolated build of this
fix; the parallel session's newest launcher (v1023+) also carries it.

1. Launch as usual. In `artifacts/xr-recoveries.log` there must be no new `frame=1 session_state=2` after the first
   session start, and no `SSAA resolve:` line.
2. Change Resolution in VR Settings (in the pause menu and at the main menu), up and down several steps,
   including 125% and 150%. Expect no black phase and no "out of VR": the image keeps coming, the new size sticks,
   and `xr-live-status.json` `resolution_multiplier_requested`/`game_render_size`/`xr_eye_size` change together.
3. `eye_source_pending` should tick only around the change; `eye_resolves` may rise every frame (the canted optical
   density renders ~7% above the XR eye and resolves down) — that is the intended path now, not a fault.

## Limits

- The fix is proven by the v1020 session log, the exact HRESULT reproduced on WARP, and the checks above; not by a
  headset session.
- On the right eye the resolve still needs one `CopyResource` of the backbuffer per resolve frame (the swapchain
  buffer cannot take a read view). Making the paired capture produce a readable right-eye copy would remove that
  copy; not done here.
- With canted views the game renders above the XR eye on every gameplay frame, so the resolve pass runs every
  frame; it is now correct but costs a full-screen pass and a little softness compared with an eye chain sized to
  the optical density (not changed here, to avoid resizing XR swapchains at gameplay start).

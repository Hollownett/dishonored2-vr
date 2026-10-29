# v685 test release: VR stays in VR, and the game's own Hypatia hint

This release fixes two headset defects reported after v684. They turned out to be the same
subsystem: the decorative eye-art overlay (the game's own prompt and tutorial art drawn into the
rendered eye).

## What the v684 session showed

`xr-recoveries.log` from the 18:11–18:16 session, three times within two minutes:

| local time | event | next line |
| --- | --- | --- |
| 18:12:45 | `Native eye artwork graphics operation failed` | `empty-frame frame=1 session_state=2` |
| 18:12:51 | `Native eye artwork graphics operation failed` | `empty-frame frame=1 session_state=2` |
| 18:14:07 | `Native eye artwork graphics operation failed` | `empty-frame frame=1 session_state=2` |

`frame=1` is a brand-new XR session. Each failure threw out of `GameXr::present` into the render
callback's handler, which treats any exception as a session fault: `recover_xr_session`, destroy the
session, restore flat stereo, wait out the backoff (1 s, 2 s, 4 s, …), rebuild. That is the
"VR injection breaks and falls back to flat mode" the headset saw, three times, with the pauses
growing.

The counter-serum hint was on the same path: the game's own `_tutorialMessage` is captured as native
art and drawn into the eye by the very draw that threw, and v678–v682 published the English fallback
card into the same slot, replacing it.

The session also ended in an access violation inside AMD's FSR4 teardown. `tools/symbolize_crash.py`
resolves it to `Fsr4EyeUpscaler::State::~State` → `ffxDestroyContext` → `amdxcffx64!…` → `amdxc64`
during `dllmain_crt_process_detach`, with the freed-heap pattern `0xfeeefeee` in `rcx`. That is a
crash on game exit, not during play.

## Why the draw threw

`NativeEyeArt::draw` created its views with the game's backbuffer format, which is
`R8G8B8A8_UNORM_SRGB`. The captured HUD art is a typed `R8G8B8A8_UNORM` texture, and D3D11 refuses
an sRGB view of a typed UNORM resource: `CreateShaderResourceView` returned `E_INVALIDARG`, and the
old `checked()` threw `Native eye artwork graphics operation failed`. The eye image itself is the
other half: the runtime hands the sRGB chain **typeless** `R8G8B8A8` storage
(`xr-transfer-*.json` `destination_format:27`), where a TYPELESS view descriptor is rejected and an
sRGB view composites nothing.

## What changed

- **A decorative draw can no longer end VR.** `NativeEyeArt::draw` reports the first failing D3D
  call with its HRESULT instead of throwing. `GameXr` records the failure, drops the art that
  produced it, and keeps presenting; the note overlay disables itself the same way. No eye-art fault
  reaches `recover_xr_session` any more.
- **Views follow the resource.** The art view falls back to the texture's own concrete format (that
  is the actual fix for the v684 throw). The eye view follows the eye texture: typeless storage gets
  the byte-preserving UNORM view, a typed eye keeps its format. Views are retried with the
  resource's own format, then plain UNORM, before a failure is declared.
- **The game's own hint wins.** While the game's native tutorial/information message is on screen it
  owns the tutorial slot; the English fallback card is published only when the map's own hint does
  not fire, as v678 claimed. No more replacing the original prompt with the mod's card.
- **FSR4 teardown is skipped during process detach.** The FSR4 state no longer calls
  `ffxDestroyContext`/`FreeLibrary` while the loader is shutting the process down (queried with
  `RtlDllShutdownInProgress`); the OS reclaims the context. Quitting no longer faults in the AMD
  driver.
- **Status:** `xr-live-status.json` gains
  `eye_art:{failures,rejects,note_failures,disabled,last_hr,eye_view,art_view,last_call}`.
  If `failures` is ever non-zero, those fields name the exact call and formats that failed.

## Verification

- `tests/native_eye_art_contract_check.cpp` (new, registered in `tools/build-msvc.cmd`): on a WARP
  device, typeless eye storage composites 1024 pixels through concrete UNORM/UNORM views; a refused
  draw returns `CreateShaderResourceView hr=0x80070057` instead of throwing; argument refusals are
  reported and never reach D3D.
- `eye_reticle_gpu_check`, `live_reticle_art_gpu_check`, `native_ui_layout_check` and
  `fsr4_eye_upscaler_check` pass unchanged.
- The DLL builds with `/W4` and the exact warning set v684 produced (no new warning).
- `game_xr_simulation_check` stops at `game_xr.hpp` C4706 under `/WX` and at its wrist-overlay
  assertion (line 109); both reproduce on the unmodified tree, so they are pre-existing.
  `tools/build.ps1` also stops earlier at `dlss_mode.hpp` C4702 (`/WX`) for the same reason, so the
  candidate was compiled with the project's own final link line.

## Test (headset)

1. Start as usual and play normally for a few minutes. In `artifacts/xr-recoveries.log` there must be
   no `Eye art draw failed` line and no new `frame=1` restart after the first session start.
2. In `artifacts/xr-live-status.json`, expect `"eye_art":{"failures":0,...}`. A non-zero value is
   still not fatal (VR keeps presenting) and its fields are the diagnosis.
3. With the filled counter-serum, approach Dr. Hypatia. The game's own tutorial message should appear
   (head-locked, the original localized hint). The English card appears only if the game's own hint
   is not on screen.
4. Quit the game and confirm it exits without a crash dialog.

## Limits

- Not headset-tested. The evidence is the symbolized crash record, the session log, and the fixtures
  above — not a headset run.
- If this save never fires the game's own cure tutorial, the fallback card is still the only hint
  (unchanged), but it no longer suppresses the original when the original is there.
- The containment keeps presentation alive; it does not make a failing draw succeed on a runtime
  where the eye chain cannot take a view at all. That case now degrades to no eye art instead of no
  VR, and is named in the status fields.

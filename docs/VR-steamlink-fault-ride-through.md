# Steam Link: VR keeps running through frame faults (branch fix/steamlink-xr-recovery)

User report (2026-11-18): "When I played it via Steam Link I had constant freezes from time to time and Steam Link
connection drops, but Virtual Desktop worked very well."

## Cause (code review of v1392)

- Every exception out of the Present hook ran `recover_xr_session` (`src/dxgi_probe.cpp`). It destroys the XR session,
  the XR instance and unloads the OpenXR loader, then builds a new `GameXr` after a backoff.
- On SteamVR that is the game leaving VR: SteamVR shows its own scene and Steam Link drops or resets the stream.
  VDXR (Virtual Desktop) survives the same teardown much better, which is why only Steam Link showed it.
- The backoff was 1, 2, 4, 8, then 10 s and never reset during a play session, so the freezes got longer the longer
  you played.
- Faults that turned into teardowns include: an `xrEndFrame` failure with any code other than four layer errors (a
  SteamVR hitch can return e.g. `XR_ERROR_RUNTIME_FAILURE`), a 1 s swapchain image wait that timed out, and any
  exception from feature code in the Present hook (v685, v1021, v1027 and v1039 each fixed one such case).

This is from reading the code. A Steam Link session's `xr-recoveries.log` has not been read yet.

## What changed

- `src/xr_fault_policy.hpp` (new, pure, `tests/xr_fault_policy_check.cpp`):
  - a fault that leaves the session usable is **ridden through**: only this frame is lost;
  - full recovery stays for a lost session/instance (`XR_ERROR_SESSION_LOST`, `INSTANCE_LOST`, `HANDLE_INVALID`,
    `SESSION_NOT_RUNNING`, `RUNTIME_UNAVAILABLE`, `GRAPHICS_DEVICE_INVALID`, `OUT_OF_MEMORY`), for the runtime asking the
    app to exit (`EXITING` / `LOSS_PENDING`, now `XrSessionFault`), for a failed `xrBeginSession` / `xrEndSession` /
    `xrBeginFrame` (also `XrSessionFault`: each leaves a state a dropped frame cannot repair), and for a **fault storm**:
    faults that keep coming (gaps under 0.5 s) for 3 s and at least 30 times, or faults with no clean Present in between
    for 3 s (a fault that blocks by itself, like a 1 s image wait timing out on every Present);
  - a fault while the session exists but is not running (idle / ready / stopping, as during a Steam Link reconnect) is
    ridden through too: there is no frame to end, and the next state change is handled as usual;
  - the recovery backoff starts over after 30 s of frames.
- `GameXr::abandon_frame` (`src/game_xr.hpp`): hands back swapchain images the faulted frame left acquired, then ends
  the open frame with the last good projection or a black quad (never zero layers, see `xr_frame_hardening.hpp`). The
  projection is replayed only when no eye image went back to the runtime since it was captured (a projection layer shows
  the chain's most recently released image, which would be the faulted frame's); otherwise that one frame is black.
- Swapchain image leases (`src/game_xr.hpp`): `xrAcquireSwapchainImage` / `xrWaitSwapchainImage` /
  `xrReleaseSwapchainImage` are now members with the same signatures that count acquired and waited images per chain,
  so an abandoned frame cannot leave a chain one image out of step. The wrist chains keep their own pending image and
  are not touched.
- `xrEndFrame` rejections: any non-fatal code drops just that frame; only rejections that last 3 s (and at least 30
  frames) recover. Before, only four layer errors were dropped and a run of 30 frames (0.33 s at 90 Hz) recovered.
- Knob `build/legacy-xr-recovery` restores the old behaviour (every fault recovers, old rejection rule, old backoff), for A/B.
- Knob `build/force-xr-frame-fault` (test): after 200 gameplay frames, one exception inside a frame while eye 0's image is
  leased. Expected: one black frame, VR keeps running, `xr_fault_policy.ridden` 1, `settled_images` 1, `xr_recoveries` 0.
- `tools/build-msvc.cmd`: `cutscene_skip_checkletterbox_gate_check` (two names glued together since v962, a check file
  that does not exist) split into `cutscene_skip_check letterbox_gate_check`.

## Status (`artifacts/dxgi-probe-result.json`)

The counters are in `dxgi-probe-result.json`, written when VR stops (Stop VR in the launcher; closing the game without
it writes no result file), not in `xr-live-status.json`. New object `xr_fault_policy`: `legacy`, `ridden` (faults ridden through), `storms`, `backoff_recoveries`,
`backoff_resets`, `abandoned_frames`, `settled_images`, `end_frame_rejections` (the last three count the current
`GameXr` session). `xr_recoveries` still counts full recoveries. `xr-recoveries.log` gets one line per ridden fault
(the first 40, then every 100th): `Frame fault ridden through, VR kept running (#N): <reason>`.

## Build and test on Windows

1. `tools\build.ps1` (or `tools\build-msvc.cmd`); `xr_fault_policy_check` runs with the other checks. Built with MSVC on
   Windows for v1393: `xr_fault_policy_check`, `xr_frame_hardening_check` and `game_xr_wrist_recovery_check` pass.
2. Quick check of the ride-through: create `build\force-xr-frame-fault` in the folder the DLL runs from (the mod home,
   or the repo when the launcher runs from `launcher\dist` in developer mode), play a minute, look for
   `Frame fault ridden through` in `xr-recoveries.log`, press Stop VR for the counters above; then delete the knob.
3. Steam Link session, 20-30 min of normal play, including the places where it froze before.
4. Afterwards, **before starting the game again** (the log is cleared at launch), keep
   `%LOCALAPPDATA%\DishonoredVR\artifacts\xr-recoveries.log` and `dxgi-probe-result.json`, plus
   `C:\Program Files (x86)\Steam\logs\vrserver.txt`, `vrcompositor.txt`, `driver_vrlink.txt`.
5. Pass: no freeze longer than a moment, no Steam Link drop; `xr_recoveries` 0 (or only after a real disconnect).
   `ridden` > 0 means the fix caught faults that used to tear VR down; the log says which ones.
6. Virtual Desktop: a short session to check nothing changed there.
7. If anything is worse: create `%LOCALAPPDATA%\DishonoredVR\build\legacy-xr-recovery` to get the old behaviour back.

Not changed: the 1 s swapchain image wait timeout (a timeout is now one dropped frame instead of a teardown, but the
wait itself still blocks up to 1 s), and a full recovery still recreates the XR instance (keeping the instance and
recreating only the session would be the next step if real session losses still drop Steam Link).

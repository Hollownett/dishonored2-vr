# VR v1028 (test candidate, 2026-11-05) — the eye-capture diagnostic no longer runs every frame

The v1027 headset report: **"FPS is very bad in v1027 in comparison with build v1020."** The cause is not the physics work
(v1024-v1026); it is one diagnostic. The native-eye transfer capture (`capture_xr_transfer`) could not succeed in the current
gameplay chain, and since v1027 stopped it from ending the XR session it instead ran — full readbacks, file writes and compare —
**on every frame, for the whole session**. This build makes a failed or impossible capture latch, and refuses an impossible pair
from the texture descriptions before it pays for any of it.

## The measurement (the session's own numbers)

`%LOCALAPPDATA%\DishonoredVR\artifacts\xr-live-status.json` after the v1027 session (pid 15108, installed
`d2vr_render_probe_arm_v1027.dll`; gameplay 19:51-19:55):

```
xr_cpu_spans_ms.eye_transfer_observer  [37.951801, 46.700802]   <- p50 37.95 ms, p95 46.70 ms, 7332 frames
perf.mod_cpu_ms_mean                   44.3765   (p95 71.6179, period 11.1111, missed_fraction 1)
frame_split.interval_ms_p50            50.0166   (p95 90.5893)  <- 20 FPS
frame_split.hook_ms_p50                39.5896                  <- the Present hook, i.e. this diagnostic
frame_split.eye0_ms_p50 / eye1_ms_p50   4.606 / 5.5736
```

Everything else in the frame is sub-millisecond: `eye_copy` 0.0045, `eye_aa_sharpen` 0.0134, `eye_release` 0.0023,
`eye_overlay_draws` 0.09 ms. 44.38 ms of mod CPU against an 11.11 ms period is the 20 FPS.

**The physics changes are not this.** In the same session `weapon_body` published 8770 times with `touches: 0`, `pushes: 0`; the
weapon-body lane and the v1024 grip ball add per-frame work measured in microseconds, and the frame's cost is 38 ms in the
transfer observer, not in any contact solve.

## The cause

`capture_xr_transfer_unsafe` captures one eye pair per pose epoch and only sets `mask|=1u<<eye` **after** the compare passes:

1. it reads back the game eye image and the XR eye copy (~31 MB each at 2668x2930), writes `xr-source-*.rgba` and
   `xr-destination-*.rgba` (31 MB each),
2. then, for a processed eye (`source != native_source` — the uncant resample and/or the SSAA resolve, i.e. the default
   gameplay chain), it reads back the native eye and compares sizes,
3. the native eye is 2496x2688 while the processed eye is the 2668x2930 XR eye (`game_render_size` vs `xr_eye_size` in the same
   status), so it throws `"Native and processed eye dimensions differ"` **after** step 1 and 2's cost,
4. the throw skipped the mask update, so the next frame's Present ran the same impossible capture again.

v1027 made that throw non-fatal (`noexcept` wrapper + skip + count) — correct, a diagnostic may not cost a session — but that
turned "one failed capture, then a session recovery" into "one failed capture per frame, forever".

The artifacts agree: in that session `xr-source-left.rgba` and `xr-destination-left.rgba` were rewritten at 19:55:17 (session
end) while `xr-transfer-left.json` and `xr-native-source-left.rgba` stayed at **18:53:20** (an earlier session, which did complete
a pair). Only the left eye's attempt ever ran: eye 1 returns early while `mask!=1`, and `mask` never advanced. The v1020 session of
18:51-18:57 is the one that wrote the 18:53:20 pair, which is why v1020 did not feel like this: its capture completed once and
stopped, or failed into a session recovery, instead of looping per frame.

## The fix (`src/dxgi_probe.cpp`)

- **The latch.** `latch_transfer_capture(gameplay, why)` sets `xr_transfer_refused[slot]` once, counts in
  `xr_transfer_capture_skipped` / `xr_transfer_capture_refused`, and writes one recoverable-fault line. The unsafe body returns
  immediately while the slot is latched; the menu and the gameplay captures latch separately.
- **The latch clears** on a new pose epoch (resume/recenter) and on an explicit `capture-eye-pair.request`, so
  `tools\request_vr_capture.py` still takes a pair on demand.
- **Refuse before paying.** The source, destination and native texture descriptions are compared before any readback or file
  write; a size mismatch is refused there, which is exactly the case that used to cost 38 ms to discover. The old throws stay for
  every other failure and are latched by the wrapper.
- **The status** gains `image_transfer_capture_refused` next to `image_transfer_capture_skipped`.

A capture that *can* succeed is unchanged: one pair per pose epoch, the same files and the same `xr-transfer-*.json`.

## Build and checks

- `build\d2vr_render_probe_arm_v1028.dll`, 14,921,728 bytes, SHA-256
  `931027ae6b6b9c8289a2186b7c322d25512aacc6cfae7d6f658bf42456aa65`, `build\compile-v1028.cmd`, MSVC 2022 x64 `/W4`
  `dll_exit=0`; the only warnings are the pre-existing C4456/C4459/C4244/C4267 and the FidelityFX LNK4099 set, none in the
  changed lines.
- `tests\eye_capture_routing_check.py` (extended): the noexcept wrapper latches through `latch_transfer_capture`; the latch is
  one-way inside an epoch; the size refusal precedes the first readback; the latch is consulted before the capture work, cleared
  per pose epoch and by an explicit request; both diagnostic throws remain; the observer assignment, the recovery catch and both
  status counters are present. PASS.
- `artifacts\repair-v1028-test-candidate.json` is the candidate the launcher picks.

## Not verified

- **No headset run of v1028.** The cost is established from the v1027 session's own spans, and the fix is established by
  compilation and the routing check; whether the frame now runs at the previous rate is for the headset to show.
- The parallel session's v1029 build of the same worktree (20:04) also carries this change, as will later builds.
- The diagnostic still cannot compare a processed eye against the native one at these two sizes; it now skips that case instead of
  paying for it. Making the compare meaningful (resampling the native image to the eye size) is not attempted here.

## What to check in the headset

1. `xr-live-status.json`: `xr_cpu_spans_ms.eye_transfer_observer` must fall from ~38 ms to about 0, `perf.mod_cpu_ms_mean` from
   44 ms to a few ms, and `frame_split.interval_ms_p50` from 50 ms to the previous value.
2. `image_transfer_capture_refused` should be 1 (or 2, menu and gameplay) per session, not thousands; no new
   `frame=1 session_state=2` in `xr-recoveries.log`.
3. The gameplay should feel like v1020 again: same motion, same weapon behaviour. Nothing in this change touches physics,
   locomotion or turning.
4. `tools\request_vr_capture.py` should still produce `xr-request-*-transfer-*.json` (the request re-arms the latch).

# Sword and hand flicker (user report 2026-10-20)

Report: the sword render sometimes breaks, and the hand with the sword frequently appears and disappears (missed hand frames).

## Evidence

- A real headset session (scratchpad `headset1/xr-live-status.json`, 8,578 tracked arm uploads per side) shows `native_arm_pose_fallbacks` = [277, 276], 3.2 % of uploads, almost equal on both sides. Frame spacing in the same session (`xr-frame-timing.csv`, gameplay frames) has only 6 gaps over 250 ms, so the 250 ms freshness window is not what trips: the fallbacks are single-upload events.
- Every Linux sim run (sim rig, no real HMD, 10-14 ms frames) shows exactly 1 fallback per side, at start-up, in paired and in alternate-eye mode, with and without 2.0x resolution. The new `sword_sweep` scenario (slow sweeps, then ~3.5 m/s slashes) adds nothing. The sim cannot reproduce the headset rate (its frame pacing and thread timing differ), so the numbers below are before/after parity, not proof.
- Two gates refused a tracked pose on a single sample, with no grace:
  1. `FullBodyDriver::arm_frozen` refused a track whose tick (or controller sample tick) was *newer* than the callback's clock (`callback_tick < track_.tick`). The callback reads `GetTickCount64()` before it takes the driver lock; the head-view thread can publish in between (the clock moves in 15.6 ms quanta). Newer is not stale. Both hands fell back for that frame.
  2. `NativeAttachmentPose::publish` accepted only a source equal to the current policy (`source.tick == policy_tick_ && revision == policy_revision_`). When the head-view thread advanced the policy by one sample between an arm solve and its publication, the publication was refused, and the palette already held for that side was erased first, so the item reader returned to the native arm (hand and sword jump to the animation's position) until the next upload.
- The counters (status `arm_flicker`) separate the causes in the next headset session: `why` (tracked, no_track, callback_before_track, track_aged, no_solution, stale_revision, publish_refused, other) per side, `gate_flips`, `fallback_runs_max_frames`, `attach_clears`, `weapon_hidden_frames` (control ticks where the engine hid a tracked arm), and `publish.{grace_accepts,rejects_policy,rejects_source,rejects_other}`.

## Cause

A tracked-palette gate flips to the native fallback on one late or early sample. The two strict comparisons above are the candidates; the headset counters decide which one dominates.

## Fix rule

- `arm_frozen`: a callback may precede the track's tick (and source tick) by up to `tracked_pose_future_skew_ms` = 100 ms. A track older than 250 ms still falls back, as does a callback more than 100 ms before the track.
- `NativeAttachmentPose::publish`: a source exactly at the policy is accepted as before. A source one or more samples behind (strictly older tick and revision), in the same pose epoch, reference epoch and mapping revision, and no more than `publish_grace_ms` = 100 ms old is accepted. An epoch, reference, or mapping change, a replayed (same tick, lower revision) sample, or a pose older than 100 ms falls back immediately.

## Not changed

Native ownership of the arm during a scripted animation (`native_hands`, takedown, finisher), the held-object release, and the attack path are untouched. If the headset still shows flicker with `gate_flips` near zero, the cause is outside these gates (engine arm hide, `weapon_hidden_frames`).

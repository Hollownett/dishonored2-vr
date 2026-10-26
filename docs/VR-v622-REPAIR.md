# v622 feedback repair candidate — 26 October 2026

Base: v620, `feat/bonelab-feel` at `17e047a674f9415161843de8dc4b410003b0881d`. Work is in `/home/hollownet/github/dishonored2-vr-bonelab`; the older main checkout is unchanged.

## Intended behavior

- Crosshair: project the same qualified gun/crossbow/power launch ray into each eye and composite a transparent ring directly into that eye image. The old flat HUD stage no longer clips placement. Each receipt matches the actual displayed frame, pose epoch and reference epoch, including the kept eye in performance mode. Offscreen aims disappear instead of clamping to an edge.
- Crosshair ownership: unrelated view callbacks clear aim intent only. The native binding survives until actual movie teardown, disable or expiry, and retains exact pointer/list/record validation. Composition requires a fresh qualified aim ray and a live native crosshair.
- Pickup: distinguish deliberate hand movement from shared player movement when assigning the native interaction query. A newly reaching hand can take the query from a resting targeted hand without waiting for the 600 ms fairness interval. Keep nearest eligible object bounds, the 20 cm acquisition distance, native permission and line-of-sight checks.
- Prompt: join native Add/Show artwork to the exact query invocation that emitted it, after that query completes. Rejected/empty queries cannot donate artwork to later targets. Events outside a qualified query use the existing VR panel so their native pixels remain visible without guessing a world target. Do not drop a new target's sole event behind the previous target's 100 ms throttle.
- Sprint: enable the existing player-root feedforward gate. Hand backswing must not remove compensation for player locomotion. Accepted walking/turning inputs remain unchanged.
- Rendering: performance mode continues to alternate eyes by default. The selected temporal AA is now enabled by default there, with the existing native per-eye ownership checks and spatial fallback. FSR Native keeps full rendering resolution; this change does not select a scaled FSR preset. FSR/DLSS support and provider checks are retained.
- Temporal warmup/recovery: a compatible declined upscaler dispatch copies the current eye colour to its output rather than running native TAA over possibly shared history. Invalidate older queued work and reset both eye histories before accumulation resumes; discard old motion and reset history on camera ownership, scene and XR discontinuities.

XR recovery now retires alternate-eye plans and retained state across sessions, even when the new session starts at the same pose epoch. A fresh session bootstraps with paired images. Recovery/cooldown frames do not count toward failed one-eye rendering, and a genuine fallback updates the mode status. Transfer diagnostics compare the base eye copy before any overlay and explicitly label that capture stage.

## Defaults and rollback

`run-root-gate` and `alternate-upscalers` change from default-off enable-file experiments to default-on disable-file switches (`no-run-root-gate`, `no-alternate-upscalers`). The launcher and Linux install migration preserve explicit user off choices. Old positive files may remain but no longer control these two paths. To compare the previous behavior, switch off the corresponding feature and restart the mod session.

Keep native ownership failures visible: temporal AA may fall back to spatial AA while the selected/saved AA stays unchanged. Both fresh-eye and kept-eye image/pose identities remain required. No new engine offset, hook ABI, OpenXR availability probe, locomotion input or turning policy is introduced.

The retired `prompt-target-anchor` experiment is removed from the feature list: exact captured target ownership is now required and cannot be disabled. Existing legacy files/state are retained harmlessly during upgrade.

## Evidence and limits

The sprint replay reproduces 15.1598 cm cyclic error and a feedforward drop from 6 to 1.140215 m/s with the prior hand-target gate. The player-root gate removes that cycle in the replay. Extended tests cover walking and sprinting forward/sideways, 90 Hz target publication against a 180 Hz servo, and identical stationary-root hand response.

The temporal GPU regression verifies actual current-eye colour output while leaving shared history unchanged. Existing native planner tests cover absent/stale right-eye twins. First unprepared FSR3 eyes and both-eye history resets are exercised by the FSR GPU wrapper. [AMD's integration guidance](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/) requires resetting temporal history on camera discontinuities.

v621 was rejected after simulation found zero direct-reticle draws and a false performance fallback during XR recovery. Its artifacts remain marked DO-NOT-SHIP. v622 contains the ownership-lifetime and recovery repairs. Build, automated check and simulation results are recorded with the candidate artifacts. Linux clang-cl/xwin/Wine checks do not replace the required native MSVC release gate. A genuine FSR4 provider is unavailable in the Wine test environment; a provider fallback is not proof of genuine FSR4 operation.

The v622 runtime loot run submitted 10,755 alternated frames plus two paired bootstrap frames, with no fallback. It recorded 19,327 direct-reticle draws without compositor failure. FSR Native stayed mode 2 at render fraction 1, dispatched 5,273 times per eye, with one safe startup copy and no FSR failures or stale jobs. The table coin was taken 81 ms after the grip press, with zero incorrect selected-target prompts across 940 logged prompt frames. The 8,192-row trace cap limits evidence for other objects.

Native HUD Add/Show events in this run occurred outside the selection callback. They exercised the native-pixel panel fallback. The zero world-prompt latency counter is not measured visual latency; removal of the event throttle is verified in code, while headset prompt onset remains unverified.

The additional run-hands scenario did not establish sustained sprint: only nine rows exceeded 4.5 m/s. Its checker reported an 8.13 cm right-hand residual swing, and console teleports left the right physics body over 60 m away. The original failed checker output is retained. This runtime result is inconclusive for the sprint fix and does not supersede the repeatable servo replay or constitute headset acceptance.

The dedicated floor-cot run recorded 6,341 trace rows: the coin was first-ranked in 99.3% of nearby frames but native LOS blocked 85.8%, selection reached only 2.4%, and neither of two presses took it. The test does not establish easy floor pickup. Native obstructions and clamped-hand reach remain authoritative.

The user's initial right-eye warp remains unverified in a headset. The warmup/shared-history failure path is repaired, but its relation to this reported symptom requires headset evidence. Gun/crossbow/power alignment, small-coin pickup intent, sustained sprint, prompt delay and comfort also require a real headset trial.

Follow-up DLSS/FSR alternating-eye execution and pixel validation on the laptop is recorded in [VR-v622-AA-VALIDATION.md](VR-v622-AA-VALIDATION.md). This adds runtime AA evidence without changing the v622 DLL or establishing headset visual acceptance.

## Headset trial

1. Start a fresh game process with the reviewed candidate using `-SkipPreflightWait`; preserve required OpenXR initialization and recovery.
2. Keep performance mode on and choose FSR Native. Check both eyes from the first gameplay frame through the first two minutes, after pause/resume, loading and AA switches.
3. Aim a gun, crossbow and power across the full visible field; swap equipment and holster/draw. Compare the reticle with actual impact/target direction.
4. Reach for table/floor coins with either hand while the other hand rests near another object; repeat while walking. Check prompt onset, target choice and successful native pickup, including blocked objects.
5. Sprint for at least ten seconds forward and sideways with moving hands, a sword and a carried object; stop and walk again. Confirm no lag/catch-up cycle and no regression to accepted walking/turning.

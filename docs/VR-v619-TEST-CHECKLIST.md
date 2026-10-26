# v619 accumulated repair test build

Headset status: not run. This build includes the settings, pickup, aim, sprint, prompt, temporal-AA and hand-depth work developed after v613. Experimental paths remain off by default; they are available in the launcher's Settings → Features and the Linux feature CLI. Performance mode remains on by default.

## Enabled changes

- Launcher startup/restart and settings routing repairs; requested AA survives renderer/provider fallback and is saved independently of the effective mode.
- Pickup acquisition is 20 cm from the hand to the actual object bounds; grip-only physical contact is 10 cm. The nearest eligible object wins without facing, object-type, hover-history or other-hand-hover preference. Held-body exclusion and native permission/LOS remain. NPC and door discovery keeps its existing range.
- v612 barrel-equip routing restored separately from the v613 gun tracking used by holster/draw. Weapon/item/model changes reset aim filters. Idle gadget sensors retain native ownership, as in v612.
- Existing holster/draw and sword flip/catch behavior is preserved.

## Experimental switches (off by default)

| Feature ID | Test | Evidence and limit |
| --- | --- | --- |
| `run-root-gate` | Sprint steadily for at least 10 seconds, moving the hands normally, then stop. Repeat with a sword, native held prop and physical grab. | The replay removes the cyclic error; sustained headset behavior remains unverified. |
| `prompt-target-anchor` | Approach an unaware guard with and without nearby loot targeted by the hand. Check kill/choke hint placement and a native takedown. | Guard offers/hints appeared in v612 and v613; the reported prompt degradation was not reproduced. |
| `alternate-upscalers` | Keep performance mode on and choose each supported AA mode. Check both eyes after startup, swaps, pause/resume and loading. | Real-game FSR3, DLSS Quality and DLAA evaluations reached both eyes. Native FSR4 is unavailable on this Wine/hardware setup; its fallback was exercised. Headset quality remains unverified. |
| `world-hand-depth` | Hold a paper stack, an opaque prop and a corpse limb. Move each across both hands, from both sides; check interaction prompt readability too. | Real-game load reset the inherited arm depth to 5; the correction keeps it at 0 and restores native state on Stop. Bottle captures do not establish resolution of the reported paper/corpse transparency. |

Toggle one experiment at a time and restart the mod session. Existing feature choices are preserved during upgrade. Restore defaults leaves these four experiments off. On Linux use `linux/d2vr-linux feature on <feature-id>` or `off <feature-id>`.

## Required headset checks

1. Exit the game, install v619, choose launcher AA/settings and launch. Compare the actual `build/vr-settings.cfg`, the in-game VR panel and `settings_source` in the status. Stop/relaunch and verify an in-game change survives. Windows launcher startup remains to be verified on Windows.
2. Compare pickup reach and competition with v612/v613 in the same location: a small prop beside a corpse, two nearby props, and a blocked object beside a reachable one. Verify that the nearer eligible object wins and that native hold/release still works.
3. Equip a gun and a power. Repeatedly swap and holster/redraw; compare the reticle with the drawn barrel and actual shot/power target. Available simulator saves had no equipped gun/power, so alignment remains unverified.
4. Run the four experimental tests above, preserving accepted locomotion and turning settings.
5. Recheck sword flip/catch and holster/draw, native corpse carry, mine placement and grenade throws.
6. Stop during gameplay and preserve the status/traces. If restoration is pending while paused, resume briefly; exit the game before replacing the DLL.

The old performance-mode right-eye warp remains unresolved. Do not report it, sustained sprint, stealth prompt quality, gun/power alignment, or paper/corpse visibility as headset-validated based on simulator checks.

## Validation record

The release assets include `v619-validation.json` with build/check results and asset hashes. Simulator A/B evidence covers close native pickups, native guard offers/hints, both-eye temporal processing and native depth-state restoration. Automated checks establish their specific contracts, not headset visual quality or comfort. The Linux full harness has environment-specific skips and an unavailable native FSR4/provider GPU check; the release notes record the actual outcome.

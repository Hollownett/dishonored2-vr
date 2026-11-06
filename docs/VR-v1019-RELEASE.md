# VR v1019 (2026-11-05) — corpse on the shoulder: the game's own body carry

A body put on a shoulder now goes onto the game's own shoulder carry
(`arkPlayerStateCarryCorpse`, player FSM state 45) instead of staying a physics prop held by the mod.
That is what places like **Sokolov out of the Jindosh mansion** need: the mission expects a body the
game itself is carrying, with the player's carry pose, the game's Drop and Throw, and the native prompt.

## The mechanic

- **Put it on.** Grip a dead or unconscious body (any bone), lift it to either shoulder and let go
  there. The mod keeps the body where your hand let go, for a moment, and presses the game's own Use on
  it; the game takes it and carries it from then on. Your hand is free.
- **On the shoulder.** The body is the game's (`CarryCorpse`): the game's animation, its scripting
  (mission objectives, guards noticing, the skiff), and the mod's existing carry behaviour (no head
  clipping, the free hand does not push guards, no attack while carrying).
- **Take it off.** Squeeze at the shoulder with either hand and carry that hand away: it is put down
  (the game's Drop). A throwing motion throws it (the game's Throw). Both were the v636 gestures and
  are unchanged; only the body they act on is now the game's carry.
- **If the game does not take it** (a body whose **Loot** action is still offered, or the engine's own
  refusal), the body stays on your physical shoulder, exactly as in v636/v672: it comes with you when
  you walk, turn and crouch, and the same squeeze takes it back into your hand. The status counts why.

## How the handoff works

`src/shoulder_carry.hpp` (pure policy) + the dock in `src/grip_decision.hpp` + the wiring in
`src/dxgi_probe.cpp`:

1. `ShoulderDock` anchors the body where the hand let go at the shoulder (threshold: the bone held for
   250 ms, the grasp point within 0.6 m of the hand). This is the v636 dock, now on by default.
2. The render thread publishes the placement (body, hand, tick). The input thread's
   `ShoulderCarryTrigger` holds the game's Use (`input.x`) while **all** of these hold:
   - the placing hand's live native Use target is that body (`native_interaction_aim.target_owner`),
   - the engine does not offer that body's **Loot** action (the engine offers Loot as a corpse's
     PRIMARY action and Carry as its secondary one, so a Use press would loot it instead of carrying
     it; loot it first with a plain grip and put it on again),
   - the game carries nothing yet (a Use press while carrying is the game's Drop),
   - inside a 1500 ms window from the placement (the body is never held in the air indefinitely;
     v613 measured ~0.55 s from the held Use to the engine's carry start, plus the lock and retries).
   At most three held-Use episodes, at least 250 ms apart.
3. While that held Use is on, the press claims the game's interaction query for the placing hand
   (`native_interaction_aim.lock`), so the Use acts on that hand's body and not on whatever the other
   hand points at. The press starts once the lock is 50 ms old; the hand's target reads empty for a
   round trip after the lock (the selection is invalidated), so a lost target keeps the hold for 200 ms
   before the hold ends and the retry gap follows.
4. When the game's `CarryCorpse` appears, the dock yields (`ShoulderDock::take`), the physical lane
   lets go, and the v637 auto-drop stands aside for this carry (`shoulder_native_carry`).

Everything else keeps the v637/v672 rules: a grip on a corpse never sends a carry Use, the prompt is
the game's, and a native carry the mod did not start (for example a choke that ends on the shoulder) is
still put down by the auto-drop.

## Knobs

| Knob (`<mod home>\build\...`) | Effect |
|---|---|
| *none* | on: the placement hands the body to the game's carry; the physical shoulder is the fallback |
| `no-shoulder-carry` | off: a body released at the shoulder falls, as in v672 |
| `no-shoulder-native` | the physical shoulder only (no native handoff) |
| `legacy-corpse-carry` | v612: bodies are carried by hand only, no shoulder zone, no native carry |

Status (`artifacts/xr-live-status.json`): `shoulder_carry {enabled, native, placed, presses, started,
loot, failed, takeoffs, offer}` — `offer` is the engine's own secondary action offset for the placing
hand on the body (`112` = `0x70` Carry, `152` = `0x98` Loot, `-1` none), which separates "the engine
never offered the carry" from "the press did not start it". The existing `sword_grip.carry_release` is
still `[native carry drops, throws, shoulder docks]` and `native_carry_auto_drops` counts the
auto-drops.

## Build and evidence (2026-11-05)

- `build/d2vr_render_probe_arm_v1019.dll`, 14,902,272 bytes, SHA-256
  `bc7bb92110fdc016c3c42a578e679c5d013ab28c2571199bae70a0e7435a849c`, compiled by
  `build/compile-v1019.cmd` (`dll_exit=0`). It includes another session's uncommitted v1018 weapon
  work, which is in the same worktree.
- `artifacts/repair-v1019-test-candidate.json` is the candidate the launcher build picks (newest
  unblocked, DLL present, sha256 matched by the build task).
- The launcher is `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1019.exe`, 83,182,566 bytes,
  SHA-256 `20f2830720f3e671e053d06ebf108ff98aabb7eae083823caa156f1cfdaed4a5e` (same bytes as
  `launcher/dist/DishonoredVRLauncher.exe`), published with `dotnet publish launcher -c Release`; its
  publish log printed `Embedding d2vr_render_probe_arm_v1019.dll from repair-v1019-test-candidate.json`.
  A `--check-package` run into an empty home installed `build\d2vr_render_probe_arm_v1019.dll` with
  SHA-256 `bc7bb921…` (verified against the embedded manifest). That headless check then throws after
  the install on this machine — identically for the v1018 launcher — so that failure is pre-existing
  and unrelated to this DLL.
- The launcher itself runs: a `--page play --screenshot artifacts/v1019-launcher-play.png --exit` run
  shows all setup checks green and `Mod  d2vr_render_probe_arm_v1019.dll (verified)`
  (`artifacts/v1019-launcher-play.png`). That run also installed v1019 into
  `%LOCALAPPDATA%\DishonoredVR` (its `build\d2vr_render_probe_arm_v1019.dll` matches the hash above and
  the older probe DLLs are gone), so `LAUNCH VR` uses it without another install.
- PASS: `shoulder_carry_check` (new: Loot blocks the press, held Use, retry gap, bounded window,
  started once, give-up, new placement), `grip_decision_check` (the dock's new take),
  `held_release_check`, `corpse_strength_routing_check`, and the new
  `shoulder_carry_routing_check` (the wiring: the press is the game's Use, the lock, the auto-drop
  stand-aside, the knobs, the status keys).
- `corpse_routing_check`, `physical_grip_routing_check` and `held_object_routing_check` fail in this
  worktree **before** this change too (verified against a clean `HEAD` worktree); they are not caused
  by v1019. `tools/build-msvc.cmd` still stops on pre-existing `/WX` warnings, so the per-version
  compile script was used.

## Not verified

- **The engine's acceptance of a body placed on the shoulder.** v632 recorded the engine refusing a
  body held up in the air, which is why v636 replaced the v613 handoff with the physical shoulder.
  This change presses the Use while the body is at the shoulder; whether the engine takes it there is
  exactly what the first headset session has to show (`started` vs `failed`).
- The headset feel of the carry pose (the body's position on the shoulder, the view blocked, whether
  the player's animated carry arms fight the tracked arms), the Drop/Throw gestures with the native
  carry, and Sokolov's mission script (the objective at the skiff, the guards).
- Nothing here was run in the game. No headset session, no save loaded, no mission.

The checklist is `docs/VR-v1019-TEST-CHECKLIST.md`. Do not treat the checks above as gameplay proof.

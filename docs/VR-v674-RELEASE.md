# v674 test release: loot a body with grip (Vasco's note)

Based on v672. This fixes the Vasco note that could be seen on his body but never picked up in
VR. It has **not** been tested in a headset yet.

## What was wrong

The note with Vasco's safe code is a special-loot pickup shown on his belt. The only thing the
game offers there is the dead body's own interaction, and the game offers two actions on it:
**Loot** as the primary action (`m_loot`, declaration `+0x98`) and **Carry** as its secondary
action (`m_carryCorpse`, `+0x70`). A Use press runs the primary action, so it would loot the
body and hand over the note.

Since v637 the mod has blocked every Use whose target is a dead or unconscious body, so that no
press can shoulder it ("bodies are carried by hand only"). That guard also blocked the body's
Loot action. A grip over Vasco therefore went to the physical lane and held a corpse bone; the
game never looted him. The v672 session that reproduced this recorded the `Vasco (dead)` native
prompt, both action offers eligible in the same cycles, and 758 blocked body Uses.

## What changed

- A dead or unconscious body whose native **Loot** action is live is now a loot press, not a
  carry: its Use reaches the game, and its native prompt is shown beside the body. The existing
  v637 hand-only carry rule still applies to every body that offers only Carry, so a corpse still
  cannot be shouldered.
- The grip-alone decision prefers that loot over a bone of the same body (the body's native
  target is classified as loot while the Loot action is offered).
- `body_loot_passes` in `xr-live-status.json` counts the frames where a lootable body's Use was
  allowed through (next to `body_use_blocked`). It is the evidence to check after the test.

The native Loot receipt uses the same exact ownership/identity validation as the Talk route; the
mod does not grant items, call engine functions or write interaction fields. Only the game's own
Use press is let through.

## Install

Exit Dishonored 2, run the v674 launcher, choose **LAUNCH VR**. v672 remains available for
rollback. This build also carries the palm-placed stun-mine/spring-razor change that was in v673.

Built files in this checkout:

- `launcher\dist\DishonoredVRLauncher.exe` (SHA-256 `37a60d8212d20e2711b9492013fd283197b26dd7c1631c735f806e6b0578f81a`)
- `build\d2vr_render_probe_arm_v674.dll` (SHA-256 `d1621167a7d4d7611ec9a8fdbb7c2f09ef3c3782cb3b280209f525e348da5cfa`)

## Test (headset)

1. Load the save near Vasco's body. Aim the palm at the note/body and squeeze grip. Expect: the
   body is looted and the safe-code note opens as the left-hand tablet; no body is put on your
   shoulder. Release and repeat once.
2. Look at the body: expect the game's own Loot prompt beside it (not a suppressed or flat-only
   prompt) while the Loot action is offered.
3. Loot an ordinary guard's body with grip: the same route (the body's Loot action) takes it.
4. Grip and drag a body by a limb: unchanged. A body with nothing left to loot must still never be
   shouldered by a grip.
5. Close the note with B; check the body, walking and turning are normal.

## Validation and limits

- Focused offline checks pass: `grip_decision_check`, `native_talk_offer_check` (the loot action
  offset), `gadget_aim_check`. The DLL built with MSVC `/W4` (the repository's full strict
  certification still stops on pre-existing warnings, as in v672/v673).
- The native selector semantics are documented in
  [native-corpse-loot-route.md](native-corpse-loot-route.md) with the live v672 evidence.
- **Not headset-tested:** the loot itself, the prompt appearance and the note tablet after this
  route. A body whose Loot eligibility the engine does not offer keeps the old hand-carry guard
  and is not looted; that case is not forced by the mod.

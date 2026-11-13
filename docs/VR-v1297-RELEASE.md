# VR v1297 (hotfix, 2026-11-13)

Hotfix on v1295. One change: a game crash is fixed.

## Fixed: crash a few seconds after some conversations or scripted scenes

**What you saw:** the game closed (or froze) about 10-15 seconds after talking to someone or after a scripted scene,
reproducibly at the same spot (seen in the Dust District). Crash reports name `Dishonored2.exe+0xcaf005`.

**Cause:** when the scene ends the game wakes up ragdolls and cut-off limbs again (switches their physics to
"dynamic"). For a ragdoll or limb whose physics bodies were already removed, the game's own code picks body 0 instead,
which is Havok's fixed world body. That body cannot move and has no motion data, so Havok reads a null pointer and
the game crashes.

**Fix:** the mod refuses that physics request: Havok's motion-type change is skipped for body 0 and for the invalid body
id, and the game's ragdoll wake-up is skipped for a ragdoll with no live body. Nothing that can actually move is
affected. The live status (`xr-live-status.json`) counts the skips under `bodyless_motion`.

Tested by the author in the game at the spot that crashed; everything else is unchanged from v1295.

# v658: physical rear strikes and native animation dispatch

This candidate implements the user's clarified contact rule after the rejected v656 run. It includes the existing v657 note repairs from the shared working tree.

- A physical sword contact on the rear or side of an NPC is lethal regardless of blade speed or the generic AI knowledge flag. A native-unaware NPC remains eligible at any contact location.
- The default producer has no contact speed threshold. Its old 35 cm/12 cm tip extension is now legacy-only, so a stationary sword short of the body cannot kill through that assist. The actual blade uses the existing 2 cm collision sweep; corpse-overlap replacement also uses the blade's actual length.
- Rear/side is measured from the actual impact point in the NPC's facing frame, with successful pose reads required. It does not use a displaced player origin, head direction or neck height.
- This intentionally makes rear contact lethal even on an alerted NPC. Frontal hits on alerted NPCs retain ordinary combat damage and its existing speed requirement. Repeated contacts from the same stroke retain the existing duplicate-hit protection.

## Native animation

A fresh victim-specific native sensor target plus an exact current ordinary GlobalAssassination offer can reserve the contact for the game's own attack command. The offer helper validates kind 2, ready/enabled, component identity and membership, state-machine owner, embedded target slot, handler identity and the full victim spawn through two read passes. It reads existing native state; it does not write AI awareness or fabricate an assassination packet.

The controller retains the exact owned contact through the attack's Block-release lead. Changed player, sword, victim, epoch, scene or native offer cancels that contact press. A verified offer bypasses the gesture cooldown while still requiring a free attack-press slot. A busy slot leaves the contact request queued for retry. No gesture-speed detector is needed to start this native contact press.

The native animation owns the target once it starts. Otherwise the reserved physical strike becomes lethal at 250 ms, through the same verified sword damage path. Ordinary body hits without an offered animation apply lethal damage on their own game-thread pass. Nonlethal reservation, active takedowns and owner/registry checks retain priority. The native offer is the animation trigger; there is no new hard-coded neck-height hitbox.

## Validation and limits

Focused MSVC `/W4 /WX` checks pass for the extracted real contact branch, no-speed admission, exact native offer and identity races, native contact ownership, ordinary sword routing, attack intent including confirmed-offer cooldown behavior, held-contact deadlines and existing combat policy. Cases include an aware/unavailable native result with a rear hit, a fake native offer, two different pending victims and animation reservation refusal.

The full strict build was attempted and still stops at the pre-existing `/WX` C4702 in `src/dlss_mode.hpp:163`. Candidate build and packaging evidence are under `artifacts/stealth-contact-20261028`. Source identity is recorded by the numbered DLL's `.source.json` archive.

No game session was launched or altered for this candidate. Local fixtures do not establish headset animation timing or gameplay acceptance. Required headset checks are a slow stab and a slash to the back/lower torso, a contact while the native animation trigger is offered, an already-alerted frontal hit, and the existing nonlethal choke. Test the same patrolling-guard approach that failed in v656.

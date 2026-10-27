# v638 test build: severed parts, body prompt, dragged body, takedown prompt size

Headset status: not run. Built on v637. User feedback on v637: very good overall; four items remain.

## Severed parts picked up reluctantly, no prompt (fixed)

A severed limb or body half is an `arkSeveredLimb`, a native carryable (HoldObject) like a bottle. Its model is skinned (vtable 0x1b0d628, measured live), not a static mesh (0x1b0dd88). `NativeInteractionAim::owner_bounds` read bounds only from static meshes, so the hand sphere never qualified a part. The part became a target only when the game's own gaze or hand ray picked it.

Now `owner_bounds` also accepts the skinned model of an `arkSeveredLimb` (RTTI name, vtable cached). Its box is the visible part.

Sim A/B, crouched with the hand on a severed head and the head level:

| | target rows | sphere accepted | prompt frames | picked up |
|---|---|---|---|---|
| v637 | 0 | 0 | 0 | no |
| v638 | 137 | 135 | 193 | yes (HoldObject, first squeeze) |

## "Carry body" prompt (hidden)

The prompt over a dead or unconscious body (`+0xd1` bits 0x08/0x20) is no longer published as world art (retained entry, live target and the second hand's indicator). Native prompts never draw flat, so it is gone. Grip on a body already never issued the game's carry Use (Ruling V1). The knob `build/legacy-corpse-carry` restores the prompt.

## Free hand lost on a dragged body (fixed)

The v637 session logged a dragged body lagging up to 1.25 m behind the hand. The holding hand's clamp yields (`clamp_yields`), but the free hand's clamp made planes on the body's legs and held the drawn hand there. Now the clamp ignores hits on any body either hand holds or docks (`NativeGrab::corpse_hold`).

## Takedown prompt size

`takedown_prompt_width`: clamp(distance x 0.08, 0.16, 0.6) m, before x 0.12 and 0.25..1. The HUD size multiplier (hud% / 65) is unchanged.

## Headset checks

1. Cut a body (sword on a fresh corpse, or a grenade). Reach for an arm, leg, head or half body without looking straight at it: the pickup prompt shows, and grip picks it up on the first squeeze.
2. Look at and touch a dead and an unconscious body: no "Carry body" prompt. Grabbing limbs and the torso still works.
3. Drag a body by one hand while walking, with the other hand low near its legs: the free hand stays with your controller.
4. Sneak up on a guard: the kill/choke prompt is smaller. Change HUD size (55% / 100%): the prompt changes with it.

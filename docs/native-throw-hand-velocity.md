# Grenades thrown with the real hand (Track T item A)

Behaviour: hold the left trigger (the game's own pin/cook), swing, release. The grenade leaves with
`1.5 x (peak hand velocity relative to the player over the 0.10 s before the release) + player velocity`, capped at 17 m/s
(the game's own throw speed), launched from the drawn left hand. No release in the last 1.5 s, no hand sample in the window,
a hand sample older than 250 ms, or a >12 m/s sample (teleport) means the native throw is left untouched. Mines and spring
razors are not thrown by hand (see mine aim, item B). Knob: `build/no-hand-grenade-throw`.

Ideas (gain, window, cap, call site) come from the external GPL-3.0 mod's documented facts; no code is shared.

## RVAs verified on our exe (sha256 b671f824...84516180, read with python from the Steam install)
| What | RVA | Verified |
|---|---|---|
| arkCpntWeaponThrowable::Throw | 0x632F70 | prologue = `native_fire_seam_trace.hpp` row 0 |
| call to the velocity setter | 0x63333C | bytes `48 8D 54 24 50 49 8B CE E8 5F 8F 24 00` = `lea rdx,[rsp+0x50]; mov rcx,r14; call 0x87C2A0` |
| velocity setter | 0x87C2A0 | `8B 02 89 81 3C 03 00 00 ...` = copies float3 `[rdx]` to `entity+0x33C`, leaf, returns void |
| launch point | `[rsp+0x70]` | filled by ComputeAimTarget 0x638AE0 (rdx), read at 0x633183 and passed as r8 to the launch 0x639970 at 0x6333FA |
| grenade check | GetComponent 0x877720, flags 0, typeinfo 0x39E28E0 | Throw itself does `lea rdx,[0x39E28E0]; xor r8d,r8d; mov rcx,r14; call 0x877720` at 0x633448 |

Disassembly of 0x632F70..0x633500 confirms [rsp+0x50] (velocity) is written at 0x633296/0x6332B7/0x6332C5 and scaled at 0x633310-0x63332A
before the call, so rewriting it inside the setter is the final velocity. [rsp+0x70] is the launch point, 0x20 bytes after the velocity.

## Implementation
- `src/throw_gesture.hpp` (pure, tested by `tests/throw_gesture_check.cpp`): sample ring, release-edge capture (hysteresis .55/.35), `take()`.
- `src/native_grenade_throw.hpp`: MinHook on 0x87C2A0 (8 prologue bytes checked first). The hook acts only when `_ReturnAddress()` is
  0x633341 (Throw's call site) and the entity has an arkCpntGrenade; every other caller passes straight through. Same effect as the
  external mod's call-site patch without hand-written thunks.
- `dxgi_probe.cpp`: `publish_hand` feeds the left hand's relative velocity (minus `player_velocity`) and the drawn grip position; the trigger is fed per frame;
  status JSON `grenade_throw` {enabled, calls, rewritten, not_grenade, no_gesture, faults, last_speed}.
- Hand velocity is the existing C9 controller window (60 ms) from `publish_hand`, the same source as the held-object throw.

## Not validated
Gain, window and cap are unvalidated on a headset (the source mod only tested them on a mock runtime). The game launches about 0.3 s
after the release, so the spawn point is the hand's position at launch (the grenade stays drawn in the hand until then).

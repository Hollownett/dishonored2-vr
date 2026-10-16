# Native Shadow Walk ownership and VR integration

Research date: 2026-10-15. Pinned Steam executable build 17576867, image base `0x140000000`. Addresses below are RVAs unless prefixed `0x14`. This is static evidence; no game control, build, injected call or headset acceptance was performed for this audit.

## Public primary references

Bethesda describes Shadow Walk as Emily approaching and incapacitating a target in [the official E3 feature](https://bethesda.net/en-US/news/dishonored-2-at-e3-2016-new-setting-new-hero-themed-missions). Bethesda Support explicitly confirms that its upgrade enables [passage through rat tunnels](https://help.bethesda.net/app/answers/detail/a_id/36755/~/why-does-the-tutorial-tell-me-to-possess-a-rat-and-find-a-small-passage-to-get). These establish product behavior, not memory layouts or ABI authority. The layouts below come from the pinned executable and shipped declarations.

## Exact native owners

- FSM `0x802de0` returns `*(*(FSM+0x90)+8)+0x60`, the current mode declaration's ID. Missing pool returns 14. Shadow player mode is 2 in the controller and Slash eligibility branches.
- `arkPlayerStateShadowWalk` vtable is `0x1c7ed28`; declaration vtable `0x1c7ebf8`. Both names are confirmed from MSVC RTTI in the pinned PE. Ground movement uses pool index 0 and sprint uses index 1. Controller `0x7fb4a0` checks mode 2 and active state 0/1 before selecting those exact slots. State `+8` is the index, `+0x10` is its owning FSM, `+0x18` is its declaration and `+0x20` is its active byte.
- The shipped `shadow_walk_sprint.playerstateshadowwalk` explicitly uses `PLAYER_STATE_SPRINTING`; both ground and sprint declarations specify autocrouch posture and native rat-hole scenes. Their movement speeds remain native: ground 2.5 forward, sprint 3.2 forward.
- Shadow state activation `0x8492f0` calls `0x7f43b0(camera,7,...)` and `0x7f4240(camera,decl+0x150,decl+0x154)`. Exit `0x8498a0` calls `0x7f2930(camera)`.
- Camera enum initializer `0x19f4d60` binds string `MODE_CAMERA_SHADOW_WALK` at `0x141e36828` to integer **7**. Native current/target mode fields are camera `+0x25200/+0x25204`.
- Camera update `0x7f6140` selects `0x7f1400` for mode 7. This routine reads controller look from `camera+0x25158` then `controller+0xd0`, consults native physics `camera+0x25160`, and updates pitch `+0x25264` and camera angular velocity `+0x252ac/+0x252b0`. Shadow Walk is therefore a player-controlled camera mode, although camera-position generation still uses its native mode branch.

## Rat-hole ownership must remain native

**Controller `+0x172` is `m_isInRatHole`.** This is reflected data, not an inferred visibility toggle: string `0x141d9b280` is registered at instruction `0x14159e810`; offset `0x172` is stored at `0x14159e826`.

`0x7fb4a0` toggles that byte. For active ShadowWalk slot 0/1 it calls `0x849bf0` on entry and `0x849d40` on exit. Entry plays declaration `+0x180` and selects posture `decl+0x178`; exit plays `decl+0x188`, restores arm visibility and restores posture from `decl+0x70`. Reflection of `m_enterRatHoleMidnight` at `0x141789901` confirms declaration offset `0x180`. Both native state declarations name `power_shadow_walk_rathole_in.midnightscene` and `power_shadow_walk_rathole_out.midnightscene`.

The state midnight-end callback `0x84a410` hides both arm entities while controller `+0x172` is true. Its registration/unregistration uses state vtable slots `+0x20/+0x28` (`0x8495d0/0x8499f0`), event `0xdc`.

Do not infer rat-hole ownership from state `+0xa0`: update `0x849ed0` uses that float as a wall-alignment dwell timer. The update can request state 20 after this threshold. That is separate from the controller's rat-hole marker.

## Native attack already supports an empty shadow hand

Normal controller mappings send ACTION_MELEE/RT to Slash 39. Crucially, both Slash eligibility functions special-case player mode 2 before checking a sword:

- `0x86c3e0`: call `0x802de0`, compare EAX with 2 at `0x86c3fc`, then jump directly to generic state eligibility `0x855230` at `0x86c40b`.
- `0x86c470`: the same compare at `0x86c489` and generic jump at `0x86c49b`.

Only the non-shadow path checks the right-hand weapon component's `+0x11c == 1`. The older `docs/native-attack-gating.md` eligibility table omits this mode-2 exception. Swing-generated RT can therefore use the existing native shadow Slash without spoofing sword equipment or calling a new engine ABI. Native damage, attack limits, assassination eligibility and upgrades remain authoritative. This proves the entry path, not visual correctness or attack success in a running headset session.

## Smallest safe integration

Qualify ordinary Shadow Walk through one stable, typed read: current primary player; started owned camera/controller/FSM; current pool; mode declaration ID 2; exactly owned active ShadowWalk state in pool 0 or 1 with the RTTI-confirmed state/declaration vtables and matching IDs; controller rat-hole byte exactly zero; current and target camera modes both 7; no code lock or body-yaw blend. Re-read identity, pool, declaration, state and rat-hole values before accepting the snapshot. A stale or mixed snapshot falls back to native authored ownership.

The implementation captures `rig::started` identities for the FSM and controller. The FSM must occur exactly once in the current player's component list; it rechecks the list data/count/capacity and the same membership slot. It rechecks both started identities after the state, pool, mode and script receipts. After qualification, the camera reader rechecks the primary game/player, camera generation, mode pair, camera-to-controller/body/physics links, physics identity and heading delegate. Negative fixtures mutate the FSM generation, pool, membership slot/list, camera controller/mode/generation and primary player during the active-state read.

Use that narrow qualification in the central camera classification so render head tracking, controller aim and locomotion consumers share one answer. `NativeTurning::ready` must consume the same classification while retaining its look/native velocity and impulse gates. Preserve normal mode-0 behavior.

Allow that qualified Shadow Walk to drive the existing motion-swing detector without its sword-equipped/grip requirement. Emit the existing bounded RT edge; do not route a shadow hand through sword contact damage or synthesize sword blocking. Clear motion/pending attack state on mode exit, rat-hole entry, authored finisher ownership, lost focus or identity change. Active scripted takedown/finisher states still own presentation. Keep native camera height/posture and accepted locomotion/turning configuration.

The integration's `f.sword_equipped` is an existing free-hand admission flag: its assignment is `!(physical_grab_requested && native_grab.holding(1))`. It does not inspect a sword or require a sword grip. `cf.shadow_walk` uses this flag and the existing swing guard; the separate `cf.equipped` path still checks actual sword/carry ownership for ordinary gameplay.

## Evidence files

- `artifacts/claude/attackgate/out/shadow-audit-readonly.txt`: vtable, RTTI-adjacent state functions, scene callbacks and rat-hole helpers.
- `artifacts/claude/attackgate/out/shadow-audit-control.txt`: activation, exit, update, sprint eligibility and camera enum registration.
- `artifacts/claude/attackgate/out/shadow-audit-raw-evidence.txt`: pinned PE disassembly for mode getter, both Slash mode-2 bypasses, RTTI names and rat-hole/declaration reflected offsets.
- `artifacts/claude/attackgate/out/b3.txt`: `0x7fb4a0` mode/slot selection and rat-hole toggle.
- `artifacts/re-toolchain/direct-ghidra/demo-native-aim-build-blocks/function-1407f6140.txt`: camera steering branch and native mode update.
- `artifacts/claude/attackgate/decls/allstates/playerStateShadowWalk__components__characters__player__base__states__shadow_walk*.playerstateshadowwalk`: shipped ground/sprint declarations.

Required acceptance remains real headset operation: enter and leave the power, move/turn/look with tracked head and controllers, swing once per attack, enter/exit a rat tunnel, finish an assassination, return to ordinary gameplay, and reload. Static evidence and offline checks cannot establish those outcomes.

# DOTO valve (crank) release and body carry: review

Read-only, 2026-11-22. No src/, offsets.json, build or game touched. Scratch: `scratchpad/valve/` (`ids.py`, `cc.py`, `hh.py`, `d2.py`).
Method: pefile + capstone on both exes; `tools/doto/pe_index.rtti` for vtables; every `arkDeclPlayerState*` decl ctor
(`lea rax,[rip+decl vtable]` ... `mov dword [rbx+0x60], id`) in both exes.

## A. Valve stops turning after 1-2 s

### D2 fix (what it depends on)
`git log`: v691 made a grip's Use a 200 ms press; the crank hold was added later, "A crank wheel (valve) the press started keeps Use held
with the grip until the game leaves the crank state" (`src/dxgi_probe.cpp:17468-17485`):

    cranking = interaction.interact && now-grip_use_since>=200 && d2vr::native_cranking(reader,image_base,machine)
    if((interact||door_hold) && ... && (legacy_corpse_carry || now-grip_use_since<200 || cranking || door_hold)) input.x=true;

`native_cranking` (`src/native_takedown.hpp:128`) = `native_player_state(read,base,machine,85,0x1c3c9c8)` = pool slot 85 of the player
state machine, vtable `arkPlayerStateCrank`, active byte +0x20. The wheel only turns while Use (`m_crankAction`) stays down.
So the whole fix hinges on one player-state **id literal (85)**.

### Cause in DOTO (high confidence, proven)
The generator translated the vtable (`0x1c3c9c8 -> 0x1cbe908`, port-report `src/native_takedown.hpp:129`) but **not the id**.
DOTO inserted a state at 74: decl ctor ids (D2 -> DOTO), from the decl ctors `mov [rbx+0x60], id`:

    arkDeclPlayerStateCrank   D2 0x6880ca mov [rbx+0x60],0x55 (85)   DOTO 0x6e1fea mov [rbx+0x60],0x56 (86)

Every state id >= 74 is +1 in DOTO (new `ChangeWeaponState`=74): Cancel 74->75, Interact 75->76, Spyglass 76->77, SpyglassZoom 77->78,
Whisper 78->79, Undraw 81->82, Powerwheel 82->83, KillCam 83->84, **Crank 85->86**, StoneShroud 86->87, EatRat 87->88, CancelClimb
88->90, CancelGrapple 89->91, InAir 91->104. 0..73, 49/50/45/53/64/65 are unchanged (checked). In DOTO pool slot 85 is a different
state (its `[s+8]!=85` or vtable check fails against the Crank vtable), so `native_player_state` returns nullopt -> `cranking` is never
true -> the grip's Use is a 200 ms press -> the wheel gets 0.2 s of turn and then runs back / stalls. (Status counter to confirm:
`crank_hold_frames` stays 0 in the headset session.)

### Fix (rank 1, one rule)
`tools/doto/offsets.json` "text" (generated line is `return native_player_state(read,base,machine,85,0x1cbe908).value_or(false);`):

    {"file":"src/native_takedown.hpp","contains":"native_player_state(read,base,machine,85,","old":"machine,85,","new":"machine,86,",
     "why":"arkPlayerStateCrank id: decl ctor DOTO 0x6e1fea mov [rbx+0x60],0x56 (D2 0x6880ca 0x55); DOTO inserted ChangeWeaponState at 74, every id >=74 is +1","source":"review/valve-carry"}

(If the rule engine matches the D2 source line instead of the generated one, use `contains` "machine,85,0x1c3c9c8".)

### Same shift, other D2 ids that did not port (rank 2, same root cause)
| where | D2 | DOTO | effect |
|---|---|---|---|
| `src/spyglass_lens.hpp:25` `state_spyglass=76,state_zoom=77` (read via `native_player_state(...,0)`, dxgi_probe.cpp:9800) | 76/77 | **77/78** | spyglass lens/zoom never detected |
| `src/native_action_camera.hpp:17` `{83,vtable,false}` KillCam | 83 | **84** | kill cam not recognised (`[s+8]!=id`) |
| `src/full_body_driver.hpp:92-94` `state_name()` cases 83 KillCam, 84 GrabGrappledEntity, 85 Crank, 86 StoneShroud, 87 EatRat; `kStates=96` (line 98) | | +1 for all (84 GrabGrappled -> 85 presumably, not in the decl census) | tracked-arm ownership during crank / kill cam / shroud off by one; states >=96 (Swapper 93 fits, Mimicking 96, UseMimic 102, InAir 104) never read |
Rules: text rules `state_spyglass=76,state_zoom=77 -> =77,...=78`; `{83,0x` -> `{84,0x` in native_action_camera.hpp; in full_body_driver.hpp renumber
cases 83..87 to 84..88 and `kStates=96` -> `112` (`full_body_states` in dxgi_probe.cpp:4293 sizes from kStates). Also check
`full_body.owned(2/3)`-style users of Crank in `body_ik.hpp:118` (name based, fine).

### Other possible contributors (lower, not needed once the id is fixed)
* `grip_use_since`/`cranking` need `interaction.interact` (grip alone with a native target). If the hand leaves the valve `use_hand`'s routed target
  no longer matters: once `cranking` is true the hold is independent of the target. Only the id is wrong.
* `final_talk_targets` etc. unchanged (id independent).

### Verify
* Headset-free: there is no crank scenario in `tools/linux/scenarios`. Cheapest check is a read-only live read: pool slot 86 of the machine
  (`*(machine+0x98)+0x10+86*8`) has vtable `base+0x1cbe908`, `[s+8]==86`, and byte `+0x20` becomes 1 while the wheel is turned (slot 85 does not).
  Then status `crank_hold_frames` must climb while the grip is held on a valve (headset). A new scenario would hold a grip on a valve for
  >3 s and assert `crank_hold_frames > 150`.

## B. Body carry: body not on the hand, hands invisible

I could not prove a single root cause statically; what is proven and what is ranked:

### Proven DOTO differences that touch the carry
1. **CarryCorpse stage offset (high, but only the left-hand redraw uses it).** `src/dxgi_probe.cpp:15171`
   `stage_now=read_self(state->carry.states[1]+0x110,...)` (and the comment in `src/shoulder_carry.hpp:109-112`). D2 copy fn
   `0x829c60` reads `[r9+0x110]` (stage, copied into `[rdx+0x10]`), the DOTO twin `0x891f80` reads `[r9+0x120]` (and the neighbouring flag byte
   `+0xd0 -> +0xe0`, `+0xf8 -> +0x108`: CarryCorpse state fields are +0x10 from ~+0xd0 up; `+0x130` is unchanged). Left unported, `stage_now`
   is some other field, `carry_redraw.update(...settled...)` never fires: the left item is not redrawn after a carry. Rule:
   `{"file":"src/dxgi_probe.cpp","contains":"state->carry.states[1]+0x110","map":{"0x110":"0x120"},"why":"CarryCorpse stage: D2 0x829cb7 [r9+0x110], DOTO 0x891fe7 [r9+0x120]","source":"review/valve-carry"}`.
2. HoldObject state fields are the same in both (`+0x68` spawn, `+0x70/0x71` flags, checked in vtable slots 6/12/13/21). The carry snapshot
   (`native_carry_snapshot.hpp`) is correctly translated (vtables 0x1d00228 / 0x1cfcef8 / decls 0x1d000e8 / 0x1cfcdb8 verified by RTTI,
   hold decl id 0x31 = 49, machine pool +0x98, body +0x358, components +0x270).

### Ranked hypotheses for "no hands" / "body not on the hand" (need a live read-only look, not provable from the exe)
1. (med) Engine hides the right arm during CarryCorpse; the mod shows it via `NativeHeldObject::hidden_arms/keep_arms/show_arms` gated by
   `wheel_tracks_arms(...)` and `state` from `native_hand_state()` (`dxgi_probe.cpp:15100-15200`, `native_held_object.hpp:150,345`). All its
   offsets were translated (`+0xd8` hidden flag, `arm_show_rva 0x7d8420`). If `native_hand_state()` returns {} in a carry, none of it runs. It
   returns {} on any failed check; the unported-by-name ones to read live are `body+0x60` (inventory ptr), `*inventory+0x68` (slots),
   `descriptor+0x138`, `part+0x18/+0x21`, `graph+0x190/0x1d0`, `arm.animation+0x38` (`native_hand_occupancy.hpp:25-100`, identical in the generated
   tree). `hand_bits.carrying` in status stays 0 while the game carries -> this is the failing link. Check status `hand_bits`, `arm_hidden`,
   `carry_left_state` rows in `dual-wield-events.jsonl` during a carry.
2. (med) The physical body-on-hand path (`NativeGrab`/`hand_body*`, grip drags, trigger hold = native carry, v1392) depends on G1/G2/G3 (feature-parity.md
   section 0) plus the physics record layout `world+0x28/0x30/0x120`, record `+0x44/+0x70/+0x90`, component `+0xc8/+0xd2/+0x100`, model `+0x1840`
   (`native_grab.hpp:781-819`, `native_hand_contact.hpp:651-671`, `hand_body_native.hpp:154-206`), all identical to D2 in the generated tree and
   **unverified** for DOTO; `component+0xc8/+0xd2/+0x100` are derived-component fields (DOTO shifts derived classes by 8/0x10), `model+0x1840`
   is a model field (DOTO render_scale_code showed `+0xc8 -> +0xd0` on the entity). A wrong `component+0x100` ragdoll handle list or `+0xc8` "native parent"
   makes every body "carried by a parent" and refuses the hand grab (`refresh_body` returns false at `if(parent || carry[10]) return false;`).
   Verify by fn_diff of the physics-component ctor and a live `fields` read of a body's component (`tools/entities.py`, read only).
3. (low) `body+0x300` anchors (`dxgi_probe.cpp:8421`) and `entity+0xd1` flag fixed by rules already (G3), not the cause.

### Concrete next step
Apply rule A1 and B1 (and the A2 table), rebuild, then in a headset carry read `xr-live-status.json`: `hand_bits.carrying` (must be 2 for a body on the
right shoulder), `shoulder_carry.started`, `carry.holds`, `arm_hidden`, `corpse.holds/binds`. If `hand_bits.carrying==0` while the game visibly
carries, hypothesis 1 (native_hand_state failing) is it; if it is 2 but the hand is invisible, the arm-show hook; if grip on a body never binds
(`corpse.binds` 0, `physical_grab_picked` 0), hypothesis 2.
Headset-free sim: `tools/linux/scenarios/corpse_lift.py` / `corpse_drag.py` / `corpse_collar.py` with `tools/doto/doto_sim.py`; they need a body in reach
on the DOTO save and G1/G2 in the build; status counters above decide.

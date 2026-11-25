# R7: DOTO Hook Mine lethal / non-lethal switch (static RE)

DOTO `Dishonored_DO.exe`, RVAs (image base 0x140000000). Static only; nothing launched, no repo edits.
PROVEN means read in the disassembly or in the shipped decl text. UNVERIFIED means it needs a live read.
Scratch tools in this folder: `fsref.py` (fast string to code ref), `reflseq.py` (reflection field/offset dump),
`dref.py`, `qref.py`, `idx.py` (DOTO `.index`/`.resources` lister and zlib extractor). Extracted decls are in `decls/`.

## TL;DR
* **The state id is wrong in R4.** `PLAYER_STATE_CHANGE_WEAPON_STATE` = **0x4a = 74**, not 73. 73 is `PLAYER_STATE_LOCK_HEART_TARGET`.
  The enum records are laid out `[name][value]`, so each `mov` that follows a name `lea` writes the value of the
  **previous** name. Checked against `TARGET_WALL=0` (0x1b11f14) and against D2, where LOCK_HEART=0x49 and CANCEL=0x4a, while DOTO has LOCK_HEART=0x49,
  CHANGE_WEAPON_STATE=0x4a and CANCEL=0x4b. That is exactly the "ids >= 74 are +1 vs D2" shift. The state decl constructor also writes it:
  `mov dword [decl+0x60], 0x4a` (m_id) at 0x6d9e96, 0x6d9f1e and 0x6da185.
* **`+0x778` and `+0x790` are not lethality.** They are the component manager's per-class pools (see Q1). The 276 that was measured is a pool field.
* The toggle is **hold `ACTION_POWER_WEAPON` (gamepad LT; keyboard is the same action) for at least 0.3 s** with the Hook Mine in the left slot and at least one mine loaded.
  A press shorter than that is a throw on release. The measured run (1.6 s hold, no throw) fits **a toggle that worked**: the button is
  consumed, so releasing it does not throw. Two holds toggle twice and end back at the start state. The run watched the wrong state (73) and the wrong field.
* The lethality lives in the weapon entity's `arkCpntWeaponStates.m_currentStateIndex` (**+0x20**): **0 = "non_lethal" (the start state), 1 = "lethal"**.

## Q1. Where lethality is stored and how it is read

### Data (shipped decls, readable through `idx.py`; `.resources` are zlib)
`components/characters/player/base/weapons/hook_mine_states.cpntweaponstates`:
```
m_startState = "non_lethal";
m_states: item[0] m_name="non_lethal" (idle midnight hook_mine_idle_non_lethal, transition -> "lethal", midnight gadget_hookmine_toggle_lethal)
          item[1] m_name="lethal"     (idle midnight hook_mine_idle_lethal,     transition -> "non_lethal")
```
`.../weapons/hook_mine.cpnthookmineequipment`: `m_lethalIfInThisState = "lethal";` (equipment decl **+0x340**, reflection at 0x17b3448)
and `m_playerQuickUseState = "PLAYER_STATE_PLACE_WEAPON_AFAR"`.

### arkCpntWeaponStates (vtable 0x1c45e18), PROVEN
Fields from reflection (0x17a5d10): **m_currentStateIndex +0x20**, **m_pendingStateIndex +0x24**, m_midnightHandle +0x28. Decl `[ws+8]`:
`+0x68` start-state name, `+0x70` stateDesc array (stride 0x20: `+0` name (atomic string), `+8` idle midnight, `+0x10` transitions ptr
(stride 0x10: `+0` target name, `+8` transition midnight), `+0x18` transition count), `+0x78` count (low 24 bits).
* 0x3c34d0 (vslot 3, start): sets current = index of `m_startState`, which gives 0.
* 0x3c34a0 `GetCurrentStateDesc` returns `decl[+0x70] + cur*0x20`.
* 0x3c3a00 `RequestFirstTransition` sets pending (+0x24) = the target of `stateDesc[cur].transitions[0]` and returns the transition midnight.
  0x3c3a70 does the same for a keyed transition.
* 0x3c3c10 `CommitPending`: `if (pending >= 0) { cur = pending; pending = -1; ApplyIdleMidnight (0x3c36a0) }`. It is registered by
  vslots 5/6 (0x3c35a0/0x3c3610) as the handler for **message 0x1ed** on the weapon entity.
* 0x3c36a0 plays the state's idle midnight (the red/blue glow; the red/blue part is UNVERIFIED visually).

### The equipment's lethality test, PROVEN
`0x3b1d80 IsLethal(arkCpntHookMineEquipment*)`: `ws = 0x8e5050(entity=[this+0x10], &WeaponStatesType 0x3a7c6f0, 0)`;
`return *(GetCurrentStateDesc(ws)->name) == equipDecl([this+8])+0x340`. Callers:
* 0x3b1ccf in equipment vslot 48 (0x3b1c80, fire/spawn): it writes **projectile arkCpntHookMine +0x538 bit1 (0x2) = IsLethal()**,
  `and byte [rdi+0x538],0xfd; or ..` at 0x3b1cd4. It then copies the state onto the projectile's own WeaponStates (0x3c3a70 at 0x3b1d6e).
* 0x4adb2b HUD (`arkHUDElementEquipment`, which has the bits m_toggleLethal/m_toggleNonLethal at hud+0x663 bit0/1 and a decl field
  `m_toggleLethalNonLethalAction` (eActions_t, decl +0x108) for the button prompt).
* The thrown mine branches on `test byte [mine+0x538],2`: lethal drag/dismember or stun. Sites: 0x3ad0e7, 0x3ad3cc, 0x3adbd8, 0x3b02ea, 0x3b0803.
  The victim-side `m_draggedLethally` (reflection 0x1787e6a, on an NPC midnight-proxy component) and `arkMsgHookMineForceStun` are downstream of this bit. I did not trace them further.

### What +0x778, +0x790 and +0xc are
`0x84dab0` returns `[[0x3a72280]+0x1e8968]`, the component manager. Vslots 15/16/17 are generated per class and have the same shape in every class with a different offset
(HookMineEquipment +0x778, WeaponStates +0x790). vslot 15 (0x3b1ab0/0x3c33d0) spin-locks mgr+0xd24 and calls 0x4de550(mgr+0x778, this), which looks like a pool release.
So +0x778 is the **arkCpntHookMineEquipment factory pool** (`cpnt_poolsize_arkCpntHookMineEquipment`) and `+0xc` is a pool counter or size (exact meaning UNVERIFIED).
It never changes with the mode.

### Read chain for the mod (reader thread, no calls)
1. Machine (player state machine component) `+0x380` -> `[+0x68 + hand*8]` slot (**hand 1** = the LT/left slot, which is what ChangeWeaponState uses).
   The slot's component is `[slot+0x18]`, valid if handle `[slot+8]` resolves (0x692120). Or use the mod's existing left-weapon pointer.
2. Check `*(u64*)weapon == base+0x1c43928` (arkCpntHookMineEquipment).
3. entity = `[weapon+0x10]`. Scan `entity+0x270` (component ptr array, count `[entity+0x278] & 0xffffff`) for vtable `base+0x1c45e18` to get `ws`.
4. **lethal = ([ws+0x20] == 1)**. To be strict, compare the name: `[[ws+8]+0x70] + cur*0x20` name vs equipDecl+0x340.
   `[ws+0x24] != -1` means a switch is in flight.

## Q2. What enters ChangeWeaponState (74)
There are no immediate requests. `reqs.py` for 0x49 at 0x869570/0x869760/0x86a290/0x86a2c0/0x869c90 found nothing; it is table-driven.
`components/characters/player/billie/controller.cpntplayercontroller`, `m_statesMapping` (41 items). Every `ACTION_POWER_WEAPON` row:
| item | m_usage | m_state |
|---|---|---|
| 12 | (default PRESSED) | USE_POWER |
| 14 | BUTTON_DOWN | FIRE_WEAPON |
| 15 | PRESSED | PLACE_WEAPON |
| 16 | PRESSED | THROW_WEAPON |
| 27 | PRESSED | DRAW_LEFT_HAND |
| 31 | PRESSED | USE_PHANTASMAL_LINK |
| **37** | **BUTTON_RELEASED** | **PLACE_WEAPON_AFAR** (Hook Mine throw) |
| **38** | **BUTTON_DOWN** | **CHANGE_WEAPON_STATE**, m_priorityWhenAvailable 0 |
| 40 | PRESSED | USE_MIMIC |

`stateMapping_t` fields are m_usage, m_action, m_state, m_priorityWhenAvailable and m_triggerThreshold. buttonUsage_t is BUTTON_PRESSED/DOWN/RELEASED/TOGGLE_DOWN.
ACTION_POWER_WEAPON is LT on the gamepad (`default_keybinds.cfg`, R4 §0). The keyboard uses the same action (its mouse/key bind is in default_keybinds.cfg).
No other action maps to state 74, so it is **not** ZOOM, RELOAD or ALT_FIRE.

## Q3. Hold condition, and why the 1.6 s hold looked like a failure
State decl `arkDeclPlayerStateChangeWeaponState` (size 0x98). The constructor defaults (0x6d9e83) and reflection (0x184c330):
* **m_holdDurationBeforeActualActivation +0x90 = 0.3 s** (0x3e99999a). The shipped decl does not override it.
* **m_disableStateChangeIfFireUnavailable +0x94 = true**: can-enter 0x6da060 then requires the slot-1 weapon's vslot +0x110.
  For the mine that is 0x3c2e80 -> 0x68d070 `[weapon+0xa8] > 0`, i.e. **a loaded mine**. It also needs a WeaponStates component on that entity and the generic 0x8be330.
* **m_clearButtonOnDeactivation +0x95 = true**: at activation it clears the state's button bit in the controller
  (`[machine+0x350]+0x98 &= ~(1<<state+0x34)`), so **the release no longer fires PLACE_WEAPON_AFAR**.

Flow, PROVEN except where marked:
* Start (base vslot 4, 0x8be0d0): `state+0x34` = the controller mapping bit for id 74 (0x85ef80).
* Enter (0x6d9df0): timer `state+0x50` = 0. If the duration is <= 0 it executes immediately.
* Update (0x6da320):
  * Timer < 0.3 s: if the mapping bit is still down (`[ctl+0x98] & [ctl+0x80] & bit`), `timer += dt`.
    Otherwise it calls 0x8bd210, which exits the state. No transition was requested, so nothing changes, and the release then maps to PLACE_WEAPON_AFAR, which **throws**.
  * Timer >= 0.3 s: it executes (0x8bdf50), sets `state+0x54` bit0, clears the button, and the base update calls vslot 0xc8 = 0x6da1b0.
    That sets **pending = the other state** (0x3c3a00) and plays the toggle midnight.
* Finish (vslot 0xb8, 0x6d9fc0): posts **msg 0x1ed** to the weapon entity id `[ent+0xb0]`, which runs 0x3c3c10 and **flips m_currentStateIndex**. Exactly when this fires relative to the midnight end is UNVERIFIED.

So: a 0.3 s hold is borderline and throws, which matches the measurement. A 1.6 s hold toggles and suppresses the throw, which also matches.
The measurement sampled state 73 (LOCK_HEART_TARGET) and a pool counter, so it could not see the toggle. Two holds probably went lethal and then back to non_lethal (UNVERIFIED).
No upgrade or attribute gate was found. The only gates are a loaded mine (+0xa8 > 0), the mine in slot 1, and the generic state can-enter.

## Q4. Recommendation
**Trigger:** hold native LT (ACTION_POWER_WEAPON) continuously for **~0.5 s** (more than 0.3 s plus frame jitter) with the Hook Mine drawn and idle in the left slot, then release.
Send this as one clean hold through the PadComposer path, with no 2-frame debounce gaps.
After the toggle the game itself drops the button, so the release does not throw. The mod must not add its own tap or release-throw logic for the same gesture
(GadgetGrip's 150 ms tap is the throw; keep it under 0.3 s). Suggested VR gesture: a long squeeze with the trigger up, or a dedicated button, mapped to "LT held 500 ms".
**Do not** request state 74 directly through 0x869760. Update aborts on the first frame unless the controller's LT mapping bit is down, so it still needs LT held.
It also runs off the game thread, and it would bypass the throw/release arbitration.

**Verify** (reader thread, 50 ms sampling):
1. `IsInState(machine, 74)`: `[[machine+0x98] + 0x10 + 74*8]` is the state object. Its vtable is base+0x1cbbf78, `[obj+8] == 0x4a`, and active is `byte [obj+0x20]`.
   It should be active from about the press until the toggle finishes. `float [obj+0x50]` is the hold timer and `[obj+0x54]&1` is "executed".
2. `ws+0x24` goes from -1 to the target at about 0.3 s, then back to -1 while **`ws+0x20` flips 0<->1**. This is the authoritative check.
3. End to end: after the next throw, the projectile's arkCpntHookMine `+0x538 & 2` = lethal (bit set by 0x3b1cd4).
4. HUD prompt bits at hud+0x663 (optional).

**Risks:** with no loaded mine, can-enter fails, so there is no toggle. The release then goes to PLACE_WEAPON_AFAR (an out-of-ammo animation; UNVERIFIED).
Mid-equip, sliding or another LT state can win the mapping arbitration. Every hold toggles once, so the mod must know the current index ([ws+0x20]) and not "set lethal" blindly.
A sim scenario: hold LT 0.6 s, then idle 1.5 s, twice. Expect `ws+0x20` to go 0 -> 1 -> 0, state 74 active in each window, and no throw. A control hold of 0.15 s should throw with `ws+0x20` unchanged.

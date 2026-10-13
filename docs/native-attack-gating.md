# Native sword-attack gating: what starts, buffers, converts and cancels a slash

Steam build 17576867, image base `0x140000000`. Addresses are RVAs unless written `0x14…`.
This is static analysis only. Ghidra ran read-only at low priority, and nothing was read from a running game.
**VERIFIED** means I read it in a decompile, disassembly, PE bytes or decl text. **INFERRED** means it is deduced and needs a live check.
Raw dumps are in `artifacts/claude/attackgate/out/` (`b1..b10`, `arm1`). Decls are in `artifacts/claude/attackgate/decls/`.
Rerun a query with `artifacts/claude/attackgate/run.cmd <name> dc:0x14… vt:0x14… rf:0x14… ds:0x14…:n`.
The helper scripts in `artifacts/claude/attackgate/scripts/py/` are:
* `vtref.py <rva>`: vtable owner, slot and RTTI class.
* `rel.py <rva>`: rel32 code references.
* `dscan.py <start> <end> <disp…>`: field-offset users.
* `msgids.py <id…>`: entity event id to `ARK_MSG_*` name.

## Summary: four native mechanisms drop or cancel VR swings

1. **A 160 ms RT hold turns the slash into a "focused" (heavy) charge** (VERIFIED code; the timing is from the scene files).
   Every slash scene sends `FOCUSED_ATTACK_CAN_START` at 125–200 ms of scene time. At the mod's 1.7× rate that is **74–118 ms real time**.
   If RT is still down at that event, the slash scene is replaced by `sword_attack_focus_in`, which has no hit frames.
   The real slash is only launched when RT is released (about 160 ms), so it lands about 140 ms late.
2. **RB pressed during the slash cancels it.** The mode rule `SLASH → BLOCK` has `m_quit = true`, and the controller requests Block every frame while RB is held (`BUTTON_DOWN`).
   The VR re-press of RB at RT release (about 160 ms) therefore quits the slash before or inside its hit window, or quits the re-launched slash from point 1.
   This is VERIFIED down to the quit. That the parry_in scene then also stops the slash scene's active frames is INFERRED.
3. **RT does nothing while Block is active.** Block and Slash both use body part `0x04`, and Block locks it. No `BLOCK → SLASH` rule overrides that, so the start request is rejected (VERIFIED).
   RT is an edge-triggered (`BUTTON_PRESSED`) mapping, so a rejected press is lost.
   Releasing RB in the same frame as the RT press only works if the Block stop is processed before the Slash start. That depends on the order of the controller and FSM component updates (INFERRED).
4. **A press during an active slash is not queued by the FSM.** `m_allowSelf` is 0.
   The slash keeps one private flag, `m_wantChained`. It honours that flag only at `SLASH_CAN_BE_INTERRUPTED`, only if RT is **still held** at that moment, and only if RB is **not** held.

There is no cooldown, stamina, spam counter or minimum press duration on the player's attack (VERIFIED).

## 1. FSM plumbing (VERIFIED)

| Item | Where | Notes |
|---|---|---|
| Pool indices | live dump `artifacts/research/native-arm-assets/native-carry-fsm-v95-44016.json` | **Slash 39 (0x27)**, **Block 44 (0x2c)**, Recoil 40, Idle 22, Blinking 12, Sliding 7, WallCrouch 11, Assassination 13 |
| State input action `S+0x34` | same dump | Slash `0x13` = ACTION_MELEE (RT), Block `0x15` = ACTION_BLOCK (RB) |
| Controller action masks `ctrl=*(FSM+0x2f8)` | `7f8270` | `+0x80` enabled, `+0x88` pressed edge, `+0x90` released edge, `+0x98` held, `+0xa0` toggle. Offsets VERIFIED by usage; names INFERRED |
| State mappings | `controller.cpntplayercontroller` + `7f8270` | 16-byte items `{usage, action, state, priority}`. Usage 0 `BUTTON_PRESSED` uses `+0x88`, 1 `BUTTON_DOWN` uses `+0x98`, 2 `BUTTON_RELEASED` uses `+0x90`, 3 `BUTTON_TOGGLE_DOWN` uses `+0xa0`. A match calls `8022e0(fsm, state)` (RequestStart) |
| | | `ACTION_MELEE → PLAYER_STATE_SLASH` is the default `BUTTON_PRESSED` (one frame only). `ACTION_BLOCK → PLAYER_STATE_BLOCK` is `BUTTON_DOWN` (requested every frame while held) |
| Request queue | `FSM+0x220` | 12-byte items `{type, state, param}`. Type 1 = start (`8022e0` / `8024d0`), type 2 = stop (`802a20`, which is what `854190` "exit self" calls) |
| FSM frame | `8063f0` (factory update `950530`) | (1) priority mappings `7f8040`; (2) **process the queue** with `804850` until it is empty; (3) mode switch; (4) each active state's update (vfunc `+0x68`). Stops that a state requests in step 4 are processed on the **next** frame |
| Controller frame | `7fb5c0` (factory update `94f9f0`) | Runs the main state mappings (`7f8270`) and input combos. Its order relative to `950530` is INFERRED; both are factory slot `+0x20` updates |
| Transition rule matrix | `pool+0x2e0 + from*0x5a + to` | bit0 = allow, bit1 = quit. Built by `864330` (defaults) and `8649e0` (decl rules) |

Default rule from `864330` for states A → B (VERIFIED):
```c
if (A == B) { allow = A.decl+0x69 & 1 /*m_allowSelf*/; quit = A.decl+0x69>>1 & 1 /*m_quitSelf*/; }
else { allow = (B.decl+0x65 /*m_bodyParts*/ & A.decl+0x66 /*m_lockedBodyParts*/) == 0;
       quit  = (B.decl+0x65 & A.decl+0x67 /*m_exclusiveBodyParts*/) != 0
               || (A.decl+0x69 & 4 /*m_cancelable*/ && B.decl+0x69 & 0x10 /*m_canceling*/); }
```
The field names come from the reflection string order next to `m_lockedBodyParts` (INFERRED). The semantics are VERIFIED.
Explicit rules in `default.playermode` override these defaults. `m_allow` defaults to true; this is INFERRED from rules that re-enable after `ANY → X m_allow=false`.

How `804850` handles a start request (VERIFIED):
1. The state must exist and be enabled.
2. `CanStart`, which is vfunc `+0x30`, or `+0x38` when the request carries params.
3. For **every currently active state X**, `matrix[X][new] & 1`. Active states already stopped earlier in the same batch are excluded.
4. The code body-part locks `FSM+0x2e4/+0x2e9`.

Then all stops run (vfunc `+0x60`), every active X with `matrix[X][new] & 2` is quit (vfunc `+0x60`), and the new state runs activate (`+0x40`, then `+0x50`).
A rejected request is **dropped**. There is no FSM-level retry or buffer.

Decl values (VERIFIED from ctors `853c90` → `854dc0` → state ctor; the decl text does not override them):

| decl | +0x60 id | +0x65 bodyParts | +0x66 locked | +0x67 exclusive | +0x69 flags | +0x70 input mode | +0x78 cooldown | +0x7c max dur | +0x88 |
|---|---|---|---|---|---|---|---|---|---|
| Slash (`86a710`) | 0x27 | 0x04 | 0x04 | 0 | 0 (no allowSelf) | 1 | 0 | 0 | 1 |
| Block (`858d50`) | 0x2c | 0x04 | 0x04 | 0 | 0 | 1 | 0 | 0 | 0 |
| Recoil (`863ce4`) | 0x28 | 0x04 | 0x04 | 0x01 | 0 | 0 | 0 | 0 | 1 |

Explicit rules involving these states (VERIFIED, `default.playermode` `m_rules`):
* `[89] SLASH→BLOCK m_quit=true`
* `[173] SLASH→BLOODTHIRSTY quit`
* `[208] SLASH→USE_TIMESHIFTDEVICE quit`
* `[35/39/81] SLASH→MANTLE/DROP_READY/VAULT quit`
* `[205] USE_TIMESHIFTDEVICE→SLASH allow=false`
* **There is no `BLOCK→SLASH` rule**, and there is no Recoil rule.

Consequences:
* **matrix[BLOCK][SLASH] = 0**: an attack is impossible while Block is active.
* **matrix[RECOIL][SLASH] = 0**: an attack is impossible during recoil.
* **matrix[SLASH][SLASH] = 0**: no re-entry.
* **matrix[SLASH][BLOCK] = allow+quit**: block cancels an attack at any time.

## 2. Slash state (`arkPlayerStateSlash`, vtable `1c85d28`)

| slot | fn | role |
|---|---|---|
| +0x30 / +0x38 | `86c3e0` / `86c470` | CanStart: `855230(S)` and the right-hand weapon component `+0x11c == 1` (sword). `855230` = body-part code-lock check `854890`, then `!855300` (cooldown `decl+0x78`, which is 0), then no pending EquipChange (pool 53/64 `+0x118`) |
| +0x40 | `86c860` | pre-activate: blink attack (`+0xd3`) if Blinking (12) is active and the attribute `S+0x10c` is > 0 |
| +0x50 | `86aa80` | activate: computes `+0xcc/+0xcd/+0xce`, sets **`+0xcf` m_focusAllowed** = not shadow mode, not state 20, not Sliding; clears `+0xd0..+0xd9`; calls `854e50`, which runs enter now, or defers it while WallCrouch (11) uncrouches (`decl+0x8a m_waitWallUncrouch = true`) |
| +0xc8 | `86c630` | enter: `86b050` takedown arbitration, otherwise `86d320` (target) and **`86cbf0` perform** |
| +0x68 | `86d060` | update (below) |
| +0xb8 | `86bec0` | "input released" handler, only reachable while focusing |
| +0x60 | `86bc60` | exit. It does **not** stop a normal slash scene (only blink `+0xd3` / focus `+0xd1`) |

Runtime fields (names from the reflection list `m_focusAllowed … m_isChained`, INFERRED; offsets VERIFIED):
* `+0xcf` m_focusAllowed
* `+0xd0` m_focusCanStart
* `+0xd1` m_isFocusing
* `+0xd2` "focused" (held long enough) / aborted
* `+0xd3` blink attack
* `+0xd5` **m_buttonReleased**
* `+0xd6` **m_wantChained**
* `+0xd7` m_isChained
* `+0xdc` = action id mapped to Block (0x15)
* `S+0x38` = state elapsed time (accumulated in `8555a0`)

Perform `86cbf0`:
1. Evaluates `slash.rulehandler`. The right/left alternation uses `slash.lastSlashType` and `slash.timeSinceLastSlash <= 1`. The variants `a`/`b` are picked at random.
2. Plays the outcome scene. If the previous outcome scene `S+0x100` is still playing, the scene starts at the outcome `+0x4c` offset (`m_chainedSlashStartTime = 0.1 s`, name INFERRED).
3. Applies the `ARK_ATTRIBUTE_SLASH_SPEED_FACTOR` rate (`S+0x108`) through `7d8fe0`.

### 2a. Event handlers (VERIFIED, ids resolved with `msgids.py`)

| event | handler | effect |
|---|---|---|
| 0x154 `FOCUSED_ATTACK_CAN_START` | `86d3d0` | `+0xd0 = +0xcf` |
| 0xbf `SLASH_CAN_BE_INTERRUPTED` | controller `7fbeb0` (registered on the right-arm entity `body+0x680` at `7f98bf`) → **`86bb60`** | the slash **exits** here, see 2c |
| 0xe7 `ATTACK_HIT` | `86c1b0` | counts hits on the same target within `decl+0xbc` (1.0 s), stored in `S+0xf0`. This feeds the fatality choice and is **not** a gate |
| 0xe8 `ATTACK_HIT_CAN_START` | `86c310` | plays `m_attackHitMidnight` |
| 0xa2 `DAMAGED` | `86d3a0` | aborts a focus charge (plays `m_cancelMidnight`) unless the damage type is in `decl+0xb0` (`m_focusAbortIgnoredDamage`) |

### 2b. Update `86d060` (VERIFIED, abridged)
```c
8555a0(S);                                   // base: release/max-duration logic, ONLY when flag bit2 (scene-wait) is clear
if (S->d0 /*focusCanStart*/ && !S->d1) {
    if (!S->d2 && !S->d5 /*button not yet released*/) 86cf10(S);   // START FOCUS CHARGE
}
...
held = ctrl->m80 & ctrl->m98 & (1ull << S->m34);
if (!S->d5 && !held) S->d5 = 1;              // m_buttonReleased
if (S->d5 && !S->d6 && held) S->d6 = 1;      // m_wantChained (re-press after a release)
86d200(S);   // once released: send ARK_MSG_AI_MELEE_DEFENSE_OPPORTUNITY (0x3e) to the target
```
* `86cf10` (focus start):
  * sets `+0xd1 = 1`
  * stops the camera scene
  * **plays `decl+0x80` = `sword_attack_focus_in`**, which replaces the slash scene and is played without the speed factor
  * clears flag bit2, so the release becomes observable
* `86bec0` (release while focusing) sets `+0xd2 = (elapsed >= decl+0xa0)` and then runs **perform** (`86cbf0`).
  * `decl+0xa0 = 0.5 s`, focus-hold duration; the value is VERIFIED from ctor `86a710`, the name is INFERRED.
  * If `+0xd2` is set, the rules pick `focused_attack` → `sword_attack_focus_out` (`sword_focused.damage`).
  * Otherwise a normal slash starts **now**, at release.

### 2c. Exit or chain at `SLASH_CAN_BE_INTERRUPTED`: `86bb60` (VERIFIED from disassembly)
```c
854190(S);  S->d7 = 0;                                   // stop the Slash state
if (held(ctrl, S->dc /*ACTION_BLOCK*/)) { 8022e0(fsm, 0x2c /*Block*/); S->d6 = 0; return; }
if (S->d6 && held(ctrl, S->m34 /*ACTION_MELEE*/)) { S->d7 = 1; 8022e0(fsm, 0x27); }  // chain
S->d6 = 0;
```
The stop is queued before the start, so the chained start passes the self-rule (VERIFIED queue order in `804850`).

## 3. Timing (VERIFIED from the scene files; real time = scene ÷ 1.7)

| scene | FOCUSED_ATTACK_CAN_START | MELEE_WEAPON_ACTIVE_FRAMES | SLASH_CAN_BE_INTERRUPTED |
|---|---|---|---|
| slash_right | 160/180 ms → **94/106** | 200–332 / 220–348 → 118–195 / 129–205 | 420/446 → **247/262** |
| slash_rightb | 125/180 → **74/106** | 165–330 / 220–360 → 97–194 / 129–212 | 438/465 → 258/274 |
| slash_left | 200 → **118** | 240–340 → 141–200 | 501 → **295** |
| slash_leftb | 200 → 118 | 240–340 → 141–200 | 551 → **324** |
| slash_sneak_left/right | 200 → 118 | 240–374 → 141–220 | 369/367 → 217/216 |
| focus_cancel | – | – | 250 (rate 1.0) |
| sword_parry_ll/lr/rl/rr (NPC parry recoil) | – | – | `RECOIL_CAN_BE_INTERRUPTED` 301–304 (rate 1.0) |
| sword_recoil_* (wall) | – | – | `RECOIL_CAN_BE_INTERRUPTED` 50 (and 200) |

Two scene items per file probably correspond to rig variants (INFERRED). In stand combos, the outcome alternates right → left.

## 4. Block (`arkPlayerStateBlock`, vtable `1c81a68`), as it relates to attacks (VERIFIED)

* Block is requested every frame while RB is held. CanStart `859870` = `855230` and sword equipped.
* Release: base update `8555a0` with mode 1 and the button not held calls slot `+0xb8` `859710`, which plays `m_midnightOut` and **immediately** requests the stop. There is no exit timer.
  The stop is queued during the state update, so Block leaves the active list on the next FSM queue pass, about 1 frame later.
* Block anti-spam:
  * Activation `858f40` sets `m_canParry` (`S+0x59`) only if `now - m_lastDeactivationTime(S+0x68) > S+0x70`.
  * `S+0x70` is the out-midnight duration computed in `8590b0` from `decl+0xa8` (`sword_parry_out`, 266 ms clip).
  * So an RB re-press within about 0.27 s of a release gets no parry window (`PARRY_PHASE` 300 ms), only a plain block.
* Block update `85a160` hides the **left** arm and soft-locks a body part while the left hand is empty (equipment `+0x70 == 0`); exit `859640` shows it again. This does not conflict with Slash's body part `0x04`.

## 5. Answers

**Q1, states and transitions.**
* **Idle → attack:** RT edge, `8022e0(fsm,39)`, then `804850` (CanStart + matrix vs every active state), then activate → enter → perform. Movement states do not lock `0x04`.
* **Combo:** a press while Slash is active is rejected by the FSM (`m_allowSelf = 0`). The slash buffers it in `m_wantChained` only if RT was released first. At `SLASH_CAN_BE_INTERRUPTED` (247–324 ms at 1.7×) it is chained **only if RT is still held and RB is not held**; otherwise it is discarded.
  * After that point the state has exited, so a new press starts a fresh slash immediately. If the previous scene is still playing, the new one starts 0.1 s in.
  * Left/right alternation lasts 1 s.
* **Block → attack:** rejected while Block is in the active list (VERIFIED). It works only after the Block stop has been processed.

**Q2, cooldowns.** There is no cooldown (`decl+0x78 = 0`), no max duration (`decl+0x7c = 0`), and no stamina or spam counter (VERIFIED). The only time gates are:
* the non-interruptible slash window above
* Recoil (pool 40), which locks `0x04` until `RECOIL_CAN_BE_INTERRUPTED`: 50 ms after a wall hit, about 300 ms after an NPC parry
* WallCrouch deferral (`m_waitWallUncrouch`)
* code locks during equip changes
* the Block parry re-arm gap of about 0.27 s (parry only, not attack)

**Q3, RB during an attack.**
* RB during an attack **cancels it at any time, including the windup**: rule `SLASH→BLOCK quit` plus RB being `BUTTON_DOWN`.
* Releasing RB and pressing RT in the same frame is a race:
  * the Block stop is queued in Block's update and processed on the next FSM pass
  * the Slash start is processed in whichever pass follows the controller update
  * if the controller update runs first, the RT edge is evaluated while Block is still active and is **lost**, because RT is edge-only (component order INFERRED)
* There is no extra block-exit delay beyond that one FSM pass.

**Q4, press length.**
* There is no minimum press length and no release is needed: the slash starts on the press edge.
* Holding past `FOCUSED_ATTACK_CAN_START` (74–118 ms real at 1.7×; 125–200 ms at 1.0×) converts the slash into a focus charge.
* Releasing before 0.5 s of state time then fires a normal slash *at release*; releasing after 0.5 s fires the focused attack.
* **So a 160 ms press is not a light attack at 1.7×.** It is a cancelled focus charge followed by a slash that starts late.

## 6. Recommendations for the VR input

1. **RT pulse: 2 game frames, about 35–45 ms, and never longer than `125/speed − 1 frame`** (≈ 55 ms at 1.7×). Do the same for the thrust. This alone restores the instant light slash (hit window 97–212 ms after the press).
   Optional native heavy attack: hold RT ≥ 0.6 s (`focus_in` starts at about 74–118 ms; release after 0.5 s of state time gives `focus_out` / `sword_focused.damage`).
2. **Never press RB while the Slash state is active.** Gate on `*(u8*)(pool[39]+0x20)`, not a timer; otherwise hold off ≥ 330 ms after the RT edge at 1.7× (≥ 560 ms at 1.0×).
   If RB is held at the interrupt point, the native code goes straight to Block.
3. **Release RB ≥ 2 game frames before the RT edge**, or wait until `pool[44]+0x20 == 0`. Do not release RB and press RT in the same frame.
4. **Swings while the slash is active:** queue them and pulse RT when `pool[39]+0x20 == 0`, `pool[40]+0x20 == 0` (Recoil) and Block is inactive. This replaces the 300 ms limit with the exact native gate.
   Pressing early only works through `m_wantChained`, which needs RT held through the interrupt point and then a release within about 50 ms of the new slash starting.
5. **Optional data-only relaxation, done on the game thread like the slash-speed attribute.**
   * Patch the current mode's transition matrix (`m = *(FSM+0x90) + 0x2e0`, 90×90 bytes):
     * `m[39*90+44] = 0`: SLASH→BLOCK no allow and no quit, so RB can never cancel an attack. The interrupt hand-off to Block still works because `86bb60` stops Slash first.
     * `m[44*90+39] = 3`: BLOCK→SLASH allow + quit, so RT works from a held block.
   * Re-apply when `FSM+0x90` changes (mode switch or level load). This is INFERRED safe (same mechanism as the decl rules) and needs a headset check.
   * To disable focus conversion completely, zero `*(u64*)(slashDecl+0x80)` (`focus_in`; `86cf10` returns early). This loses the native heavy attack. Prefer recommendation 1.

## 7. Arm hide/show callers (`FUN_140774a10(u32* armIdField, char show)`, 18 callers)

Body = `*(FSM+0x300)`. Left arm = `body+0x370`, right arm = `body+0x680`. Owners come from `vtref.py` (vtable, slot, RTTI; VERIFIED) unless marked INFERRED.

| fn | owner | role | left 0x370 | right 0x680 |
|---|---|---|---|---|
| `8060c0` | FSM helper `(fsm, show)`; the same flag goes to both arms (VERIFIED `movzx edi,dl`) | see callers below | =arg | =arg |
| ↳ `848940` | arkPlayerStateShadowHide vt `1c7e978` +0x60 (exit) | exit | show | show |
| ↳ `848b70` | ShadowHide code range (INFERRED) | update/transition | hide | hide |
| ↳ `848b40` | called from arkPlayerStateAssassination vt `1c83468` +0x58 (`860eb0`, activate) | entry | show | show |
| ↳ `848ad0` | called from Assassination +0x68 (`863760`, update) and arkPlayerStateChoke vt `1c77c18` +0x60 (`82b370`, exit) | update/exit | arg | arg |
| ↳ `848a00` | callers `623100`, `848cd0` (INFERRED ShadowHide) | – | show | show |
| ↳ `821e60` | controller sub-object `ctrl+0x410` (hands/equipment manager, INFERRED) | – | `!(flag&0x10)` | same |
| `849d40` | ShadowWalk helper (INFERRED), called by controller `7fb4a0` (toggles `ctrl+0x172`) | exit/toggle | show | show |
| `84a410` | midnight-end callback registered by arkPlayerStateShadowWalk vt `1c7ed28` +0x20/+0x28 (`8495d0`/`8499f0`, LEA) | scene end | hide if `ctrl+0x172` | hide if `ctrl+0x172` |
| `88adc0` / `88b0a0` / `88b1d0` | controller mode-switch helper `ctrl+0x258` (cinematic switch; via `88a160`/`88a2e0`/`88a4c0`) (INFERRED class) | mode switch | hide only if the held object was two-hand (`+0x44 == 0x32`) | hide |
| `82f360` | arkPlayerStateClimb vt `1c78498` +0x58 | entry | `show=cond` | hide |
| `8304e0` | Climb +0x60 | exit | show | show |
| `8310a0` | Climb +0x68 | update | `show=cond` | hide |
| `83d770` | arkPlayerStateHoldObject vt `1c7a998` (via +0x58 `83c460` / +0xb0 `83dd60`) | entry | hide if two-hand | hide |
| `83ce70` | HoldObject +0x60 | exit | show (if `S+0x70`) | show |
| `846330` | arkPlayerStateSwimming vt `1c7dc68` (via +0x60 `846270`, +0x68 `846450`) | exit/update | depends on hold-object states 0x31/0x32 | =arg |
| `859640` | arkPlayerStateBlock vt `1c81a68` +0x60 | exit | show (if it hid it) | – |
| `859ff0` | Block +0xb0 | message | show | – |
| `85a160` | Block +0x68 | update | **hide while the left hand is empty** | – |
| `85a910` | arkPlayerStateCatFall vt `1c82158` +0x58 | entry | hide | hide |
| `85b1e0` | CatFall +0x60 | exit | show | show |
| `85ba50` | arkPlayerStateDead vt `1c82478` +0x58 | entry | hide | hide |

* **CarryCorpse** (vt `1c77748`) never calls `774a10`. None of the 18 callers resolves to it, and its slots (`828240`, `828d90`, `829870`, `829ff0`, …) do not call it (VERIFIED).
  Its activate is the base `853d40`. That function runs `774900(body,1)` (`body+0xc58` counter) only if `decl+0x65 & 0x10`; the counter's meaning is INFERRED.
  Arm visibility while carrying is therefore driven by the `carry_corpse_*` midnight scenes (INFERRED). **There is no left-arm hide through `774a10`.**
* **Heart:** no Heart state calls `774a10` (UseHeart `1c80b78`, PlayHeartBark `1c7c908`, LockHeartTarget `1c3d4c8`; VERIFIED negative).
  PlayHeartBark's decl claims `m_bodyParts.m_left_arm`, which is an FSM body-part resource and not visibility, and it plays `heart_use.midnightscene`.
* Block hiding the left arm when the left hand is empty matters for VR left-hand rendering while auto-block is held.

## 8. Live checks still open

* The relative order of the controller factory update (`94f9f0`) and the FSM factory update (`950530`) in a frame. This decides whether a same-frame RB-release + RT-press always fails or always succeeds.
* Whether Block's `parry_in` actually terminates the slash scene's `MELEE_WEAPON_ACTIVE_FRAMES`. The slash exit does not stop the scene itself.
* Confirm with RT held 160 ms: `pool[39]` `S+0xd1` becomes 1 at about 74–118 ms. With a 40 ms pulse, `S+0xd5` should be set before `S+0xd0`.

## Bloodfly swarms (sim, 2026-10-13, v382)

- In the test save's bloodfly shop (nests ~[13.4, 87.1, 16.96]) the attacking flies are **`arkSwarmAgentEntity`** "bloodfly_NNNN"
  (vtable RVA 0x1c44ce8; components arkCpntAnimBloodFly, arkCpntReferenceFrame, arkCpntSwarmAgentPossession), **not arkNPC**.
  The v362 proximity contact only considers small arkNPCs, so it never touches them.
- `idEntity::TakeDamage` 0x879270 (vtable slot 40, not overridden) looks up a health component (0x877720) and does nothing
  without one; these agents have none. Contact damage through TakeDamage cannot kill them.
- Native kills go through the player melee component: `arkCpntWeaponMeleePlayer` (vtable 0x1b9d508) keeps an
  `idStaticList<arkSwarmAgentContact_t,32>`; kill events `SAPE_KILL_SWARM_AGENT_WITH_FX/_WITH_ANIM`, message
  `arkMsgPlayerKilledSwarmAgent`, swarm classes `arkCpntSwarmBase` (vtables 0x1c432a8/0x1c43420) and `arkCpntBloodFlySwarm`
  (0x1c42098/0x1c42210). The contact collection and kill call are not mapped yet (headless Ghidra is blocked by the sandbox here).
- Sim: `vr_selftest.py --scenario swarm --teleport 12.4 88.2 18.6` (arrives 2 s before the sweeps). 11 native slashes with the
  blade-directed melee aim (613 sword-aim frames) did not reduce the ~20 flies; the god-mode player dies ~30 s in (Game Over 8192).
- Next: map how arkCpntWeaponMeleePlayer fills its swarm contacts (likely a sweep of the native animated blade) and either
  feed the tracked blade to it or call the swarm's agent kill directly for agents the VR blade crosses.
- Static (capstone, no Ghidra): arkCpntWeaponMeleePlayer vtable slots with 24-byte list handling: 4 (0x6302a0), 27 (0x61a750),
  30 (0x61a2b0), 31 (0x61b120), 33-36 (0x62ef80, 0x62fec0, 0x630890, 0x6302e0), 39 (0x62e550), 41 (0x634990), 42 (0x62f290).
  Melee code calls 0x8c3d30 (a typeNum-range cast) from 0x623f20/0x6240d2 while walking 16 component slots; agent possession
  methods live at 0x4c2000-0x4c5000, swarm base at 0x6a2000-0x6ab200. Kill call still unmapped: needs decompiler xrefs.
- Fly counts are not a kill measure: the level pools 201 agents (`+0xd0/+0xd1` = 0b/81 dormant, 08/80 active); arriving at the
  nests woke 63, and during 11 sword-aimed slashes the active count only drifted 58-63 (agents return/respawn,
  `m_respawnDeadAgents`). A kill needs the native kill event (arkMsgPlayerKilledSwarmAgent or the possession kill) to be observed.
- **Kill path (live call trace, v383, 2026-10-13).** Native slashes DO kill swarm agents: 15 of 17 agent deactivations came
  0.3-0.5 s after a slash. Chain: melee active frames -> `0x6b8b70(agentRecord, damageParams, killType=0)` (callers 0x62f86b,
  0x626120) -> `0x6a43a0(swarmCpnt, agentIndex, bool, bool withAnim)` -> swarm vfunc slot 39 (+0x138, with anim) or slot 38
  (+0x130, `(swarm, index)`: records the dead id at +0xa0). The agent record is pooled agent data at agent entity `+0x360`
  (vtable 0x1c417d8, stride 0x100; +0x10 entity, +0x18 swarm component, +0x84 state, 7 = dead). Passing the
  `arkCpntSwarmAgentPossession` component instead faulted (v384); the swarm's agent pool is `[[record+0x18]+0x138]+0x1a8`, stride
  `+0x1b0`. Params: engine ctor 0x780260 with the decl at `*(base+0x231f330)`, +0x24 point, +0x30/+0x3c vectors, +0x50 sword id.
- **Fix (v384):** the tracked blade's small-creature contact now includes live swarm agents and kills them with that same call
  (native_melee.hpp `kill_swarm_agent`). Diagnostic used: `src/native_call_trace.hpp` (knob build/trace-calls, hex RVAs).
- **v385 crash -> off by default (v386).** With the pooled record (+0x360) the direct kill applied 10 times without a fault, then
  the game died with heap corruption (0xC0000374). Most likely a double kill: 0x6b8b70 still runs the swarm kill dispatch for an
  agent whose state is already 7, so an agent the blade killed while it sat in the native swing's own contact list is killed
  again. The direct kill is now behind `build/swarm-contact-kill`. A safe version must feed or prune the native
  `arkSwarmAgentContact_t` list (melee component) instead of calling the kill alongside it. Meanwhile the native sword-aimed
  slash kills flies (v383 trace).
- **Guarded blade kill (v388-v392, default on).** `NativeSwarmKillGuard` hooks 0x6b8b70 and returns at once for an agent whose
  record state is already 7, so neither the blade nor the native swing can kill an agent twice. Sim v391 (knob on): three runs,
  36 blade kills, 11 native re-kills skipped, no crash; v392 (default): 15 kills, 4 skipped, no crash. Off with
  `build/no-swarm-contact-kill`. Test-only note: `build/sim-invulnerable` hooks TakeDamage, so NativeMelee::verify accepts the
  hooked prologue (`take_hooked`); before v391 that silently disabled all blade contact damage in combat sims.

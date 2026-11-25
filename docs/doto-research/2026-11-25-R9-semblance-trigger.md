# R9: how the flat game triggers Semblance (face steal), and why LT during a choke does nothing

DOTO `Dishonored_DO.exe` (Steam build 17712087), RVAs (image base 0x140000000). Static only: no game run, no build, no sim.
PROVEN = read in the disassembly or in the shipped decl text. INFERRED = deduced, consistent with the code. UNVERIFIED = needs a live read.
The decls were extracted with `re-mine/idx.py` (from the R7 scratch folder) into `/tmp/claude-1000/-home-hollownet/a690a703-d6b6-43bf-b470-4d9d7f6d77f7/scratchpad/re/r9/decls/` (temporary; re-extract with `idx.py get REGEX outdir`). The files are `billie/states/mimic_choke.playerstatechoke`,
`use_mimic.playerstateusemimic`, `base/states/choke.playerstatechoke`, `billie/modes/default.playermode`,
`base/modes/default.playermode`, `billie/controller.cpntplayercontroller` and `powers/mimic/*`.

## TL;DR
* **Semblance does not run inside the ordinary choke (41).** It has its own choke state, **95 = `PLAYER_STATE_MIMIC_CHOKE`**. A **fresh LT press**
  (`ACTION_POWER_WEAPON`, edge `BUTTON_PRESSED`) **starts that state instead of a choke**. Conditions: Semblance equipped, Billie
  not in any choke, and the choke sensor holding a valid target (the same "from behind" target the RB choke uses; mimic only allows non-combat targets).
  Chain: LT press -> `USE_MIMIC` 102 -> it requests 95 -> 95 plays the mimic in/loop/win scenes -> `MIMICKING` 96.
* **LT during state 41 is rejected by the FSM body-part lock.** Choke (41) has `bodyParts = locked = 7` (body, left, right). UseMimic 102 and UsePower 63
  have `bodyParts = 2`. No `CHOKE -> USE_MIMIC/USE_POWER/MIMIC_CHOKE` rule exists in any playermode. Because the input is an edge,
  the rejected press is lost. This matches the sim exactly: no 63, no 102, no 96, `+0x120` = 0.
* Second path: **LT on an already unconscious NPC** (the interaction-sensor target) takes the face from the body
  (`m_allowMimicOnAsleep` defaults to true). So "RB choke until unconscious, release, then a new LT press while looking at the body" also works,
  but it plays the "win_ground" scene, not the takedown.
* No hold is needed. MIMIC_CHOKE's `m_chokeDuration = 0`, so its loop wins on the first loop frame. **ACTION_INTERACT held during MIMIC_CHOKE
  cancels it.**

## 1. State ids and classes
| id | enum | class / decl | Tag |
|---|---|---|---|
| 41 | `PLAYER_STATE_CHOKE` | `arkPlayerStateChoke` (vt `0x1cfd3e8`); decl ctor `0x892500` writes `+0x60 = 0x29`, `+0x65 bodyParts |= 7`, `+0x66 locked |= 7` (`0x892696..`) | PROVEN |
| 95 | `PLAYER_STATE_MIMIC_CHOKE` | **the same `arkPlayerStateChoke` class**. Data: `billie/states/mimic_choke.playerstatechoke` (`m_id = "PLAYER_STATE_MIMIC_CHOKE"`), mode state list item[81]. The choke code branches on its own id: `cmp [state+8],0x5f` at `0x893048`, `0x8948b6`, `0x894cfc`, `0x896b66`, `0x896ba4`; `0x891b20` and `0x3c8af0` treat pool[41] (`+0x158`) and pool[95] (`+0x308`) the same way (`0x893e20`) | PROVEN |
| 102 | `PLAYER_STATE_USE_MIMIC` | `arkPlayerStateUseMimic` (vt `0x1cbfc08`), **a subclass of UsePower** (ctor calls `0x8bf210`; decl ctor `0x8bf1c0` sets bodyParts 2 and exclusive 2, `+0x60 = 0x66`) | PROVEN |
| 96 / 100 | MIMICKING / CANCEL_MIMICKING | decl ids `0x60` / `0x64` | PROVEN |

The enum order in the strings dump also puts MIMIC_CHOKE at 95 (between CANCEL_VOID_STRIKE 94 and MIMICKING 96). Correction to R2 §2.3:
`[[machine+0x98]+0x340]` is **pool entry 102 (UseMimic)** ((0x340-0x10)/8 = 0x66), not a choke sub-object. Its `+0x88` is UseMimic's
**result code** (below), so "`==3` choke phase" in R2 is wrong.

## 2. Input mapping and FSM rules (shipped data, PROVEN)
* `billie/controller.cpntplayercontroller` `m_statesMapping`: item[12] `ACTION_POWER_WEAPON -> USE_POWER` and item[40] `ACTION_POWER_WEAPON -> USE_MIMIC`.
  Both use the default `BUTTON_PRESSED` (one-frame edge). item[17] `ACTION_BLOCK -> CHOKE` (RB, PRESSED). LT = `ACTION_POWER_WEAPON` (`default_keybinds.cfg`).
* `billie/modes/default.playermode` edits the base rules: [116] `USE_POWER -> USE_MIMIC` (quit, inherited), [117] `USE_MIMIC -> USE_POWER` allow=false,
  [214] `USE_MIMIC -> MIMIC_CHOKE` (allowed). No rule mentions CHOKE together with USE_MIMIC, USE_POWER or MIMIC_CHOKE, so the default
  body-part rule applies: `allow(41->102) = (2 & locked41=7) == 0` = **false**, and the same for 63 and 95.
  The default-rule formula is PROVEN in D2 (`docs/native-attack-gating.md`). That DOTO uses the same formula is INFERRED, but the sim result agrees.
* Starting 95 from 102: 95 is exclusive on all parts, so 102 is quit when 95 starts (INFERRED, same default rule). A short tap is enough.

## 3. UseMimic 102 flow (PROVEN unless marked)
* CanStart `0x6e58c0`: UsePower CanStart `0x8bfcb0` (equipped power present, generic `0x8be330`, power vt+0x100 = cooldown `0x671d30`).
  Then **power decl `+0x1a0 == 15`** (Mimic).
* Start `0x6e4dd0`: base `0x8bf270` (mana check `0x671a10`; if OK, calls **power StartAim vt+0x118 = `0x3b5240`**, which sets `+0x120 = 1` when 95 can start).
  Then `+0x88 = classify 0x6e5560` (or 4 when `0x671a10` fails, 5 when no power), then `switch(+0x88)` (jump table `0x6e50cc`):

| `+0x88` | condition in `0x6e5560` | action |
|---|---|---|
| **0** | `0x86a2c0(machine,95)` (MIMIC_CHOKE can start) and choke sensor **0x17** has a target (`[sensors+0x17*8+0x20]+0x50`) | **request 95** `0x869570` at `0x6e4ebc`; `+0x8c` = target id |
| 1 | else: target = **interaction sensor 0x21** (`+0x90`, set at `0x6e53d0`) entity; mimic filter `0x3b5550` OK; NPC `+0xd9 & 0x20` (asleep; needs decl `+0x1c8 m_allowMimicOnAsleep`, default 1) or `&8` (dead; `+0x1c9 m_allowMimicOnDeath`, default 0) | face from a body: `0x8bd0d0`, `+0xa4=1`, decl `+0xa8` scene (`m_corpseMidnight` = power_mimic_win_ground) |
| 2 | awake target, attribute at `attrs+0x164` > 0 (`0x6e5960`; combat-mimic upgrade, INFERRED), `!0x6143c0` | combat mimic (`m_combatMidnight`, vt+0xb8, `0x6e51e0`) |
| 3 | no target but already mimicking (`arkCpntMimic+0x4c == 2`) | request CancelMimicking 100 (`0x869760`) |
| 4 / 5 | invalid target, no mana, or no power | HUD reason (`0x8bfea0` with 0x15..0x17), fail scene |

The other branch of StartAim (`pool[102]+0x88 == 3` -> power vt+0x120) is the cancel-mimicking path. It is not a choke phase.

## 4. MIMIC_CHOKE 95 (choke class, PROVEN unless marked)
* CanStart `0x8946e0`/`0x894730`: target = the choke sensor's entity. Same checks as the RB choke (alive, range/rules `0x8d3340` or ShadowHide).
  For id 95 it also rejects when the target is alerted (`0x614300`/`0x6142c0` true, INFERRED meaning: the "combat choke" branch) or when choke fields
  `+0x10e` / `+0x115` are set (drop or slide choke, INFERRED names), plus a flag check `0x959460`.
* Init `0x893048`: hold-button bit `+0xe8 = 0x85ef80(ctl, id==95 ? 63 : 41)` = the action mapped to USE_POWER (**LT**) for 95, and to CHOKE (**RB**) for 41.
  `+0xec` = the INTERACT (76) bit.
* Phase `+0xc0`: 0 in, 1 loop (`0x896b20`), 2..4 win, lose and out (`0x896ed0`/`0x896d30`/`0x896de0`). Loop: `+0xd4` accumulates dt only while the hold
  button is down (reset to 0 when released), and `+0xd8` always accumulates. **Win when `+0xd4 >= duration`**, where duration = decl `+0x158`
  (or `+0x15c` in combat) × attribute (95: `MIMIC_CHOKE_LOOP_DURATION_FACTOR` via `attrs+0x154`; 41: `CHOKE_LOOP_DURATION_FACTOR`).
  Cancel when (`+0xd8 >= +0x160` lose duration and button up) **or (95 only) INTERACT held**.
* Data: base choke 3 s / 3 s; **mimic_choke `m_chokeDuration = 0`, `m_combatChokeDuration = 0`, `m_loseChokeDuration = 1`**, `m_throwAction = NONE`.
  So 95 wins on the first loop frame and the length comes from the animation alone. 41 needs RB held for 3 s (what the VR choke does).
* After the win: `arkCpntMimic` `0x3d9d10` requests MIMICKING 96 (INFERRED link from the win scene; 96 requested at `0x3d9d22`/`0x3dac72`).

## 5. What the VR mod must send (answer to Q4)
1. **Do not send RB (the choke) when the intent is Semblance.** Once 41 is active, nothing on LT can reach Semblance until 41 ends.
2. Billie behind an unaware NPC, with the choke prompt available (choke sensor target valid) and Semblance equipped: send a **fresh LT press**
   (LT up for at least one frame before, then down). Hold it about **0.1-0.3 s**, then release. The edge is consumed on the first frame, and holding longer is harmless (INFERRED).
   Native state sequence: 102 for about a frame, then 95 (in/loop/win scenes), then 96.
3. Do not send INTERACT (X) while 95 runs. It cancels the mimic choke.
4. Keep `dxgi_probe.cpp:17682` (the left fist zeroes LT) from eating the press. The gesture is "hand to the head, squeeze", not a choke grab.
5. Fallback for an NPC already knocked out by a normal choke: after 41 ends, look at the body (interaction sensor target)
   and send a fresh LT press. That gives case 1, the face from a sleeping body.

Sim check: states 102 -> 95 -> 96. `pool[102]+0x88` = 0 at the LT frame (1 = body path, 4 = failure). Mimic power `+0x120` = 1 on success.
UNVERIFIED live: the 1-frame order between the USE_POWER and USE_MIMIC requests (rules 116/117 make 102 win either way), and the exact NPC alert predicate meanings.

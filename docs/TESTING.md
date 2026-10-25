# Headset-free testing

All tools run against the live game (Dishonored 2 started from Steam, a save loaded).

## Self test
`python tools/vr_selftest.py --dll d2vr_render_probe_arm_vNNN.dll --scenario all [--load-save]`

Runs a simulated-VR scenario (`hud`, `walk`, `wheel`, `all`, `jump`, `lean`, `swing`, `grab`, `swim`, `power`, `powers`) through `tools/inject_probe.py`, closes the
pause menu if the game opened it, saves the VR UI screens as PNGs in the run folder (`ui-dump-png/`) and
prints PASS/FAIL for: HUD screen shown, nothing clipped at its edges, HUD/wheel never empty, injection.

## Test commands
Write lines to `artifacts/vr-command.txt` while the mod runs; answers go to `artifacts/vr-command-result.txt`.

| Command | Effect |
|---|---|
| `unlock` | 100 runes, infinite coins, every Black Market item |
| `cvar <name> <int>` | set any game cvar, e.g. `cvar ark_god_player 1` |
| `uidump on` / `uidump off` | save the HUD screen and wheel textures to `artifacts/ui-dump/` every 500 ms |
| `pad a\|b\|x\|y\|start [ms]` | pulse a button on the mod's virtual pad (load screen: `pad a`, pause menu: `pad b`) |
| `console <line>` | queue an engine console command on the game thread, e.g. `console setviewpos x y z [yaw] [pitch]` (eye position, metres), `console god`, `console map <name>`, `console LoadGame` |
| `trace <metres> [down]` | one native world sphere-cast from the eyes along the view (or straight down); prints hit, normal, entity |

## In-engine playground
- Damage: `--scenario damage --damage bullet_regular_npc` hurts the player every 3 s through the engine's `damage player1 <decl>` (god mode is switched off first). Timed eye frames (`xr-t*-left.rgba` in the run folder) show the damage vignette; HUD screen dumps are in `ui-dump-png`. Decl names are in `base/game1.index` (`damage/...`).
- Teleport: `--teleport X Y Z` on `vr_selftest.py`, or `console setviewpos`. Known spots in `campaign/dunwall/escape/streets`:
  water `60.5 -79 -8.5`, save start `-91.5 -92.7 4.95`.
- Linux sim teleport (no `vr-command.txt` timing needed): gameplay script version 8 gives a segment a `"console"` line, which the DLL queues once
  through the same `console` path, at the first script tick at or after the segment's start while in gameplay (a line scheduled during a load waits
  for gameplay; lines fire in order). Use `scenario_lib.console(t_ms, scenario_lib.setviewpos(x, y, z, yaw))` in a scenario under
  `tools/linux/scenarios/` (a 1-frame segment). The line is at most 200 chars from `[A-Za-z0-9 ._-]` and starts with a letter (no `;`); the
  transport token is percent-encoded (`%20` space). Each request is logged in `artifacts/simulated-gameplay-console.txt` (`elapsed_ms line`)
  and the queue result (`console: queued ...`) in `vr-command-result.txt`. Example: `tools/linux/scenarios/authored_turn.py`.
- `console god` toggles player invulnerability (cvar `ark_god_player`). There is no `noclip`/`give`/`spawn` console command in this build.
  In the Linux sim it did not keep the player alive (nor did `buddha player`): bloodflies near the G1 loot spots killed the player three times
  with god and buddha on. Use the dev knob `--knob sim-invulnerable` (`src/native_sim_invulnerable.hpp`, the player ignores damage).
- G1 loot measurement (hands plan Task 1): knob `build/loot-reach-trace` writes `artifacts/native-loot-reach.csv` (strictly read-only, at most
  8192 rows, flushed every 64): one row per routed stage-0 cycle and owner in the hand-volume Use result set (one owner-0 row for an empty
  set): `t,side,palm_x..z,dir_x..z,owner,spawn,takeable,set_count,rank,los_called,los_blocked,selected_owner,prompt_owner,prompt_t`
  (`prompt_t`: the tick of that prompt sample, Task 4). `rank` is the
  first index after `order_native_use_hits`; `los_called`/`los_blocked` count this cycle's stage-0 3b7b90 calls on that owner (callers 3b8b05
  and 3b87b5) and blocked raw results; `set_count` is the number of distinct owners. `native-loot-reach-owners.csv` lists each owner once
  with its position and model bounds. Run `linux/d2vr-linux sim --scenario loot_reach --knob loot-reach-trace --knob sim-invulnerable
  --lead 45 --seconds 120` (`loot_reach_floor` / `loot_reach_drawer` run the second / third case alone: the three cases overflow the row cap), then
  `tools/linux/bonelab_run_check.py RUN --scenario loot_reach --coin X Y Z --case table|floor|drawer`. Spots and results:
  `.superpowers/sdd/2026-10-25-small-objects-fingers/g1-measure.md`. The checker also judges the prompt against the take (Task 4, ruling
  H14): `prompt lies` (a loot prompt sample that no hand's latest cycle before it selected) and, per grip-Use press in the case window (lock
  starts in `native-interaction-targets.csv`), the owner that left every set within 500 ms must equal the prompt at the press and must have
  had a clear LOS on some frame. Task 4 adds `native-loot-los.csv` under the same knob: one row per stage-0 3b7b90 LOS call on a takeable set
  owner with its origin, end point, sweep shape and the cast's hits in the order 3b7b90 walks them, the blocking hit first
  (`docs/native-use-los-3b7b90.md`); it installs a hook on the collector sort 0xc85470 only while the knob is on. `/home` was full during that work: `--out-root` on another disk keeps
  the snapshots off it.

## Keys
`python tools/game_keys.py 0x0D 0x1B` sends Enter, Esc to the game window; `--hold-focus` keeps it focused
(the game pauses when it loses focus).

## Live state
`artifacts/xr-live-status.json`: `gameplay`, `logic_mask` (2 gameplay, 4 pause menu, 256 wheel, 0 loading),
`published_move` (stick handed to the game), `hud_window_layers`, `hud_panel_children`, `hud_relayouts`.
`python tools/player_states.py --pid P --watch 10` prints player state changes (swimming, mantle, takedowns).

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

## Keys
`python tools/game_keys.py 0x0D 0x1B` sends Enter, Esc to the game window; `--hold-focus` keeps it focused
(the game pauses when it loses focus).

## Live state
`artifacts/xr-live-status.json`: `gameplay`, `logic_mask` (2 gameplay, 4 pause menu, 256 wheel, 0 loading),
`published_move` (stick handed to the game), `hud_window_layers`, `hud_panel_children`, `hud_relayouts`.
`python tools/player_states.py --pid P --watch 10` prints player state changes (swimming, mantle, takedowns).

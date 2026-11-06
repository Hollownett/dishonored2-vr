# VR v1047 (test candidate, 2026-11-06)

The user's v1045 headset report:
- The Heart "quite often just picking up randomly when I squeeze my hand ... it should behave as a normal weapon, same as gun or crossbow do now".
- The VR Settings page redrawing when the anti-aliasing switches between FSR/DLSS and other modes is a bad experience.
- Every diagnostic and data collection must be optional and off by default, "so the actual player and user don't waste any disk space".

Four helpers worked on this; their patches are in the scratchpad and were integrated here. v1046 was the Heart and page test build, and v1047 adds the diagnostics switch.

## 1. The Heart is held like a gun (dual wield)

Root causes, from the user's journal (`dual-wield-events.jsonl`):
- **One shared hold flag.** Both arms shared a single hold flag (`heart_held`). After a wheel pick moved slot 1 to the other arm, the old hand's squeeze still counted as "held", so the new hand's open grip dropped the Heart 31 ms after it arrived.
- **Stale catch position.** A squeeze near the dropped Heart was tested against a stale position: the pickup's last commanded spot, not where the engine's physics had moved it.
- **Equip regardless of the take.** The catch queued the take and equipped the Heart by name whether or not the take found anything.
- **Wrong hand.** When the per-arm model refused the catch, the Heart was equipped into whichever arm held slot 1, so it appeared in the other hand.

Now:
- `HeartGrip` (`vr_controls.hpp`) holds the Heart per arm, with the gun rules:
  - A Heart that arrives in a hand stays there until that hand has squeezed it once.
  - It is held while the hand squeezes.
  - Let go at the hip, it is put away and no pickup is spawned.
  - Let go anywhere else, it is dropped and falls as its world pickup.
  - The state starts over on every arrival and arm change.
- **Taking it back:** squeeze an empty hand within 45 cm of where the Heart really is, as with a dropped gun. The position is live (entity+0x300) for every tracked pickup. The Heart is equipped only after the game thread removed the pickup (`take_ticket`/`take_result`). It lands in the hand that squeezed: the other hand's gun becomes its copy and a power is put away, the same newest-wins rule as a wheel choice. Grenades and mines use this same catch path.
- **Journal rows:** `heart_hold`, `heart_release` (hip, `hip_mm` = hand to holster distance, dropped) and `heart_take` (ok / none / refused), next to `heart_on`.
- **Sim (v1046, the Heart in the left hand):**
  - Held, then let go in front: dropped.
  - The right hand, squeezing 10 cm away, caught it 31 ms later, into the right hand. No drop when slot 1 moved arms.
  - After it landed, both hands squeezing where it was let go took nothing.
  - Counters: dropped 3, taken 2, none 0.
  - Not shown in the sim: the hip put-away. The sim's hand offsets no longer reach the hip zone (a pistol let go there dropped too), while the headset reaches it (22 gun holsters in the user's journal).

## 2. VR Settings: no redraw when the AA mode changes

- Row 2 is always the "Upscaler scale" slider: same label, range and widget.
- With a non-upscaler AA mode it shows 100% and is locked: the game's own `locked` state for unavailable rows (row-level `SettingDefinition.locked` plus the slider's own), greyed and not adjustable.
- A mode change is written in place (value plus the locked flag), so the page is never rebuilt and the highlighted row stays.
- The SMAA toggle left the page. SMAA, Off and the SSAA presets are entries of the Anti-aliasing list, as before, and the settings file is unchanged (version 11).
- Not seen live: the locked look and the in-place lock change come from the game's UI code (`Slider.applyLockedState`, `OptionsListItem.applyLockedState`), not from a capture.

## 3. Diagnostics are optional and off by default

- **One switch** (`src/diagnostics.hpp`, `d2vr::diag`) covers every journal, trace, CSV, image capture, crash record and the full status file.
  - It is the launcher feature "Save diagnostic logs (for bug reports)" (`build\diagnostics`), off by default.
  - It replaces "Record frame timing and controller diagnostics". That option's `build\no-diagnostics` is ignored now, so every install defaults to off.
- **With it off the mod writes only:**
  - a ~100-byte `xr-live-status.json` (`gameplay`, `logic_mask`, `game_layers_submitted`), read by the launcher's status line and the game-folder proxy;
  - `aa-gpu-caps.txt`;
  - the answer to a launcher Tools command;
  - the probe's result and failure note;
  - the settings.
- **Off also skips the work:** no GPU readback for captures, no log strings built, no crash handler installed. The launcher no longer re-archives old outputs into `artifacts\runs\` either.
- **Before:** one v1045 session wrote 109 files / 619 MB with the old option off, and the user's artifacts folder held 2.4 GB.
- **Old logs:** the launcher's Tools page has a "Delete logs" button (`launcher/LogCleanup.cs`). It deletes the loose log files in `artifacts\` and the `runs\` and `ui-dump\` archives, after a Yes/No dialog that shows the count and size, and never while the game runs. It keeps the candidate manifests, the GPU caps, the live status, the command files, the proxy log, the probe launch and result files, and every other folder (`game-art`, `installed-backup`, `ngx`). Tests: 5 `log_cleanup_*` checks in `tests/launcher_features`; all 183 launcher checks pass.
- **For a bug report:** turn the switch on before the session; everything is recorded as before.

## Evidence

- **DLL:** `build\compile-v1047.cmd`, dll_exit=0, 9 compiler warnings (as v1045), 15,036,416 bytes, sha256 `7bbebd3a325dd301b5921718b2a973ac9347ff6d31ecdcbd50b0b2b965ddf9fa`.
- **Tests:** see `artifacts/repair-v1047-test-candidate.json`.
  - New: `heart_grip_check`, `diagnostics_check`, `diagnostics_routing_check.py`.
  - The python routing set has no new failure against the pre-change tree.
- **Static review:** every throw on an unopened diagnostic stream was checked. Two would have fired with diagnostics off and are fixed: the GPU dispatch change record and the settled scene record, which runs in every headset session.

## Not verified

- No headset run of v1046/v1047.
- Not run live on v1047, because the remaining sims were skipped at the user's request:
  - a session with diagnostics off (the new default);
  - the locked settings row and its in-place switch;
  - the Heart hip put-away.

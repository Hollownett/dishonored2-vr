# Why VR starts 45 s after launch, and what is safe earlier

Plan C Task 5 (R2). Date 2026-10-21. Branch feat/bonelab-feel. Installed mod DLL `d2vr_render_probe_arm_v600.dll`
(sha256 ff0daf65...bc927), Linux proxy `dinput8.dll` in the game folder. Exe pinned `b671f824...`.

## Why the delay exists

| Claim | Source | Status |
|---|---|---|
| The Windows launcher waits a fixed 45 s after it sees the process, "for the title screen" | `launcher/Probe.cs` `Task.Delay(45000)` in `Launch` (added in `d2043a2`, 2026-10-13, v399, unchanged since) | confirmed by code; no commit message gives a measured reason |
| The Linux proxy copies it: `kDefaultSettleSeconds = 45`, counted from "game window found" | `linux/proxy/proxy_core.hpp:25`, `linux/proxy/dinput8_proxy.cpp` (settle loop after `game window found`), added in `3667925` | confirmed by code |
| Inject at the title so that UI movies loaded with the level get the owned-load patches | `tools/vr_selftest.py:950` (`--inject-at-title`) | asserted in help text. Consistent with the code: the movie patches (`src/controller_glyph_atlas.hpp`, `src/menu_patches.hpp`) are applied only from the load callback, so a movie loaded before the mod is in stays unpatched. This only requires injecting before the Continue load, not 45 s |
| "VR does not start during the logos or loading screens" | `linux/README.md:99` | only asserted. Measured below: it is true only for starts in roughly the first 12 s |
| `StartProbe` needs a windowed swapchain and "retries forever" otherwise | `linux/proxy/proxy_core.hpp:146` comment; real check `src/dxgi_probe.cpp:6338` (throws "requires a windowed game swapchain") | confirmed by code; the proxy writes `r_fullscreen "0"` before launch so the swapchain is windowed from its first creation |
| The 45 s is about the title screen being up | measured below: the menu (logic_mask 64) first appears 36-40 s after process start, so 45 s from the window leaves about 7 s of margin | confirmed by measurement |

## What StartProbe needs (src/dxgi_probe.cpp, StartProbe at 7896)

- Preconditions: not already started, receipt/equip hook owners ready (7897-7898); `<root>\.git` exists, else returns 3 (7933).
- The Present hook target comes from a hidden dummy window, device and swapchain that `find_present` (7857) creates itself and reads vtable slots 8 (Present) and 13 (ResizeBuffers) from. It does not wait for the game's device or window. Those are system dxgi addresses, valid at process start.
- Byte checks on the game image (`image_base` + RVA, about 60 uses before the end of StartProbe, camera identity at about 8260, output-size getters at about 8310, the `install(image_base, ...)` calls at 8391-8674) run inside StartProbe, before the first Present the game makes. The image is loaded and the window exists at process start + 1.5 s, and all sim starts from settle 0 passed them (StartProbe ran, 25,000 layers submitted).
- ResizeBuffers is hooked at 8296-8307 (only with `--paired`) and verified.

Answers:

- (a) No hook target is invalid at process start but valid at the first Present: the Present target is a dummy-device vtable slot, the engine byte checks passed at settle 0. Nothing in the code needs the title screen.
- (b) Nothing assumes the title UI movies are not yet loaded. The reverse holds: starting earlier patches more movies (the status of the settle-0 run shows `native_art_movies.patched` 20, `rejected` 0).
- (c) The hook on a swapchain re-creation: `resize_buffers_hook` (1360) and the headset-size restore check (6181, `headset_size_restored`); `xr_resolution_changes` (6843) counts XR size changes. The runs below did not exercise a mode switch because `r_fullscreen "0"` is in the config from the start.

So Step 2 finds no code dependency on the title. Step 3 finds a behavioural one.

## Experiment (Linux sim rig, installed DLL, no headset)

Command: `d2vr-linux sim --settle S --lead 28 --seconds 120 --scenario idle` (autocontinue delay 10 s, except run 3). Process start is the first `proxy` log line; the window is found 1.5 s later in every run (2.0 s once).

| # | `--settle` | StartProbe begin after process start | menu (mask 64) first seen after process start | result |
|---|---|---|---|---|
| 1 | 0 | 1.5 s | 36.1-40.2 s | FAILED, StartProbe returned 4, gameplay false, no crash |
| 2 | 8 | 9.5 s | 36.1-40.2 s | FAILED, same |
| 3 | 8, Enter delayed to 40 s | 9.5 s | Enter 1 at 49.7 s already at mask 64 | FAILED, same: 12 Enters at the menu, none worked |
| 4 | 45 (control) | 46.6 s | before the first Enter at 56.7 s | OK, gameplay true |
| 5 | 30 | 31.6 s | 41-45 s | OK |
| 6 | 20 | 21.5 s | 39.2-43.3 s | OK |
| 7 | 14 | 15.5 s | 36.2-40.3 s | OK |
| 8 | 11 | 13.0 s | 35.4-39.5 s | FAILED, same |

Failure signature: the session runs normally (25k layers, the mod is alive), the menu comes up, and 12 posted Enters never reach gameplay (`logic_mask` stays 64). Run 3 shows it is not the Enter timing: the Enters came only at the menu and still failed. This is the "StartProbe returned 4, logic_mask 64" stall named as a known flake. Every start earlier than about 13 s after process start failed (4 of 4), every start from 15.5 s on succeeded (4 of 4). With 8 runs this is a boundary, not proof; a flake that happens to line up cannot be excluded, and it would show as a failure at a later settle. Cause of the early failure is not identified.

Stalls therefore correlate with starting the mod during the first ~12 s (engine init, before logos play), not with logos in general: settle 14 and 20 started during the logo movies (mask 0) and worked.

## Flat phase measured

- (a) process start to window found: 1.5 s (2.0 s once).
- (b) window found to first Present: not measured. The proxy log has no such line and no scratch build was made; `StartProbe` does not wait for it. Named probe for Task 6 below.
- (c) logos to title to menu: logic_mask 0 until 35.4-40.3 s after process start, 64 (menu) from then on; Continue then loads gameplay in about 10-20 s. The flat phase a user sees today is therefore about 47 s of waiting plus about 14 s of loading, with the title ready after about 38 s.

## Decision

**P3, with a fixed floor instead of a readiness poll: start at window found + 20 s (about 21.5 s after process start) instead of 45 s.** Reason: Step 2 finds no code dependency on the title, but Step 3 shows starts in the first ~12 s stall at the menu, and no readiness signal (window, swapchain, 5 s) is known to separate 11 s from 14 s, so P1 (settle <= 8) is refuted by runs 1-3 and P2 (logos-ended detection) by run 6 and 7, which started inside the logos and worked. 20 s saves about 25 s, leaves a 6 s margin over the earliest success (14 s after the window) and was clean once, so a second clean run is the headset checklist item. It does not meet "VR immediately from the launcher"; that needs the Task 6 probe to explain the early failure.

**Windows launcher: W2** (dinput8 proxy beside `Dishonored2.exe`, loaded at process start, launcher writes only the start trigger). Reasons: the user approved a file in the game folder on 2026-10-21; it matches Linux, where the same loop and knob (`linux-settle-seconds`) already exist; the proxy waits on the window itself with no `Process.GetProcessesByName` polling and no fixed `Task.Delay(45000)`; and it can start at the window-plus-N s point exactly, while W1 can only start after the launcher notices the process (2 s poll) and must inject remotely. W1 stays as the fail-safe fallback (global constraint: any failure falls back to today's behaviour). Restore the old delay with `build\vr-start-delay-seconds` (Windows) / `build\linux-settle-seconds` (Linux).

## Task 6 runtime probes (not assumptions)

1. Scratch/permanent log line at the first game Present and at each `logic_mask` change with the time since process start, to measure (b) and see what changes at about 13 s.
2. A sim run of `--settle 12` and `--settle 13` repeated twice each, to separate a hard boundary from a flake.
3. At a failed early start, capture what the menu sees: does Enter reach the game window (`native_pad_window_gates` in `xr-live-status.json`), or is the title input state not initialised.
4. Headset check: a real controller press at the title after a 20 s start (Enter is only the sim's stand-in).

## Rig notes

Eight runs, installed DLL only (hash ff0daf65...bc927 verified after the last run; no knob or marker left, `build/` holds the pre-existing `fsr4.off`, `full-body`, `carry-trace`). Run logs: `~/d2vr-test-builds/sim-runs/20261021T233705`, `...T233939`, `...T234217`, `...T234451`, `...T234802`, `...T235105`, `...T235350`, `...T235632`.

## Results (plan C Task 6, 2026-10-22)

Task 6 implemented the gate (`linux/proxy/proxy_core.hpp` `start_gate`, commit d4fd87c) with the 20 s default and added permanent
probes to the proxy log: d3d11.dll and dxgi.dll load time, every `logic_mask` change, the first layer the hooked mod submitted, gameplay,
and on every posted Enter the menu input counters (`native_pad_window_gates`, `native_pad_poll_gates`, `native_joystick_wakeups`,
`native_menu_button_events`, `native_controller_reads`, each with the change since the previous Enter). All times are ms since the game process was created.
Five more sim runs with the new proxy (installed in the game folder; the old one is kept at
`/tmp/claude-1000/-home-hollownet/846b35a1-4067-4547-b512-e6ba817998ea/scratchpad/dinput8.orig.t6`), same command as above:

| run | `--settle` | StartProbe begin | menu (mask 64) | result |
|---|---|---|---|---|
| s12a | 12 | 14.1 s | 36.9 s | OK, gameplay at 52.2 s |
| s12b | 12 | 14.2 s | 37.2 s | FAILED (returned 4), mask stuck at 64, 8 more Enters ignored |
| s13a | 13 | 15.2 s | 36.8 s | OK, gameplay 53.3 s |
| s13b | 13 | 15.2 s | 38.2 s | OK, gameplay 53.2 s |
| default | 20 (new default) | 22.7 s | 37.2 s | OK, gameplay 55.0 s |

Timings (default run, others within 0.5 s): process start to window found 2.1 s (1.6 to 2.1 across runs); window found to "present seen"
(client area, two polls) 0.5 s, the polling granularity; start 20.0 s after that; first layer submitted with the mod hooked 1.75 s after
`StartProbe begin` in every run (this is the hook installation time, not the game's first Present); logos to menu: mask 0 until 36.8 to 38.2 s in
every run regardless of when the mod started; Enter to gameplay: 15 to 17 s. d3d11.dll and dxgi.dll are already loaded 0.09 to 0.18 s after process start (the game
imports them statically), so module presence says nothing about readiness. The first Present of the game itself is not observable from the proxy
and the DLL only sees Presents after its hook is in; "present seen" is the window-based stand-in the coordinator chose.

What the probes say about the early stall:

1. It is not deterministic. Same start time (settle 12, 14.1 and 14.2 s after process start), one OK and one stalled. Over both tasks: starts at or before
   14.2 s stalled 5 of 6 times (settle 0, 8, 8, 11 and 12 stalled; the other 12 worked), starts at 15.2 s or later worked 7 of 7 (13 x2, 14, 20 x2, 30, 45).
   The sample cannot show that the risk is exactly zero from 15 s.
2. The mod and the game both look healthy while it stalls: 25k layers submitted, the menu appears at the same 36.8 to 38.2 s as in a good run,
   `game_window_foreground` is true, the pad poll gates and controller reads advance at the same rate as in a good run (about 1,000 and 2,000 per 4 s at the menu),
   `native_pad_window_overrides` and `native_joystick_focus_overrides` are 0 in all runs.
3. The mod does not count the keyboard Enter at all (`native_menu_button_events` is 0 in good runs too), so the counters cannot show whether the game
   saw the key. The only difference found between stalled and good runs is `native_joystick_wakeups` at the end (stalled 1 or 2, good 4), which counts
   lease changes and may be an effect rather than a cause. The first Enter at the menu arrived 3.4 to 3.6 s after the menu appeared in both a good and a stalled run.
4. Not found: any log or status field that differs before the stall. The menu movies patched count and rejected count (20 patched, 0 rejected at the menu) are the same.

Cause: not identified. The correlation with a start before roughly 15 s after process start remains, so the 20 s default stays. Candidate cause, unproven:
StartProbe takes about 1.75 s to hook; started within a window around 12 to 14 s after process start it overlaps the game's own title/menu preparation, and
one of the hooks installed in that window (`native_pad_focus.install`, the movie-load patch callback) leaves the title menu not taking keyboard Enter.
No fix is described, because the evidence does not name the hook. The next experiment that would: repeat settle 8 to 12 with `--knob` files that switch the
optional hook groups off one at a time (pad focus first, since it is the only one that touches input), and compare stall counts, at least 4 runs per arm.

Not used: 2 of the 7 allowed runs. The legacy fallbacks were only checked in the Wine proxy check, not on the real game.

## Results: bisecting the early-start stall (Task 6b, 2026-10-22)

Method: early start (`--settle 8`, which stalled 5 of 6 times before), one optional hook group switched off per arm with `--knob`, stop an
arm at its first stall (a group that is the sole cause would give clean runs every time, so one stall refutes it). Groups with no knob were switched
off in a scratch DLL (a copy named like the installed v600, never committed; the source edit was reverted, the worktree is clean) that added
`scratch-no-pad`, `scratch-no-bridge`, `scratch-no-hud` and `scratch-no-worldui` checks around the matching `install` calls in StartProbe.
10 of the 20 allowed runs were used.

| arm (switched off) | how | runs | stalls |
|---|---|---|---|
| baseline, settle 12 (new proxy, Task 6) | none | 2 | 1 |
| movie patches and menu entry hook (`NativeMenuEntry`, the owned-load callback) | `no-menu-entry` | 1 | 1 |
| VR glyph replacement | `no-vr-glyphs` | 1 | 1 |
| native pad focus hook (`native_pad_focus`) | scratch `scratch-no-pad` | 1 | 1 |
| HUD group (wrist HUD, HUD child draw, HUD rect) | scratch `scratch-no-hud` | 1 | 1 |
| all of: pad focus, controller bridge, HUD group, world UI, menu entry, glyphs, `hud-window-off`, `no-hold-y-menu` | scratch + knobs | 1 | 1 |
| controllers off through the probe config (to switch pad focus off without a DLL) | override config | 3 | 3 invalid: 2 refused by StartProbe (turning and simulated input need controllers), 1 never left `logic_mask` 0, so not comparable |
| `--probe-mode paired` | flat paired stereo | 1 | game exited during the session (not comparable) |
| `--probe-mode single` | flat single eye | 1 | no `xr-live-status.json`, so no `logic_mask` to judge; "OK" only means StartProbe returned 0 (not comparable) |

Every comparable arm stalled the same way (menu reached at 36 to 37 s, Enter ignored, `logic_mask` stuck at 64). Even with every optional group off
the stall remains, so none of the optional groups is the cause, alone or together. One run of the all-off arm does not prove the
stall rate there is as high as the baseline's, only that it is not zero.

Cause: not identified. What the bisection rules out is the whole optional part of StartProbe. What is left is the code every session runs:
the dummy D3D11 device and swapchain that `find_present` creates (`src/dxgi_probe.cpp` 7857), MinHook initialisation and the Present and ResizeBuffers hooks,
the D3D11 context dispatch bindings (`src/gpu_dispatch_bindings.hpp`: MinHook batch hooks on viewport, render-target and draw calls of every context, applied with
`MH_ApplyQueued`, which suspends the game's other threads) and the simulated XR session. A start at 14 s or earlier overlaps their roughly 1.75 s of setup
(first hooked layer 1.75 s after `StartProbe begin` in every run) with something the game does at about 12 to 16 s after launch; the code does not say what.

Proposed next steps (not done, not shipped):
1. A scratch DLL that returns from StartProbe after `find_present` only (dummy device created and destroyed, nothing hooked). If early starts still stall, the cause is the dummy
   device or hidden window, and the fix is to read the Present vtable slot from the game's own swapchain at its first Present instead of creating a device.
2. A scratch DLL that installs the Present hook but never binds the context dispatch hooks (`gpu_bindings.bind`). If clean, defer the first bind until `logic_mask` first reaches 64
   (the menu), keeping every feature, since the menu is the first time the redirected render targets matter.
3. Whichever is clean, repeat at settle 8 four times, then twice more to confirm.
The defence in depth for now is unchanged: the 20 s default start, which was clean 7 of 7.

Incidents: one run (no-vr-glyphs) ended with the rig unable to restore a save slot file that the game removed while closing; `d2vr-linux sim --restore`
restored it and verified. After every run the installed DLL hash was ff0daf65...bc927 and `build/` held only its own files.

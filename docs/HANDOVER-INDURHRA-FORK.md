# Handover: Indurhra's Dishonored 2 VR fork (2026-11-22)

Read this before working on the fork. It records what the fork adds on top of Hollow's mod, how each update was made, and the
standing rule for future updates.

## The standing rule: our changes always go on top

**Every time the mod is updated to Hollow's newest release, start from his `main` and replay our commits on top, so ours are the
newest layer. Where his code and ours disagree, ours wins, no matter what** (user, 2026-11-22: "mine should be on top no matter
what"; "every change we've made, put over the top. performance changes and afw"). Never merge his work over ours or drop ours. Where
he removed something of ours, restore it. Where he fixed the same thing differently, use ours (his additions that do not touch our
behaviour, such as new features and presets, stay). Author replayed commits as `Indurhra <Indurhra@users.noreply.github.com>`. Do not push without the user's go-ahead.

## Repositories and folders

| What | Where |
|---|---|
| Hollow's mod (upstream) | `https://github.com/Hollownett/dishonored2-vr`, git remote `upstream`. The user is not a collaborator there (checked 2026-11-22). |
| Our fork | `https://github.com/Indurhra/dishonored2-vr`, git remote `origin` |
| Working copy | `Q:\Modding Community\dishonored2-vr-main` (a worktree of `Q:\Modding Community\dishonored2-vr`) |
| Current branch | `update/v1457-ours` = Hollow's v1457 (`4685650`, his newest release and `main` on 2026-11-22) + our commits; installed as v1459 |
| Installed mod | `%LOCALAPPDATA%\DishonoredVR\build\` holds exactly one `d2vr_render_probe_arm_v*.dll` (the game folder's `dinput8.dll` proxy loads it) |
| Backups | `%LOCALAPPDATA%\DishonoredVR\backup\` (every replaced DLL, the original intro videos) |
| DLSS runtime | `%LOCALAPPDATA%\DishonoredVR\nvngx_dlss.dll` (NVIDIA 310.9.16, signed; copied from the user's own files, never committed) |

## What the fork adds (our commits on top of v1457)

| Commit | What | Relation to Hollow's line |
|---|---|---|
| `c7b747f` | `tools/sample_threads.py`: works without pefile; attributes samples to the calling module (`any` = any module's code) | He made it pefile-free in v1394; ours is the superset (his `pe_exports` name still works) |
| `7e4e2ac` | `read_self` region cache: 4-way set-associative sets on top of his v1394 recent-region list and TTLs; `build/region-cache-stats` diagnostic | He fixed big-region misses; ours fixes two live regions colliding in one slot (91% of misses in our profile) |
| `6548811` | `build/compile-census` diagnostic: counts every d3dcompiler compile/reflect call by caller | Ours only |
| `d15c513` | **AFW renderer restored**: Renderer row 2 = AFW (alternate frame warping), VR Stereo Hub's open-source held-eye rebuild (`third_party/vsh_afw`, zlib), tuned for D2 (rig mask, player-model depth classes x5 arms / x9 held items) | He removed AFW in v1443 believing it was the PureDark bridge; it is not |
| `87ac9ea`, `89fb98d` | `docs/PERFORMANCE-PORT-2026-11-21.md`: the Cosmic Tangerine performance review, measurements, DLSS results | Ours only |
| `d76e647` | NGX's own verbose log with diagnostics (`artifacts/ngx/ngx-log.txt`) | Ours only |
| `b63b9ca` | **AFW wins over Spacewarp**: with Spacewarp on and the saved Renderer = AFW, AFW starts and Spacewarp sits the session out (still one frame generation at a time) | His rule (Spacewarp holds native) still applies to Renderer 1 / the launcher's performance mode |
| `9f1afe3` | DLSS features always created with output subrects (our fix) | He fixed the same bug for his foveated box only (`desc.output_subrects`); ours replaces it |

Already in Hollow's line (merged by him in v1411, credited in his v1445/v1457 notes): the ImGui VR menu, PSVR2 adaptive triggers on
both hands, the settings-quad fix. They are not replayed.

Not in either line (installed separately in the game folder): the intro skip (`base\video\{arkbeth,legal,void,warning}.bk2`, originals
in `backup\intro-videos-original\`).

## How each update was done

1. **v1297 -> v1392 (2026-11-21, branch `update/v1392-ours`, installed as v1393).** Cherry-picked our 10 commits onto `upstream/main`.
   Two one-line conflicts (a status line, the test build list) were merged token by token keeping both sides.
2. **v1392 -> v1457 (2026-11-22, branch `update/v1457-ours`, built as v1458).**
   - Hollow had already merged our first three commits (ImGui, PSVR2, settings quad): skipped.
   - Sampler and region cache: both sides had independent fixes; combined as described above (his recent list + our ways).
   - AFW: v1443's removal was reversed for `src/` and `tools/build-dll-fast.cmd` only (not his launcher), then our VR Stereo Hub
     port applied on top in one commit, so no intermediate state carries PureDark.
   - DLSS: first kept his per-feature `InEnableOutputSubrects` plus our NGX logging; then, at the user's instruction that ours wins
     no matter what, our always-on version replaced his (`9f1afe3`), and AFW was given precedence over Spacewarp (`b63b9ca`).

### Procedure for the next update

```
git fetch upstream
git switch -c update/v<NEW>-ours upstream/main
git cherry-pick <our commits from the last update/v*-ours branch, oldest first>   # resolve: theirs where equivalent, ours on top
tools\build-dll-fast.cmd d2vr_render_probe_arm_v<NEW+1>.dll
```

Then run the unit tests (`native_readable_range_check`, `dlss_lifecycle_check`, `vr_settings_check`), the simulator checks below,
back up the installed DLL, install, and leave pushing to the user.

## Checks used (simulator)

- `tools/vr_selftest.py` through the scratchpad wrapper: scenarios `idle`, `swing`, `guidepages`, `pistol`, `shoulderthrow`; AFW via
  the `build/afw` knob; DLSS via `build/aa-mode` (8 = DLSS Quality); look for `alternate on`, `afw ... refused 0`, DLSS `state 2` with
  0 `evaluate_failures`.
- Throughput: the simulator never blocks in `xrWaitFrame`; the engine limiter (`com_timeScaler` 3 = 90 fps in the user's config) caps
  it. Lift it in memory only with the test command `cvar com_timeScaler 10`, and check the config file afterwards. Since 2026-11-21
  something outside the mod also caps at 144 fps, which hides differences above it.
- The harness writes `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` into the user's
  `Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg`; remove those two lines after testing.
- Known harness noise (also on older builds): "injection" FAIL lines (cleanup timeout / diagnostic code 4) and swing's HUD edge
  coverage 0.022-0.023.

## v1458/v1459 results (simulator; not headset-tested)

- AFW: renderer live 2, 0 refusals and 0 fallbacks in idle, swing, menu pages, pistol and shoulder throw.
- DLSS Quality with AFW: active, 0 evaluate failures (his subrect fix covers our path).
- Unit tests: region cache, DLSS lifecycle, VR settings pass.
- Region cache on top of v1394 (native stereo, steady gameplay, 90 cap, interleaved, two pairs): VirtualQuery 0.22 / 0.28 ms per
  frame on plain v1457 vs 0.00 / 0.12 ms on v1458; main-thread busy time 5.28 / 5.43 vs 5.23 / 5.14 ms (about 0.17 ms, 3%, less).
  Small but consistent, so the layer is kept. Uncapped throughput could not separate them (both at the external 144 cap).
- Spacewarp: it never engages in the simulator here (the game keeps up at 90, so the pacer stays at full rate); its frame
  generation is untested by us.

## Things the user should know

- **AFW and Spacewarp do not run together, and AFW wins.** With AFW as the saved renderer, AFW runs and Spacewarp is off for that
  session even if the launcher has it on (the VR menu's Renderer help says so). To use Spacewarp instead, pick Native stereo in the
  VR menu (or the launcher's performance mode off) and keep Spacewarp on.
- AFW saves GPU time (one eye rendered per frame); Spacewarp saves CPU time (the game renders at half rate and the mod generates the
  frames in between).
- The user's AA setting is FSR 4 Quality, which runs as FSR 3 Quality on their NVIDIA GPU. DLSS Quality / DLAA are available.

## Rollback

Swap the DLL in `%LOCALAPPDATA%\DishonoredVR\build\` for the newest file in `backup\` (rename it to its `d2vr_render_probe_arm_v*.dll`
name) and keep only one mod DLL in that folder.

# Review fixes (branch `fix/review-2026-10-17`) — test results

Fill in the "Result" column during one Windows + headset session, then commit this file.

## Automated

| Check | Where | Result |
|---|---|---|
| Linux harness `python3 tools/linux/run_build_cmd.py --keep-going` (clang-cl + xwin + Wine, real sources) | Linux | 468 passed / 0 failed / 41 skipped (Wine-only skips, reasons in `tools/linux/run_build_cmd.py` SKIPS) — 2026-10-18 |
| `g++ -std=c++17 -Isrc tests/vr_perf_governor_check.cpp` (Task 7) | Linux | ok — 2026-10-18 |
| `g++ -std=c++17 -Isrc tests/physical_grab_check.cpp` (Task 8) | Linux | ok — 2026-10-18 |
| Launcher sources compile (net8.0-windows, warnings as errors; scratch project without embedded payload) | Linux | 0 warnings / 0 errors — 2026-10-18 |
| `tools\build-msvc.cmd` → exit 0 | **Windows** | |
| `tools\build.ps1` (DLL and source archive; does not build the launcher) → exit 0 | **Windows** | |
| `dotnet build launcher -c Release` → 0 warnings / 0 errors | **Windows** | |

## Headset / manual (Windows, one session)

| # | Task | Steps | Expected | Result |
|---|---|---|---|---|
| 1 | 2 — SEH guards | Grip a physics prop, then Pause › Load; die while the crosshair is visible and Restart Checkpoint (3× each). | No crash; `artifacts\xr-live-status.json` grab `faults` ≥ 0; game continues. | |
| 2 | 4 — sword flight | Throw the sword off a balcony edge into a void area. | It stops and lies at its last valid position within ~6 s; game keeps running. | |
| 3 | 5 — turning lock | Snap-turn 20× and smooth-turn 10 s while walking; compare `frame-timing` missed-slot % with a v59x run in the same spot. | Turning feels identical; missed slots ≤ baseline. | |
| 4 | 6 — diagnostics | During VR run `powershell -c "$f=[IO.File]::Open('artifacts\xr-live-status.json','Open','Read','None'); Start-Sleep 20; $f.Close()"`. Then stop and restart VR twice from the launcher while in a level. | VR keeps running, no recovery in `xr-recoveries.log`; JSON afterwards shows `"xr_status_write_failed":true`; no crash on restarts. | |
| 5 | 7 — resolution governor | Stand in a heavy scene (Karnaca harbour) 2 min with dynamic resolution on. | ≤ 3 changes in `perf_governor.changes`; no repeating half-second freezes. | |
| 6 | 8 — throw velocity | Throw a bottle with the same arm motion 10× (before/after if a v59x build is at hand); also carry an object while moving your hand slowly. | Throws travel noticeably further and feel consistent; carried objects follow the hand smoothly (no judder). | |
| 7 | 9 — launcher build check | Point the launcher at a copy of the game folder with one byte of `Dishonored2.exe` changed; then at the real install. | Copy: "Dishonored 2 (supported build)" red, LAUNCH VR disabled, no game start. Real install: row green. | |

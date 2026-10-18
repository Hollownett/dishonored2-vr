# Linux / Proton Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the unchanged Windows mod DLL inside Dishonored 2 under Proton on Linux, with SteamVR + Steam Link, through a `dinput8.dll` proxy and a Python CLI. Every new file goes under `linux/`.

**Architecture:** A C++17 `dinput8.dll` proxy sits next to `Dishonored2.exe`. It forwards `DirectInput8Create` to the system dinput8 and starts one worker thread. That thread does in-process what `launcher/Probe.cs` `Session` does across processes. It waits for the game window and a start trigger, writes `build\dxgi-probe.cfg` with its own PID, loads the mod DLL from the mod home inside the Proton prefix, checks `ProbeStatus` and `ProbeCapabilities`, and calls `StartProbe` on that thread. A stdlib-only Python CLI (`linux/d2vr-linux` → package `linux/d2vr_linux/`) finds the game and prefix from Steam's VDF files, lays out the mod home, installs or removes the proxy, checks the host and the prefix, prints the Steam launch options, and writes the start and stop request files.

**Tech Stack:** C++17 built with clang-cl + xwin through `tools/linux/run_build_cmd.py`'s `cl_to_clang` (MSVC `cl` also works). Tests run under Wine 11 using the harness prefix. Python 3.10+ (stdlib only) with `unittest`.

**Spec:** `docs/superpowers/specs/2026-10-18-linux-proton-design.md`. Research: `docs/superpowers/specs/2026-10-18-linux-proton-research.md`.

## Spec deviations

The code showed gaps or defects in the spec. The plan departs from the spec as follows:

1. **The stop request file does not exist yet.** The spec says `stop` writes "the stop request file the DLL already watches". The DLL watches no file. Stop is only the `StopProbe` export (`src/dxgi_probe.cpp:7120`, called by `Probe.Stop`, `launcher/Probe.cs:208-213`). The proxy adds a watcher thread that polls `<modhome>\build\linux-stop` and calls `StopProbe`. It has to be a separate thread because `StartProbe` blocks the worker.
2. **The capability check is added.** `Probe.cs:115,267` refuses to start unless `(ProbeCapabilities() & 61017) == 61017`. The spec checks only `ProbeStatus`. The proxy does both checks.
3. **The OpenXR loader needs a source.** `third_party/openxr/bin/openxr_loader.dll` is gitignored (`.gitignore: third_party/**/*.dll`) and is missing from this checkout. The v562 release zip does not contain it either; the zip holds only the DLL, the manifest and `THIRD_PARTY_NOTICES.md`. `install` therefore takes `--openxr-zip` or downloads the Khronos zip from `third_party/openxr/bin/source.json`. It verifies `zip_sha256`, extracts `zip_member`, and verifies `dll_sha256`.
4. **Release zip members use backslashes.** In the real `d2vr-v562-dll.zip`, members are named like `d2vr-v562\d2vr_render_probe_arm_v562.dll`. `install` matches members by basename after splitting on both `/` and `\`. It verifies the manifest the way `Probe.Launch` does (`Probe.cs:61-70`): `launch_blocked`, the DLL name regex, `sha256`, and `game_sha256`.
5. **The proxy needs a way to know which `<dll>` to load.** The spec does not say. The proxy loads the single `d2vr_render_probe_arm_v<N>.dll` in `<modhome>\build\` and refuses if there are none or several. `install` deletes older versions, as `ModHome.Install` does (`launcher/ModHome.cs:45-47`).
6. **The focus cvars are added.** Before every launch, the Windows launcher appends `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` to `dishonored2Config.cfg` (`Probe.cs:75`, `GameKeys.PrepareConfig` `:546-558`, "the game drops non-archived lines on exit"). The spec leaves this out. Linux has no pre-launch hook, so the proxy worker appends the lines as its first action (best effort, logged).
7. **Start trigger rules.** At attach, the proxy deletes any `linux-start` or `linux-stop` left from an earlier run. The settle timeout only auto-starts the first session. Later sessions need an explicit `start`. The default is 45 s (`Probe.cs:83`). An optional knob file `build\linux-settle-seconds` overrides it, so the test can run in seconds. The worker also deletes a start trigger that was written while a session was running.
8. **"Present in prefix" is not enough.** Proton puts builtin stubs in the prefix. Every one of `msvcp140*.dll`, `vcruntime140*.dll` and `d3dcompiler_47.dll` in this machine's `compatdata/403640/pfx/drive_c/windows/system32` carries the `Wine builtin DLL` marker. `doctor` therefore requires native files: a regular file without that marker.
9. **"Offer to create" `active_runtime.json` becomes a flag.** It is the explicit `doctor --fix-openxr` flag rather than an interactive prompt, so it can be tested. It never overwrites an existing file.
10. **The NVIDIA check uses /proc.** `vulkaninfo` is not installed. The "NVIDIA Vulkan device" check uses `nvidia_icd.x86_64.json` plus the `Model:` line in `/proc/driver/nvidia/gpus/*/information`. That line is also the source of `DXVK_FILTER_DEVICE_NAME`.
11. **Install protects a foreign proxy.** The proxy binary embeds `D2VR-LINUX-PROXY-1`. `install` refuses to overwrite a `dinput8.dll` without that marker, and `uninstall` leaves such a file alone. The spec says nothing on this.
12. **Layout and test files.** The CLI is a package plus a thin executable wrapper. The tests are split per module instead of the single `test_d2vr_linux.py`. `start`, `stop` and `doctor` accept `--mod-home`, and every command accepts `--steam-root`. The proxy check uses these overrides.
13. **The proxy clears stale outputs.** Before each `StartProbe`, the proxy deletes `artifacts\dxgi-probe-failure.txt`, `dxgi-probe-result.json` and `xr-live-status.json`, as `Probe.Session` does (`Probe.cs:247`). Otherwise the troubleshooting steps would read results from the previous run.

## Global Constraints

Copied verbatim from the spec:
- Lives on branch `linux/proton` (from `fix/review-2026-10-17`), in a new top-level `linux/` folder.
- No edits to `src/`, `launcher/`, `tools/build-msvc.cmd`. If a shared change is unavoidable it must be additive, behind a runtime Wine check, and called out for review.
- Gate: `python3 tools/linux/run_build_cmd.py` result identical before/after (same PASS/SKIP counts, 0 FAIL).

Further constraints:
- **File scope.** Every new file lives under `linux/`, plus at most `docs/`. Do not edit `tools/linux/` either. Import from it read-only (`sys.path.insert(0, "tools/linux"); import run_build_cmd as harness`).
- **Harness baseline** (latest full run): `468 passed / 0 failed / 41 skipped`.
- **CLI.** Python 3, stdlib only. Executable is `linux/d2vr-linux`.
- **Proxy build.** C++17, `/std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT`, built by clang-cl via the harness toolchain into `build/linux-proxy/` (gitignored). Any `warning:` fails the build. It must also build with MSVC `cl` using the same arguments.
- **Loader lock.** No work under loader lock. `DllMain` only stores the module handle and creates the worker thread. Nothing calls `LoadLibrary` or `StartProbe` from `DllMain`.
- **Fail safe.** The proxy must never break the vanilla game. If anything fails, it logs and idles. `DirectInput8Create` keeps forwarding.
- **Game pin.** Steam app `403640`, build `17576867`, `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180` (`Probe.cs:17`, `src/game_camera.hpp`).
- **Probe cfg.** The line is exactly `Probe.ProbeConfig` (`Probe.cs:122`): `"{pid} 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n"`, ASCII, with `pid = GetCurrentProcessId()`.
- **Mod home.** Default `C:\users\steamuser\AppData\Local\DishonoredVR`, which is `<pfx>/drive_c/users/steamuser/AppData/Local/DishonoredVR`. The ini is `<game dir>/d2vr-linux.ini`, one line. The proxy log is `<modhome>/artifacts/linux-proxy.log`.
- **Launch options.** The printed line is exactly `WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="<NVIDIA name>" __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%`.
- **Out of scope (v1).** FSR 4, Virtual Desktop, WiVRn/ALVR, flatpak Steam, and the WinForms launcher under Wine.
- **Tests.** Every automated test runs on this Fedora machine. The headset test is a manual user checklist (Task 7) and does not count as an automated test.
- **Shared Wine prefix.** `linux/tests/test_proxy_core.py` and `linux/tests/proxy_check/` use the harness Wine prefix (`harness.WINEPREFIX`). Never run them at the same time as `run_build_cmd.py`, because its exit runs `wineserver -k`.

## Review Focus

1. **Stale trigger.** A `linux-start` file is left over from the last session when the game starts. VR must not start during the logo or loading screens. The proxy deletes the file at attach and waits. Covered by Task 6 `test_stale_trigger_ignored_then_start_and_stop`.
2. **Foreign `dinput8.dll`.** The game folder already holds another mod's `dinput8.dll` (an ASI loader, for example). `install` refuses without touching it, and `uninstall` leaves it. Covered by Task 2 `test_install_refuses_foreign_dinput8` and `test_uninstall_keeps_foreign_dinput8`.
3. **Game updated by Steam.** The exe hash no longer matches the pin. `install` writes nothing at all, and `doctor` prints FAIL with the "Steam updated the game" fix. Covered by Task 2 `test_install_wrong_game_hash_writes_nothing` and Task 3 `test_doctor_game_hash_fail`.
4. **Zip as actually published, or tampered.** The zip has backslash member names and extra files, or its DLL bytes do not match the manifest `sha256`. `install` matches members by basename. On a mismatch it refuses, and neither the mod home nor the game folder changes. Covered by Task 2 `test_install_backslash_zip_layout` and `test_install_tampered_dll_writes_nothing`.
5. **Mod home holds zero or two mod DLLs.** The proxy logs `expected exactly one d2vr_render_probe_arm_v*.dll` and idles. The game keeps running, and `DirectInput8Create` still returns `DI_OK`. Covered by Task 5 `select_mod_dll` cases and Task 6 `test_no_mod_dll_keeps_game_running`.

---

## File structure

```
linux/
  d2vr-linux                     executable wrapper: sys.path += linux/; d2vr_linux.cli.main()
  d2vr_linux/__init__.py         empty
  d2vr_linux/steam.py            VDF/ACF parsing, library/game/prefix discovery, exe hash
  d2vr_linux/modhome.py          release zip + OpenXR loader verification, mod-home layout, game-dir proxy/ini, requests
  d2vr_linux/doctor.py           host/prefix checks, NVIDIA name, launch-options line
  d2vr_linux/cli.py              argparse front end
  proxy/proxy_core.hpp           pure helpers + shared constants (no <windows.h>-only logic)
  proxy/dinput8_proxy.cpp        DllMain, DirectInput8Create forward, worker + stop watcher
  proxy/dinput8.def              EXPORTS DirectInput8Create=proxy_DirectInput8Create
  proxy/build.py                 clang-cl build via the harness's cl_to_clang
  proxy/build.cmd                the proxy cl line for MSVC
  README.md                      user guide
  tests/fixtures/                libraryfolders_{linux,windows,old}.vdf, appmanifest_403640.acf, system.reg
  tests/fixtures.py              builds temp Steam/prefix/game trees from those files
  tests/wine_support.py          toolchain detection, wine env, Linux→Z: paths
  tests/test_steam.py  test_modhome.py  test_doctor.py  test_cli.py  test_proxy_core.py  test_proxy_build.py
  tests/proxy_check/host.cpp     stand-in game exe (static DINPUT8 import, "Dishonored" window)
  tests/proxy_check/fake_mod.cpp fake d2vr_render_probe_arm_v999.dll
  tests/proxy_check/proxy_core_check.cpp
  tests/proxy_check/run_proxy_check.py   Wine-hosted integration (unittest)
```

Run the Python unit tests with `python3 -m unittest discover -s linux/tests -v`. Each test file starts with `sys.path.insert(0, str(Path(__file__).resolve().parents[1]))` so that `import d2vr_linux` works.

---

### Task 1: Steam discovery (`steam.py`)

**Files:**
- Create: `linux/d2vr_linux/__init__.py`, `linux/d2vr_linux/steam.py`
- Create: `linux/tests/fixtures/libraryfolders_linux.vdf` (copy of this machine's two-library file, with apps trimmed to `403640` and `250820`, and with the second path changed to `/mnt/steam ssd/SteamLibrary` to include a space)
- Create: `linux/tests/fixtures/libraryfolders_windows.vdf` (`"path" "C:\\Program Files (x86)\\Steam"` plus `"D:\\SteamLibrary"`)
- Create: `linux/tests/fixtures/libraryfolders_old.vdf` (old format `"LibraryFolders" { "TimeNextStatsReport" "…" "1" "/games/lib" }`)
- Create: `linux/tests/fixtures/appmanifest_403640.acf` (the real one from `/mnt/steamssd/SteamLibrary/steamapps/`, verbatim)
- Create: `linux/tests/fixtures.py`, `linux/tests/test_steam.py`

**Interfaces:**
- Produces (in `d2vr_linux.steam`):
  - `GAME_APPID = "403640"`, `STEAMVR_APPID = "250820"`, `GAME_EXE = "Dishonored2.exe"`, `GAME_BUILD = "17576867"`, `GAME_SHA256 = "b671f824…16180"` (full value from Global Constraints)
  - `class SteamError(Exception)`
  - `parse_vdf(text: str) -> dict`: nested `dict[str, str | dict]`. Handles quoted tokens with `\\` and `\"` escapes, `{ }`, and `//` comments. Raises `ValueError` on an unterminated string or unbalanced braces.
  - `steam_roots(home: Path) -> list[Path]`: those of `home/.local/share/Steam`, `home/.steam/steam` and `home/.steam/root` that exist, resolved and deduplicated, in that order.
  - `library_paths(steam_root: Path) -> list[Path]`: `steam_root` first, then every library path in `steamapps/libraryfolders.vdf` (new `"N" { "path" … }` format and old `"N" "path"` format). Paths that are not POSIX-absolute (drive letter or backslash) are skipped. Deduplicated.
  - `find_game(libraries: list[Path]) -> tuple[Path, Path]`: returns `(library, game_dir)` from `steamapps/appmanifest_403640.acf` → `AppState.installdir` → `steamapps/common/<installdir>`, which must contain `Dishonored2.exe`. Raises `SteamError`.
  - `find_prefix(game_library: Path, libraries: list[Path]) -> Path`: the first `<lib>/steamapps/compatdata/403640/pfx` that has `drive_c/`, checking `game_library` first. Otherwise raises `SteamError("…run Dishonored 2 once with Proton so Steam creates its prefix…")`.
  - `sha256_file(path: Path) -> str`: lowercase hex, streamed in 1 MiB chunks.
  - `check_game_exe(game_dir: Path, expected: str = GAME_SHA256) -> str`: returns the hash. Raises `SteamError` whose message contains `Steam updated the game` on a mismatch.
  - `find_steamvr(libraries: list[Path]) -> Path | None`: `steamapps/common/SteamVR`, but only if `appmanifest_250820.acf` and `steamxr_linux64.json` both exist.
  - `@dataclass class Install: steam_root: Path; libraries: list[Path]; game_library: Path; game_dir: Path; prefix: Path`
  - `locate(home: Path, steam_root: Path | None = None) -> Install`: libraries are the union over `[steam_root]` or `steam_roots(home)`.
- Produces (in `linux/tests/fixtures.py`): `make_steam(tmp: Path, *, exe_bytes=b"MZfake", with_prefix=True, library_name="steam ssd") -> SimpleNamespace(home, steam_root, library, game_dir, prefix, exe_sha256)`. It writes a `~/.local/share/Steam` root whose `libraryfolders.vdf` points at `<tmp>/<library_name>/SteamLibrary`, holding the appmanifest, `common/Dishonored2/Dishonored2.exe`, and optionally `compatdata/403640/pfx/drive_c/windows/system32/` plus `system.reg`.

- [ ] **Step 1: Write the failing tests** in `linux/tests/test_steam.py`:

```python
def test_parse_vdf_linux_two_libraries(self):
    d = steam.parse_vdf(read_fixture("libraryfolders_linux.vdf"))
    self.assertEqual(d["libraryfolders"]["1"]["path"], "/mnt/steam ssd/SteamLibrary")
    self.assertEqual(d["libraryfolders"]["1"]["apps"]["403640"], "45263695341")
def test_parse_vdf_windows_paths_unescaped(self):
    d = steam.parse_vdf(read_fixture("libraryfolders_windows.vdf"))
    self.assertEqual(d["libraryfolders"]["0"]["path"], r"C:\Program Files (x86)\Steam")
def test_parse_vdf_unterminated_raises(self):
    with self.assertRaises(ValueError): steam.parse_vdf('"a" { "b" "c')
def test_library_paths_skips_windows_style(self):   # windows fixture copied into a temp root
    self.assertEqual(steam.library_paths(root), [root])
def test_library_paths_old_format(self):
    self.assertEqual(steam.library_paths(root), [root, Path("/games/lib")])
def test_acf_installdir_and_build(self):
    a = steam.parse_vdf(read_fixture("appmanifest_403640.acf"))["AppState"]
    self.assertEqual((a["installdir"], a["buildid"]), ("Dishonored2", "17576867"))
def test_locate_finds_game_and_prefix_in_second_library_with_space(self):
    f = make_steam(tmp); inst = steam.locate(f.home)
    self.assertEqual((inst.game_dir, inst.prefix), (f.game_dir, f.prefix))
def test_find_prefix_missing_explains(self):
    f = make_steam(tmp, with_prefix=False)
    with self.assertRaisesRegex(steam.SteamError, "run Dishonored 2 once with Proton"): steam.locate(f.home)
def test_find_game_missing_raises(self):   # appmanifest deleted
    with self.assertRaises(steam.SteamError): steam.locate(f.home)
def test_check_game_exe_match_and_mismatch(self):
    self.assertEqual(steam.check_game_exe(f.game_dir, f.exe_sha256), f.exe_sha256)
    with self.assertRaisesRegex(steam.SteamError, "Steam updated the game"): steam.check_game_exe(f.game_dir)
def test_game_pin_matches_windows_sources(self):
    self.assertIn(f'GameHash = "{steam.GAME_SHA256}"', (REPO / "launcher/Probe.cs").read_text())
    self.assertIn(steam.GAME_SHA256, (REPO / "src/game_camera.hpp").read_text())
```

- [ ] **Step 2: Run them and confirm they fail.** Run `python3 -m unittest linux/tests/test_steam.py -v`. Expected: ERROR, `ModuleNotFoundError: d2vr_linux`.
- [ ] **Step 3: Implement `steam.py` and `fixtures.py`** with the signatures listed above.
- [ ] **Step 4: Run them and confirm they pass.** Same command. Expected: `OK` (11 tests).
- [ ] **Step 5: Commit.** `git add linux/d2vr_linux linux/tests && git commit -m "linux: Steam library, game and Proton prefix discovery"`

---

### Task 2: Mod home and game-dir install / uninstall (`modhome.py`)

**Files:**
- Create: `linux/d2vr_linux/modhome.py`, `linux/tests/test_modhome.py`

**Interfaces:**
- Consumes: `steam.Install`, `steam.check_game_exe`, `steam.GAME_SHA256`, `steam.sha256_file`, `fixtures.make_steam`.
- Produces (in `d2vr_linux.modhome`):
  - Constants: `MOD_HOME_WIN = r"C:\users\steamuser\AppData\Local\DishonoredVR"`, `MOD_HOME_REL = Path("drive_c/users/steamuser/AppData/Local/DishonoredVR")`, `INI_NAME = "d2vr-linux.ini"`, `PROXY_NAME = "dinput8.dll"`, `PROXY_MARKER = b"D2VR-LINUX-PROXY-1"`, `START_TRIGGER = "linux-start"`, `STOP_REQUEST = "linux-stop"`, `DLL_RE = r"^d2vr_render_probe_arm_v[0-9]+\.dll$"`, `MANIFEST_RE = r"^repair-v[0-9]+-test-candidate\.json$"`, `REPO_ROOT = Path(__file__).resolve().parents[2]`, `OPENXR_SOURCE = REPO_ROOT / "third_party/openxr/bin/source.json"`, `DEFAULT_PROXY = REPO_ROOT / "build/linux-proxy/dinput8.dll"`
  - `class InstallError(Exception)`
  - `@dataclass class ModRelease: dll_name: str; dll: bytes; manifest_name: str; manifest: bytes; notices: bytes | None`
  - `read_release_zip(zip_path: Path, game_sha256: str = GAME_SHA256) -> ModRelease`. Matches members by basename, splitting on `/` and `\`. It needs exactly one member matching `MANIFEST_RE`. From the manifest it requires: `launch_blocked` not true, `dll_name` matching `DLL_RE`, a member with that basename, `sha256(dll) == manifest["sha256"]`, and `manifest["game_sha256"] == game_sha256`. Any failure raises `InstallError`.
  - `read_openxr_loader(source: dict, zip_path: Path) -> bytes`. Checks `zip_sha256`, reads member `source["zip_member"]`, then checks `dll_sha256`.
  - `fetch_openxr_zip(source: dict, cache_dir: Path, urlopen=urllib.request.urlopen) -> Path`. Uses `cache_dir/<basename of url>` if its hash matches. Otherwise it downloads to `.tmp`, verifies, and calls `os.replace`.
  - `is_our_proxy(path: Path) -> bool`: `PROXY_MARKER in path.read_bytes()`.
  - `mod_home_dir(prefix: Path) -> Path`: `prefix / MOD_HOME_REL`.
  - `install(inst: Install, zip_path: Path, proxy: Path, openxr_zip: Path | None, cache_dir: Path, game_sha256: str = GAME_SHA256, openxr_source: Path = OPENXR_SOURCE, urlopen=…) -> list[str]`. Returns the paths it changed, relative to the mod home and the game dir.
  - `uninstall(game_dir: Path) -> list[str]`
  - `write_request(mod_home: Path, name: str) -> Path`: writes `build/<name>` containing `str(int(time.time()))`, atomically.

**`install` order.** Every check runs before any write.
1. `check_game_exe(inst.game_dir, game_sha256)`.
2. `read_release_zip`.
3. Read the loader from `openxr_zip`, or from `fetch_openxr_zip` if none was given.
4. The proxy must exist and be ours (`is_our_proxy`). If not, raise `InstallError("…build it: python3 linux/proxy/build.py")`.
5. If `game_dir/dinput8.dll` exists and is not ours, raise `InstallError` with a message containing `another mod's dinput8.dll`.
6. Write the mod home:
   - `.git/` (an empty directory; `StartProbe` returns 3 without it, `dxgi_probe.cpp:7163`)
   - `build/<dll>`
   - `artifacts/<manifest>`
   - `THIRD_PARTY_NOTICES.md` (if present)
   - `third_party/openxr/bin/openxr_loader.dll`, plus copies of the repo's `LICENSE` and `source.json`
   - `build/fsr4.off` (empty)
   - Delete other `build/d2vr_render_probe_arm_v*.dll`.
7. Write the game dir: `dinput8.dll` and `d2vr-linux.ini` (`MOD_HOME_WIN + "\r\n"`).

Write each file to `<name>.tmp`, then `os.replace` it. Skip a file whose bytes are already identical (as `ModHome.Install`, `launcher/ModHome.cs:29`). Never touch `build/vr-settings.cfg`.

- [ ] **Step 1: Write the failing tests** in `linux/tests/test_modhome.py`. Helpers: `make_zip(tmp, backslash=True, tamper=False, blocked=False, game_sha=…)` builds `d2vr-v999\d2vr_render_probe_arm_v999.dll`, `d2vr-v999\repair-v999-test-candidate.json` and `d2vr-v999\THIRD_PARTY_NOTICES.md`. `make_openxr(tmp)` builds a fake Khronos zip and a matching `source.json` dict. The manifest's `game_sha256` defaults to the fixture exe hash. The fake proxy is `b"MZ…D2VR-LINUX-PROXY-1…"`.

```python
def test_install_backslash_zip_layout(self):
    changed = modhome.install(inst, z, proxy, oxr_zip, cache, game_sha256=f.exe_sha256, openxr_source=src)
    h = modhome.mod_home_dir(f.prefix)
    for rel in (".git", "build/d2vr_render_probe_arm_v999.dll", "artifacts/repair-v999-test-candidate.json",
                "third_party/openxr/bin/openxr_loader.dll", "build/fsr4.off", "THIRD_PARTY_NOTICES.md"):
        self.assertTrue((h / rel).exists(), rel)
    self.assertTrue((h / ".git").is_dir())
    self.assertEqual((f.game_dir / "d2vr-linux.ini").read_bytes(), b"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR\r\n")
    self.assertTrue(modhome.is_our_proxy(f.game_dir / "dinput8.dll"))
def test_install_idempotent(self):          # second run returns [] and leaves mtimes unchanged
def test_install_removes_older_mod_dlls_keeps_settings(self):   # pre-seed build/..._v998.dll + build/vr-settings.cfg
    self.assertFalse((h / "build/d2vr_render_probe_arm_v998.dll").exists())
    self.assertEqual((h / "build/vr-settings.cfg").read_text(), "7 1 1 1 45 90 2 0.5 2 65 1 0 0 0 0 0\n")
def test_install_wrong_game_hash_writes_nothing(self):
    with self.assertRaisesRegex(modhome.InstallError, "Steam updated the game"): modhome.install(inst, …)  # default GAME_SHA256
    self.assertFalse(modhome.mod_home_dir(f.prefix).exists()); self.assertEqual(sorted(os.listdir(f.game_dir)), ["Dishonored2.exe"])
def test_install_tampered_dll_writes_nothing(self):      # make_zip(tamper=True)
    with self.assertRaisesRegex(modhome.InstallError, "sha256"): …
    # same two "nothing written" assertions
def test_install_launch_blocked_and_wrong_game_sha_manifest_refused(self):
def test_install_refuses_foreign_dinput8(self):
    (f.game_dir / "dinput8.dll").write_bytes(b"MZ other loader")
    with self.assertRaisesRegex(modhome.InstallError, "another mod's dinput8.dll"): …
    self.assertEqual((f.game_dir / "dinput8.dll").read_bytes(), b"MZ other loader")
    self.assertFalse(modhome.mod_home_dir(f.prefix).exists())
def test_install_missing_proxy_explains_build(self):
def test_openxr_zip_hash_mismatch_refused(self):
def test_fetch_openxr_uses_cache_and_verifies(self):   # fake urlopen counts calls: 1 then 0; a bad payload raises, no cache file left
def test_uninstall_removes_only_our_files(self):
    self.assertEqual(sorted(modhome.uninstall(f.game_dir)), ["d2vr-linux.ini", "dinput8.dll"])
    self.assertTrue(modhome.mod_home_dir(f.prefix).exists())
def test_uninstall_keeps_foreign_dinput8(self):
def test_write_request_atomic(self):
    p = modhome.write_request(h, modhome.START_TRIGGER)
    self.assertEqual(p, h / "build/linux-start"); self.assertTrue(p.read_text().isdigit())
```

- [ ] **Step 2: Run them and confirm they fail.** Run `python3 -m unittest linux/tests/test_modhome.py -v`. Expected: ERROR, no `modhome`.
- [ ] **Step 3: Implement `modhome.py`** with the signatures and install order listed above.
- [ ] **Step 4: Run them and confirm they pass.** Run `python3 -m unittest discover -s linux/tests -v`. Expected: `OK`.
- [ ] **Step 5: Commit.** `git add linux && git commit -m "linux: mod home layout, release zip and OpenXR loader verification, proxy install/uninstall"`

---

### Task 3: Doctor checks and launch-options line (`doctor.py`)

**Files:**
- Create: `linux/d2vr_linux/doctor.py`, `linux/tests/fixtures/system.reg` (this machine's two `Khronos\\OpenXR\\1` sections, verbatim), `linux/tests/test_doctor.py`

**Interfaces:**
- Consumes: `steam.Install`, `steam.find_steamvr`, `steam.check_game_exe`, `modhome.mod_home_dir`, `modhome.is_our_proxy`, `modhome.DLL_RE`, `modhome.MANIFEST_RE`, `modhome.INI_NAME`, `modhome.PROXY_NAME`.
- Produces (in `d2vr_linux.doctor`):
  - `@dataclass class Check: name: str; ok: bool; detail: str; fix: str`
  - `DEFAULT_ICD_DIRS = [Path("/usr/share/vulkan/icd.d"), Path("/etc/vulkan/icd.d")]`
  - `WINE_BUILTIN_MARKER = b"Wine builtin DLL"`
  - `nvidia_gpu_name(proc_root: Path = Path("/proc")) -> str | None`: the `Model:` value, stripped, from the first `driver/nvidia/gpus/*/information` in sorted order.
  - `is_native_dll(path: Path) -> bool`: a regular file (not a symlink) whose first 1024 bytes do not contain the marker.
  - `prefix_openxr_runtime(prefix: Path) -> str | None`: the `"ActiveRuntime"` value in the `[Software\\Khronos\\OpenXR\\1]` section of `prefix/system.reg`, unescaped (`C:\openxr\wineopenxr64.json`).
  - `steamvr_running(proc_root: Path = Path("/proc")) -> bool`: true if any `<pid>/comm` equals `vrserver`.
  - `host_runtime(home: Path) -> tuple[bool, str]`: true when `home/.config/openxr/1/active_runtime.json` loads as JSON with `runtime.VALVE_runtime_is_steamvr == true`. Follows symlinks.
  - `fix_host_runtime(home: Path, steamvr_dir: Path) -> Path`: symlinks `active_runtime.json` to `steamvr_dir/steamxr_linux64.json` and creates parent directories. Raises `FileExistsError` if anything already exists at that path.
  - `run_checks(inst: Install | None, locate_error: str, home: Path, proc_root: Path, icd_dirs: list[Path], mod_home: Path | None = None, game_sha256: str | None = None, openxr_source: Path | None = None) -> list[Check]`: `None` means `steam.GAME_SHA256` / `modhome.OPENXR_SOURCE`, looked up at call time. Tests pass the fixture values.
  - `format_checks(checks: list[Check]) -> str`: one `PASS <name>: <detail>` or `FAIL <name>: <detail>` line per check. Each FAIL is followed by `      fix: <fix>`.
  - `launch_options(gpu_name: str) -> str`: the exact Global Constraints line.

**Check order, names and FAIL fix text.** These are the user-facing copy:

| # | name | passes when | fix |
|---|------|-------------|-----|
| 1 | `Dishonored 2` | `inst` is not None | `Install Dishonored 2 (app 403640) in a Linux Steam library, then start Steam once.` |
| 2 | `Game build` | `check_game_exe` ok | `Steam updated the game; this mod supports only build 17576867. Use a mod build made for the installed version, or restore 17576867 (Steam console: download_depot).` |
| 3 | `Proton prefix` | `inst.prefix` exists | `Set Proton Experimental for Dishonored 2 and start it once.` |
| 4 | `Mod home` | `.git/` exists, exactly one DLL matches `DLL_RE`, an `artifacts/` manifest's `sha256` equals that DLL's hash, and the loader's hash equals `source.json` `dll_sha256` | `d2vr-linux install --zip <d2vr-vNNN-dll.zip>` |
| 5 | `Proxy` | game-dir `dinput8.dll` is ours and the ini exists | `d2vr-linux install --zip …` |
| 6 | `SteamVR installed` | `find_steamvr` is not None | `Install SteamVR (app 250820) and switch it to the beta branch.` |
| 7 | `SteamVR running` | `steamvr_running` | `Start SteamVR and connect the Quest through Steam Link BEFORE launching the game (relaunch the game if it was started first).` |
| 8 | `Host OpenXR runtime` | `host_runtime` ok | `d2vr-linux doctor --fix-openxr (or SteamVR Settings > OpenXR > Set SteamVR as OpenXR runtime)` |
| 9 | `Prefix OpenXR runtime` | `prefix_openxr_runtime == r"C:\openxr\wineopenxr64.json"` and `drive_c/openxr/wineopenxr64.json` exists | `Start the game once with Proton Experimental so Proton registers wineopenxr.` |
| 10 | `VC++ runtime (native)` | `is_native_dll` for system32 `msvcp140`, `msvcp140_2`, `vcruntime140`, `vcruntime140_1` (Core.cs:145-146) | `protontricks 403640 vcrun2022` |
| 11 | `d3dcompiler_47 (native)` | `is_native_dll` for system32 `d3dcompiler_47.dll` | `protontricks 403640 d3dcompiler_47` |
| 12 | `NVIDIA Vulkan` | `nvidia_icd.x86_64.json` is in an icd dir and `nvidia_gpu_name` is not None (detail = name) | `Install the NVIDIA driver's Vulkan ICD (akmod-nvidia / xorg-x11-drv-nvidia).` |

When `inst` is None, checks 2–5 and 9–11 FAIL with detail `game not found`, and checks 6–8 and 12 still run.

- [ ] **Step 1: Write the failing tests** in `linux/tests/test_doctor.py`. The fixture `healthy(tmp)` builds on `make_steam` and adds:
  - native system32 DLLs (`b"MZ" + bytes(2000)`)
  - `system.reg` copied from the fixture, plus `drive_c/openxr/wineopenxr64.json`
  - a mod home installed through `modhome.install`
  - a SteamVR dir and appmanifest
  - `home/.config/openxr/1/active_runtime.json` → `steamxr_linux64.json` (`VALVE_runtime_is_steamvr: true`)
  - `proc/123/comm = "vrserver\n"`
  - `proc/driver/nvidia/gpus/0000:01:00.0/information` containing `Model: \t\t NVIDIA GeForce RTX 3070 Laptop GPU`
  - `icd/nvidia_icd.x86_64.json`

```python
def test_all_pass_on_healthy_fixture(self):
    checks = doctor.run_checks(...); self.assertEqual([c.name for c in checks if not c.ok], [])
    self.assertEqual(len(checks), 12)
def test_doctor_game_hash_fail(self):        # game_sha256=None (the real pin) against the fake exe
    c = by_name(checks, "Game build"); self.assertFalse(c.ok); self.assertIn("Steam updated the game", c.fix)
def test_builtin_stub_counts_as_missing(self):   # write b"MZ" + bytes(60) + b"Wine builtin DLL" to vcruntime140.dll
    self.assertFalse(by_name(checks, "VC++ runtime (native)").ok)
def test_symlinked_d3dcompiler_counts_as_missing(self):
def test_prefix_runtime_parsed_from_system_reg(self):
    self.assertEqual(doctor.prefix_openxr_runtime(prefix), r"C:\openxr\wineopenxr64.json")
def test_nvidia_name_from_proc(self):
    self.assertEqual(doctor.nvidia_gpu_name(proc), "NVIDIA GeForce RTX 3070 Laptop GPU")
def test_steamvr_not_running(self):        # comm "bash"
def test_fix_host_runtime_creates_symlink_and_never_overwrites(self):
    p = doctor.fix_host_runtime(home, steamvr_dir); self.assertEqual(p.resolve(), (steamvr_dir / "steamxr_linux64.json").resolve())
    with self.assertRaises(FileExistsError): doctor.fix_host_runtime(home, steamvr_dir)
def test_no_game_still_reports_host_checks(self):
    checks = doctor.run_checks(None, "Dishonored 2 not found", ...); self.assertEqual(len(checks), 12)
    self.assertTrue(by_name(checks, "NVIDIA Vulkan").ok)
def test_format_checks(self):
    self.assertEqual(doctor.format_checks([doctor.Check("A", True, "ok", ""), doctor.Check("B", False, "bad", "do x")]),
                     "PASS A: ok\nFAIL B: bad\n      fix: do x\n")
def test_launch_options_exact(self):
    self.assertEqual(doctor.launch_options("NVIDIA GeForce RTX 3070 Laptop GPU"),
        'WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="NVIDIA GeForce RTX 3070 Laptop GPU" '
        '__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%')
```

- [ ] **Step 2: Run them and confirm they fail.** Run `python3 -m unittest linux/tests/test_doctor.py -v`. Expected: ERROR, no `doctor`.
- [ ] **Step 3: Implement `doctor.py`** with the signatures and table above.
- [ ] **Step 4: Run them and confirm they pass.** Run `python3 -m unittest discover -s linux/tests -v`. Expected: `OK`.
- [ ] **Step 5: Commit.** `git add linux && git commit -m "linux: doctor checks for SteamVR, OpenXR, prefix runtimes and NVIDIA; launch options"`

---

### Task 4: CLI (`cli.py`, `linux/d2vr-linux`)

**Files:**
- Create: `linux/d2vr_linux/cli.py`, `linux/d2vr-linux` (mode 755, `#!/usr/bin/env python3`; inserts its own directory into `sys.path`, then `sys.exit(main())`), `linux/tests/test_cli.py`

**Interfaces:**
- Consumes: everything listed under Produces in Tasks 1–3.
- Produces: `main(argv: list[str] | None = None, *, home: Path | None = None, proc_root: Path = Path("/proc"), icd_dirs: list[Path] | None = None, urlopen=None) -> int`.

| subcommand | args | behaviour | exit |
|---|---|---|---|
| `install` | `--zip Z` (required), `--proxy P` (default `modhome.DEFAULT_PROXY`), `--openxr-zip O` | `locate` → `modhome.install` (cache `~/.cache/d2vr-linux`). Prints changed files, then `Steam launch options:` and the line (if an NVIDIA GPU is found) | 0, or 1 on `SteamError`/`InstallError` |
| `uninstall` | none | `modhome.uninstall(game_dir)`. Prints removed files and `Mod home left in place: <path>` | 0 / 1 |
| `doctor` | `--fix-openxr`, `--mod-home M` | Runs the fix first if asked (prints the created path, or a message on `FileExistsError`), then prints `format_checks` | 0 if all PASS, else 1 |
| `launch-options` | none | Prints `launch_options(nvidia_gpu_name())` | 0, or 1 with `no NVIDIA GPU found` |
| `start` / `stop` | `--mod-home M` | `write_request(mod_home, START_TRIGGER / STOP_REQUEST)`. Prints the path | 0 / 1 |

- Global option: `--steam-root PATH`.
- Errors print `d2vr-linux: error: <message>` to stderr. Argparse usage errors exit 2.
- `cli` passes `game_sha256=steam.GAME_SHA256` and `openxr_source=modhome.OPENXR_SOURCE`, read at call time rather than through default arguments, so tests can patch them.
- The mod home for `start`, `stop` and `doctor` is `--mod-home`, or else `mod_home_dir(locate(...).prefix)`. `start` and `stop` fail (exit 1) if that directory has no `build/`.

- [ ] **Step 1: Write the failing tests** in `linux/tests/test_cli.py`. They call `cli.main([...], home=f.home, proc_root=…, icd_dirs=…)` and capture stdout/stderr with `contextlib.redirect_stdout`.

```python
def test_install_then_uninstall_roundtrip(self):
    self.assertEqual(cli.main(["install", "--zip", str(z), "--proxy", str(proxy), "--openxr-zip", str(oxr)], home=f.home, ...), 0)
    # under mock.patch.object(steam, "GAME_SHA256", f.exe_sha256) and mock.patch.object(modhome, "OPENXR_SOURCE", src_json)
    self.assertIn("Steam launch options:", out)
    self.assertEqual(cli.main(["uninstall"], home=f.home), 0); self.assertFalse((f.game_dir / "dinput8.dll").exists())
def test_install_error_exit_1_message(self):     # tampered zip
    self.assertEqual(rc, 1); self.assertTrue(err.startswith("d2vr-linux: error: "))
def test_start_stop_write_requests_with_mod_home(self):
    self.assertEqual(cli.main(["start", "--mod-home", str(h)]), 0); self.assertTrue((h / "build/linux-start").exists())
    self.assertEqual(cli.main(["stop", "--mod-home", str(h)]), 0); self.assertTrue((h / "build/linux-stop").exists())
def test_start_without_mod_home_build_fails(self):
def test_doctor_exit_codes(self):          # healthy fixture -> 0; remove vrserver comm -> 1 and "FAIL SteamVR running" in out
def test_launch_options_no_gpu(self):      # empty proc_root -> 1, "no NVIDIA GPU found" in err
def test_wrapper_runs(self):
    r = subprocess.run([sys.executable, str(LINUX / "d2vr-linux"), "--help"], capture_output=True, text=True)
    self.assertEqual(r.returncode, 0); self.assertIn("launch-options", r.stdout)
```

- [ ] **Step 2: Run them and confirm they fail.** Run `python3 -m unittest linux/tests/test_cli.py -v`. Expected: ERROR.
- [ ] **Step 3: Implement `cli.py` and the wrapper.**
- [ ] **Step 4: Run them and confirm they pass.** Run `python3 -m unittest discover -s linux/tests -v`. Expected: `OK`.
- [ ] **Step 5: Run a read-only smoke test on this machine.**
  - Run `python3 linux/d2vr-linux launch-options`. Expected: exactly `WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="NVIDIA GeForce RTX 3070 Laptop GPU" __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%`.
  - Run `python3 linux/d2vr-linux doctor`. Expected: 12 lines, with PASS for `Dishonored 2`, `Game build`, `Proton prefix`, `Prefix OpenXR runtime`, `SteamVR installed` and `NVIDIA Vulkan`. FAIL is expected today (not a bug) for `Host OpenXR runtime`, `VC++ runtime (native)`, `d3dcompiler_47 (native)`, `Mod home` and `Proxy`.
  - Do not run `install` against the real game in this task.
- [ ] **Step 6: Commit.** `git add linux && git commit -m "linux: d2vr-linux CLI (install, uninstall, doctor, launch-options, start, stop)"`

---

### Task 5: Proxy core helpers, Wine-hosted unit check, and build script

**Files:**
- Create: `linux/proxy/proxy_core.hpp`, `linux/proxy/build.py`, `linux/tests/wine_support.py`, `linux/tests/proxy_check/proxy_core_check.cpp`, `linux/tests/test_proxy_core.py`

**Interfaces:**
- Produces (in `linux/proxy/proxy_core.hpp`, `namespace d2vr_linux`, header-only, `<string>/<vector>/<optional>` only):
  - `inline constexpr char kProxyMarker[] = "D2VR-LINUX-PROXY-1";`
  - `inline constexpr wchar_t kDefaultModHome[] = L"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR";`
  - `kIniName[] = L"d2vr-linux.ini"`, `kStartTrigger[] = L"linux-start"`, `kStopRequest[] = L"linux-stop"`, `kSettleKnob[] = L"linux-settle-seconds"`, `kLogName[] = L"linux-proxy.log"`
  - `inline constexpr unsigned long kRequiredCapabilities = 61017;` (`1|8|16|64|512|1024|2048|8192|16384|32768`, Probe.cs:115)
  - `inline constexpr unsigned kDefaultSettleSeconds = 45;`
  - `std::wstring mod_home_from_ini(const std::wstring& text)`: first non-blank line, with a leading U+FEFF, surrounding whitespace, `\r` and trailing `\` stripped. Returns `kDefaultModHome` if there is none.
  - `std::string probe_config(unsigned long pid)`: the exact `Probe.cs:122` line.
  - `std::optional<std::wstring> select_mod_dll(const std::vector<std::wstring>& names, std::wstring& why)`: the single name matching `^d2vr_render_probe_arm_v[0-9]+\.dll$`, case-sensitive. If there are zero or several, sets `why` to `expected exactly one d2vr_render_probe_arm_v*.dll in build (found N)`.
  - `bool capabilities_ok(unsigned long caps)`
  - `bool is_game_window_title(const std::wstring& title)`: contains `L"Dishonored"` (as `GameKeys.Post`, Probe.cs:568).
  - `unsigned settle_seconds_from_knob(const std::string& text)`: parses a decimal integer and clamps it to 0..3600. Anything else returns 45.
  - `std::string focus_cvars_to_append(const std::string& cfg)`: the `GameKeys.PrepareConfig` algorithm (Probe.cs:550-555). For each of `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` whose name is not the first token of some line, append `name value\r\n`. If anything is appended and `cfg` is non-empty and does not end in `\n`, prefix the result with `\r\n`.
- Produces (in `linux/proxy/build.py`): `TARGETS: dict[str, dict]`, holding entries named `core_check` in this task, plus `proxy`, `host`, `fake_mod` and `fake_mod_lowcaps` added in Task 6. `OUT = REPO/"build/linux-proxy"`. `cl_args(target: str) -> list[str]` and `build(targets: Iterable[str]) -> dict[str, Path]` create each `/Fe:` parent directory and run `harness.cl_to_clang(cl_args(t))` with `cwd=REPO`. They raise `RuntimeError(output)` on rc≠0 or on any `warning:` in the output. `main()` takes `[--tests]` (all targets; default is `proxy` only) and prints `name path` lines.
- Produces (in `linux/tests/wine_support.py`):
  - `toolchain_available() -> bool`: `harness.LLVM_BIN/clang-cl` and `harness.XWIN/crt` exist, and `wine` is on PATH.
  - `wine_env(extra_overrides: str = "") -> dict`: `harness.wine_env()` with `;`+`extra_overrides` appended to `WINEDLLOVERRIDES`.
  - `start_wine()`: `harness.prepare_wine()`.
  - `stop_wine()`: `wineserver -k` with that env.
  - `to_wine_path(p: Path) -> str`: `"Z:" + str(p.resolve()).replace("/", "\\")`.

`core_check` cl args: `/nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ilinux/proxy linux/tests/proxy_check/proxy_core_check.cpp /Fo:build/linux-proxy/ /Fe:build/linux-proxy/proxy_core_check.exe`.

- [ ] **Step 1: Write the failing check** `linux/tests/proxy_check/proxy_core_check.cpp`. It uses an `expect(bool, const char*)` counter and prints `proxy_core_check: all passed`, exiting 0 only when nothing failed. Cases:

```cpp
expect(probe_config(4242) == "4242 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n", "cfg");
expect(mod_home_from_ini(L"\xFEFF  Z:\\home\\u\\mod home\\\r\n") == L"Z:\\home\\u\\mod home", "ini bom/crlf/trailing slash");
expect(mod_home_from_ini(L"\r\n \r\n") == kDefaultModHome, "ini empty -> default");
std::wstring why;
expect(*select_mod_dll({L"x.txt", L"d2vr_render_probe_arm_v562.dll"}, why) == L"d2vr_render_probe_arm_v562.dll", "one dll");
expect(!select_mod_dll({L"d2vr_render_probe_arm_v1.dll", L"d2vr_render_probe_arm_v2.dll"}, why) && why.find(L"found 2") != std::wstring::npos, "two dlls");
expect(!select_mod_dll({L"d2vr_render_probe.dll", L"D2VR_RENDER_PROBE_ARM_V1.DLL"}, why), "regex exact");
expect(capabilities_ok(524287) && !capabilities_ok(61017 & ~16u), "caps");
expect(is_game_window_title(L"Dishonored 2") && !is_game_window_title(L"Steam"), "title");
expect(settle_seconds_from_knob("2\n") == 2 && settle_seconds_from_knob("abc") == 45 && settle_seconds_from_knob("99999") == 3600, "settle");
expect(focus_cvars_to_append("seta a \"1\"") == "\r\nwin_pauseOnFocusLoss \"0\"\r\nin_controlInactiveWindow \"1\"\r\n", "focus append");
expect(focus_cvars_to_append("win_pauseOnFocusLoss \"1\"\nin_controlInactiveWindow \"0\"\n").empty(), "focus present (value not forced)");
```

Then write `linux/tests/test_proxy_core.py`:
- `setUpClass`: `skipTest("clang-cl/xwin/wine toolchain missing: run tools/linux/setup-toolchain.sh")` unless `toolchain_available()`. Otherwise `build({"core_check"})` and `start_wine()`.
- `tearDownClass`: `stop_wine()`.
- `test_core_check_under_wine`: `wine proxy_core_check.exe` → rc 0 and `proxy_core_check: all passed` in stdout.
- `test_constants_match_windows_sources`: reading `linux/proxy/proxy_core.hpp` text, check that `61017 == eval of the "1 | 8 | … | 32768" expression in Probe.cs:115`, that the `probe_config` literal equals the `Probe.cs:122` format with `{pid}` removed, and that the marker, `linux-start` and `linux-stop` equal `modhome.PROXY_MARKER`, `START_TRIGGER` and `STOP_REQUEST`.

- [ ] **Step 2: Run them and confirm they fail.** Run `python3 -m unittest linux/tests/test_proxy_core.py -v`. Expected: ERROR or FAIL, because `proxy_core.hpp`/`build.py` are missing.
- [ ] **Step 3: Implement `proxy_core.hpp`, `build.py` and `wine_support.py`** with the signatures listed above.
- [ ] **Step 4: Run them and confirm they pass.** Run `python3 -m unittest discover -s linux/tests -v`. Expected: `OK`, with no skips on this machine.
- [ ] **Step 5: Commit.** `git add linux && git commit -m "linux: proxy core helpers with Wine-hosted check and clang-cl build script"`

---

### Task 6: `dinput8.dll` proxy and Wine-hosted `proxy_check`

**Files:**
- Create: `linux/proxy/dinput8_proxy.cpp`, `linux/proxy/dinput8.def`, `linux/proxy/build.cmd`
- Modify: `linux/proxy/build.py` (add `TARGETS` `proxy`, `host`, `fake_mod`, `fake_mod_lowcaps`)
- Create: `linux/tests/proxy_check/host.cpp`, `linux/tests/proxy_check/fake_mod.cpp`, `linux/tests/proxy_check/run_proxy_check.py`, `linux/tests/test_proxy_build.py`

**Interfaces:**
- Consumes: everything in `proxy_core.hpp`; `build.build`, `build.cl_args`; everything in `wine_support`; the CLI `start`/`stop --mod-home`; `modhome.is_our_proxy`.
- Produces: `build/linux-proxy/dinput8.dll`, which exports only `DirectInput8Create` and contains `kProxyMarker`. Later tasks rely on these log lines in `<modhome>\artifacts\linux-proxy.log`, one per event, each prefixed with an ISO-8601 local time:
  - `proxy D2VR-LINUX-PROXY-1 pid=<pid> exe=<path> modhome=<path>`
  - `removed stale linux-start` / `removed stale linux-stop`
  - `focus cvars appended` / `focus cvars present` / `dishonored2Config.cfg not found`
  - `game window found; waiting for start (trigger or <N> s)`
  - `start trigger` / `settle timeout`
  - `expected exactly one d2vr_render_probe_arm_v*.dll in build (found N)`
  - `ProbeStatus=<n>: a session is still active or restoring`
  - `ProbeCapabilities=<n> lacks required 61017`
  - `restart the game to load <name>`
  - `StartProbe begin <dll>`
  - `StartProbe returned <code>`
  - `StopProbe returned <code>`

**cl args.** All targets share `/nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /Ilinux/proxy` and `/Fo:build/linux-proxy/`.
- `proxy`: `/LD linux/proxy/dinput8_proxy.cpp /Fe:build/linux-proxy/dinput8.dll /link /DEF:linux/proxy/dinput8.def kernel32.lib user32.lib`
- `fake_mod`: `/LD linux/tests/proxy_check/fake_mod.cpp /Fe:build/linux-proxy/d2vr_render_probe_arm_v999.dll`
- `host`: `linux/tests/proxy_check/host.cpp /Fe:build/linux-proxy/proxy_host.exe /link dinput8.lib dxguid.lib user32.lib`

`build.cmd` is for MSVC users and builds only the proxy: `@echo off`, `if not exist build\linux-proxy mkdir build\linux-proxy`, then `cl` plus `cl_args("proxy")` with `\` separators in path tokens.

**Proxy behaviour** (`dinput8_proxy.cpp`):
- **`DllMain(PROCESS_ATTACH)`:** `DisableThreadLibraryCalls`, store the module, `CreateThread(worker)` and close the handle. Nothing else.
- **`proxy_DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN)`:**
  - On first use (`InitOnceExecuteOnce`), `LoadLibraryW(GetSystemDirectoryW() + L"\\dinput8.dll")` and `GetProcAddress("DirectInput8Create")`.
  - Return `E_FAIL` and log if that fails, or if the module resolves to the proxy itself.
  - Otherwise return the real function's HRESULT.
- **Worker.** Wrap the whole body in `try { … } catch (...) { log; }`.
  1. The game dir is the parent of `GetModuleFileNameW(nullptr)`. Read `d2vr-linux.ini`, decode UTF-8, and apply `mod_home_from_ini`. If `<modhome>` does not exist, `OutputDebugStringW` and return.
  2. Create `artifacts\`. If `linux-proxy.log` exists, move it to `linux-proxy.previous.log`. Log the `proxy …` line.
  3. Append the focus cvars to `ExpandEnvironmentStringsW(L"%USERPROFILE%\\Saved Games\\Arkane Studios\\Dishonored2\\base\\dishonored2Config.cfg")`, only if the file exists.
  4. Delete stale `build\linux-start` and `build\linux-stop`, logging each one.
  5. Start the stop-watcher thread. Every 500 ms, if `build\linux-stop` exists: delete it; if the mod is loaded, call `StopProbe(nullptr)` and log the code; otherwise log `StopProbe returned none (no session)`.
  6. Read `settle = settle_seconds_from_knob(build\linux-settle-seconds or "")`.
  7. Loop until the process exits:
     - Poll every 500 ms for a visible top-level window owned by `GetCurrentProcessId()` whose title satisfies `is_game_window_title`.
     - Wait until `linux-start` exists (delete it, log `start trigger`), or until this is the first session and `settle` seconds have passed since the window was found (log `settle timeout`).
     - Select the DLL with `select_mod_dll`, from a `FindFirstFileW` listing of `build\d2vr_render_probe_arm_v*.dll`. If none qualifies, log and `continue`.
     - If a different DLL was loaded earlier in this process, log `restart the game to load <name>` and `continue`.
     - Write `build\dxgi-probe.cfg` = `probe_config(GetCurrentProcessId())` as binary ASCII.
     - Delete the three stale outputs (deviation 13).
     - `LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)` and resolve the four exports. The DLL stays loaded until the process exits.
     - Check `ProbeStatus(nullptr) == 0` and `capabilities_ok(ProbeCapabilities(nullptr))`, logging and continuing on failure.
     - Log `StartProbe begin`, call `StartProbe(nullptr)` on this thread, and log `StartProbe returned <code>`. Add a hint for 3 (`<modhome>\.git missing`) and for 1 (`see artifacts\dxgi-probe-failure.txt`).
     - Delete any `linux-start` written during the session.
- **Marker.** Reference `kProxyMarker` in the first log line so it stays in `.rdata`.

**Fake mod** (`fake_mod.cpp`):
- Exports `ProbeStatus`→0, `ProbeCapabilities`→524287, `StartProbe` and `StopProbe`, all `extern "C" __declspec(dllexport) DWORD WINAPI f(void*)`.
- `StartProbe`:
  - `root` = the parent of the module's folder.
  - Read the first token of `root\build\dxgi-probe.cfg`.
  - Write `root\artifacts\fake-start.txt` = `"<GetCurrentProcessId()> <cfg pid>\n"`.
  - Wait on a manual-reset event for up to 60 s. Return 0 if signalled, else 4.
- `StopProbe`: returns 2 when not in `StartProbe`; otherwise it calls `SetEvent` and returns 0.

**Host** (`host.cpp`):
- Takes `argv[1]` = timeout in seconds (default 90).
- Calls `DirectInput8Create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8W, &p, nullptr)` through the static DINPUT8 import. Prints and flushes `DirectInput8Create hr=0x%08lx iface=%d pid=%lu`, and exits 2 if it failed.
- Creates and shows the window `L"Dishonored 2 proxy_check"`.
- Pumps messages with a 200 ms `SetTimer`. When `<exe dir>\host-exit` exists, it exits 0. On timeout it exits 3.

**`run_proxy_check.py`** is a `unittest` module with `if __name__ == "__main__": unittest.main()`.
- `setUpClass`: skip unless `toolchain_available()`; otherwise `build(["proxy", "host", "fake_mod", "fake_mod_lowcaps", "core_check"])` and `start_wine()`. `tearDownClass`: `stop_wine()`.
- Per test, build a temp tree:
  - `<tmp>/Dishonored 2 test/`, holding `proxy_host.exe`, `dinput8.dll` and `d2vr-linux.ini`. The ini contains a UTF-8 BOM, `to_wine_path(<tmp>/mod home)` and CRLF.
  - `<tmp>/mod home/`, holding `.git/`, `build/d2vr_render_probe_arm_v999.dll` and `artifacts/`.
- Launch with `Popen(["wine", host, "90"], env=wine_env("dinput8=n,b"), stdout=<file>)`.
- Helpers: `wait_for(predicate, seconds)`, and `cli(*args)` = `subprocess.run([sys.executable, "linux/d2vr-linux", *args])`. Every test finishes by writing `host-exit` and asserting the host rc is 0.

```python
def test_proxy_exports_and_marker(self):
    out = subprocess.run([str(harness.LLVM_BIN / "llvm-readobj"), "--coff-exports", str(proxy)], capture_output=True, text=True).stdout
    self.assertEqual(re.findall(r"Name: (\S+)", out), ["DirectInput8Create"]); self.assertTrue(modhome.is_our_proxy(proxy))
def test_stale_trigger_ignored_then_start_and_stop(self):
    (home / "build/linux-start").write_text("1"); (home / "build/linux-settle-seconds").write_text("600")
    # launch host; wait_for(log contains "game window found", 30)
    self.assertIn("removed stale linux-start", log()); time.sleep(3); self.assertFalse(marker.exists())
    self.assertEqual(cli("start", "--mod-home", str(home)).returncode, 0)
    wait_for(marker.exists, 30); host_pid, cfg_pid = marker.read_text().split()
    self.assertEqual(host_pid, cfg_pid)
    self.assertEqual((home / "build/dxgi-probe.cfg").read_bytes(),
                     f"{host_pid} 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n".encode())
    self.assertFalse((home / "build/linux-start").exists())
    self.assertIn("hr=0x00000000 iface=1", host_stdout())
    self.assertEqual(cli("stop", "--mod-home", str(home)).returncode, 0)
    wait_for(lambda: "StartProbe returned 0" in log() and "StopProbe returned 0" in log(), 15)   # two threads log; order not fixed
def test_settle_timeout_autostarts_first_session(self):     # knob "2", no trigger
    wait_for(marker.exists, 30); self.assertIn("settle timeout", log())
def test_no_mod_dll_keeps_game_running(self):               # build/ has no dll; knob "600"; cli start
    wait_for(lambda: "expected exactly one d2vr_render_probe_arm_v*.dll in build (found 0)" in log(), 30)
    self.assertFalse(marker.exists()); self.assertIn("hr=0x00000000 iface=1", host_stdout())
def test_missing_capabilities_refused(self):                # second fake build with /DFAKE_CAPS=1 -> ProbeCapabilities returns 1
    wait_for(lambda: "lacks required 61017" in log(), 30); self.assertFalse(marker.exists())
```

For the last case, `TARGETS` also gets `fake_mod_lowcaps`: the same source with `/DFAKE_CAPS=1`, written to `build/linux-proxy/lowcaps/d2vr_render_probe_arm_v999.dll`.

`linux/tests/test_proxy_build.py` (no Wine needed):
```python
def test_build_cmd_matches_build_py(self):   # the single cl line == "cl " + " ".join(cl_args("proxy")) with "/" -> "\\" in path tokens (not in /flags)
def test_def_exports_only_directinput8create(self):
    self.assertEqual(re.findall(r"^\s+(\w+)", (PROXY / "dinput8.def").read_text(), re.M), ["DirectInput8Create"])
```

- [ ] **Step 1: Write the failing tests:** `host.cpp`, `fake_mod.cpp`, `run_proxy_check.py` and `test_proxy_build.py`.
- [ ] **Step 2: Run them and confirm they fail.** Run `python3 linux/tests/proxy_check/run_proxy_check.py -v`. Expected: ERROR in `setUpClass` (`KeyError: 'proxy'` / missing `dinput8_proxy.cpp`).
- [ ] **Step 3: Implement `dinput8_proxy.cpp`, `dinput8.def`, `build.cmd`, and the new `TARGETS` entries.**
- [ ] **Step 4: Run them and confirm they pass.** Run `python3 linux/tests/proxy_check/run_proxy_check.py -v`, then `python3 -m unittest discover -s linux/tests -v`. Expected: both `OK`, with no skips.
- [ ] **Step 5: Commit.** `git add linux && git commit -m "linux: dinput8 proxy that starts the mod in-process, with Wine-hosted proxy_check"`

---

### Task 7: User guide, Windows-unchanged gates, and headset checklist

**Files:**
- Create: `linux/README.md`

**Interfaces:**
- Consumes: the CLI commands from Task 4, the build command from Task 6, and the log lines and doctor check names from Tasks 3 and 6.

`linux/README.md` sections, in order:
1. **What this is.** The proxy and the CLI. Windows is unaffected. Out-of-scope list.
2. **Requirements.** Fedora with native (non-flatpak) Steam. SteamVR on the beta branch, ≥ 2.13 (Steam Link VR on Linux). Quest with the Steam Link app. Proton Experimental (fallback Proton 10.0). Do not use GE-Proton for the first run. Never set `PROTON_USE_WINED3D`.
3. **One-time setup.**
   - `tools/linux/setup-toolchain.sh` and `python3 linux/proxy/build.py`.
   - Run the game once with Proton Experimental.
   - `protontricks 403640 vcrun2022 d3dcompiler_47`.
   - Download `d2vr-vNNN-dll.zip` from the releases page.
   - `linux/d2vr-linux install --zip …`.
   - `linux/d2vr-linux doctor --fix-openxr`.
   - Paste the `launch-options` output into Steam > Dishonored 2 > Properties > Launch Options.
   - Consider "Only update this game when I launch it", and the warning that a Steam update breaks the hash pin.
   - Stop `bonsai.service` or any other VRAM user first.
4. **Every session.**
   - Start SteamVR and connect the Quest through Steam Link first.
   - `doctor` should show all PASS.
   - Launch from Steam. At the title screen, wait 45 s, or run `d2vr-linux start`.
   - Load the save in the headset.
   - `d2vr-linux stop` before quitting.
   - Explain that alt-tabbing is safe because the proxy writes the focus cvars.
5. **Troubleshooting.** Point to `linux-proxy.log` and its lines, `artifacts/dxgi-probe-failure.txt`, and `StartProbe returned 3/1`. Then cover each of these:
   - LUID mismatch ("Game device differs from XR device requirements"): check `DXVK_FILTER_DEVICE_NAME` and that the compositor runs on NVIDIA.
   - Black screen or shader errors: native `d3dcompiler_47`.
   - SteamVR started after the game: relaunch the game.
   - `PROTON_LOG=1` gives `~/steam-403640.log`, and `WINEDEBUG=+openxr` adds OpenXR tracing.
6. **Uninstall.** `d2vr-linux uninstall`. The mod home stays in the prefix.
7. **Developer tests.** The two test commands below.

- [ ] **Step 1: Write `linux/README.md`** with exactly the sections above. Commands and log lines must match the Interfaces of Tasks 4 and 6 character for character.
- [ ] **Step 2: Run the Linux tests.**
  - `python3 -m unittest discover -s linux/tests -v` → `OK`.
  - Then `python3 linux/tests/proxy_check/run_proxy_check.py -v` → `OK`.
- [ ] **Step 3: Check that Windows is untouched.** Run `git diff --name-only 29258da HEAD | grep -v -E '^(linux/|docs/)'`. Expected: empty output (exit 1 from grep). Then `git diff --stat 29258da HEAD -- src launcher tools tests`. Expected: empty.
- [ ] **Step 4: Run the harness gate** (about 30–60 min; nothing else may use the Wine prefix while it runs). Run `python3 tools/linux/run_build_cmd.py --keep-going 2>&1 | tail -60`. Expected: `==== summary: 468 passed / 0 failed / 41 skipped`, the same counts as the baseline. If the counts differ, Step 3 already proves `src/`, `tests/` and `tools/` are unchanged. Report the diff of SKIPPED names to the controller; do not "fix" anything outside `linux/`.
- [ ] **Step 5: Commit.** `git add linux/README.md && git commit -m "linux: user guide for Proton + SteamVR + Steam Link"`

**Manual headset checklist.** The user does this; it is not an automated test. Record the result in the PR description.
1. `doctor` shows all 12 PASS with SteamVR running and the Quest connected through Steam Link.
2. Launch Dishonored 2 from Steam. `linux-proxy.log` shows `game window found`.
3. At the title screen, wait for `settle timeout` or run `d2vr-linux start`. The log shows `StartProbe begin`, and `artifacts/xr-live-status.json` updates about once a second.
4. The headset shows the game image in both eyes, with no LUID-mismatch error in `dxgi-probe-failure.txt`.
5. Head tracking moves the view. Both Touch controllers track, and snap turn works.
6. Load a save. Gameplay stays responsive after alt-tabbing to a terminal and back.
7. `d2vr-linux stop` → the log shows `StopProbe returned 0` and then `StartProbe returned 0`. The game keeps running flat.
8. Quit the game, then run `d2vr-linux uninstall`. The game launches vanilla. Remove `dinput8=n,b` from the launch options.

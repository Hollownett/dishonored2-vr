"""Prepare or run a bounded candidate smoke/reload check on an owned private desktop."""
import argparse
import contextlib
import ctypes as ctypes
from ctypes import wintypes
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

import playground
import playground_native as native
from archive_build_source import matching_provenance
from game_state import game_logic_mask


SIM_FLAGS = ['--paired', '--native-eye-capture', '--vr-preview', '--head-tracking',
             '--controllers', '--symmetric-frustum', '--stable-camera', '--tracked-arms',
             '--physical-grab', '--controller-aim', '--vr-turning', 'snap',
             '--simulated-vr', '--simulated-input']


def stage_template(source, destination):
    files = list(source.rglob('*'))
    if source.is_symlink() or any(path.is_symlink() for path in files):
        raise ValueError('Save template must not contain links')
    baseline = native.saved_snapshot([source])
    if not baseline:
        raise ValueError('Save template is empty')
    for path in files:
        if path.is_file() and path.name.lower() != 'dishonored2config.cfg':
            target = destination / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
            if native.sha(target) != baseline[str(path)]:
                raise RuntimeError('Save template changed while copying')
    if native.saved_snapshot([source]) != baseline:
        raise RuntimeError('Save template changed during preparation')
    return baseline


def probe_workspace(run, dll):
    root = run / 'probe-root'
    for folder in ('tools', 'build', 'artifacts', '.git'):
        (root / folder).mkdir(parents=True)
    (root / '.git' / 'owned-diagnostic-sandbox.marker').write_text(
        'Owned diagnostic sandbox; not a Git checkout. StartProbe requires .git existence.\n')
    for helper in (playground.ROOT / 'tools').glob('*.py'):
        shutil.copyfile(helper, root / 'tools' / helper.name)
    candidate = root / 'build' / dll.name
    shutil.copyfile(dll, candidate)
    if native.sha(candidate) != native.sha(dll):
        raise RuntimeError('Candidate DLL changed while staging')
    (root / 'build' / 'full-body').touch()
    return root, candidate


def read_memory(reader, pid):
    handle = reader.open(0x10 | 0x1000, False, pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), 'Owned game read denied')
    def read(address, size):
        buffer = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not reader.read(handle, address, buffer, size, ctypes.byref(count)) or count.value != size:
            raise OSError(ctypes.get_last_error(), 'Owned game read failed')
        return buffer.raw
    return handle, read


def private_menu_key(reader, launcher_pid, game_pid, key, diagnostics=None):
    user = ctypes.WinDLL('user32', use_last_error=True)
    visit_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    open_desktop = native.api(user, 'OpenDesktopW', wintypes.HANDLE,
                              [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD])
    close_desktop = native.api(user, 'CloseDesktop', wintypes.BOOL, [wintypes.HANDLE])
    enumerate_windows = native.api(user, 'EnumDesktopWindows', wintypes.BOOL,
                                   [wintypes.HANDLE, visit_type, wintypes.LPARAM])
    title = native.api(user, 'GetWindowTextW', ctypes.c_int, [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int])
    post = native.api(user, 'PostMessageW', wintypes.BOOL,
                      [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM])
    map_key = native.api(user, 'MapVirtualKeyW', wintypes.UINT, [wintypes.UINT, wintypes.UINT])
    get_desktop = native.api(user, 'GetThreadDesktop', wintypes.HANDLE, [wintypes.DWORD])
    set_desktop = native.api(user, 'SetThreadDesktop', wintypes.BOOL, [wintypes.HANDLE])
    thread_id = native.api(reader.k, 'GetCurrentThreadId', wintypes.DWORD, [])
    original_desktop = get_desktop(thread_id())
    desktop = open_desktop('D2VRPlayground-' + str(launcher_pid), 0, False, 0x81)
    if not desktop:
        raise OSError(ctypes.get_last_error(), 'Owned private desktop unavailable')
    windows = []
    @visit_type
    def visit(window, parameter):
        owner = wintypes.DWORD()
        reader.window_pid(window, ctypes.byref(owner))
        if owner.value == game_pid:
            rectangle = wintypes.RECT()
            label = ctypes.create_unicode_buffer(256)
            title(window, label, 256)
            reader.client_rect(window, ctypes.byref(rectangle))
            if diagnostics is not None and len(diagnostics) < 20:
                diagnostics.append({'window': window, 'pid': owner.value, 'title': label.value,
                                    'width': rectangle.right, 'height': rectangle.bottom})
            if label.value.startswith('Dishonored 2 using VoidEngine') and rectangle.right >= 320 and rectangle.bottom >= 240:
                windows.append(window)
        return True
    try:
        if not set_desktop(desktop):
            raise OSError(ctypes.get_last_error(), 'Owned private thread desktop assignment failed')
        if not enumerate_windows(desktop, visit, 0):
            raise OSError(ctypes.get_last_error(), 'Owned private window enumeration failed')
        if key is None:
            return False
        if len(windows) != 1:
            return False
        scan = map_key(key, 0)
        if not post(windows[0], 0x100, key, 1 | (scan << 16)):
            raise OSError(ctypes.get_last_error(), 'Owned private key press failed')
        time.sleep(.08)
        if not post(windows[0], 0x101, key, 1 | (scan << 16) | (3 << 30)):
            raise OSError(ctypes.get_last_error(), 'Owned private key release failed')
        return True
    finally:
        if not set_desktop(original_desktop):
            raise OSError(ctypes.get_last_error(), 'Test thread desktop restoration failed')
        close_desktop(desktop)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--save-template', type=Path,
                        help='Explicit template containing the native fs-save root contents')
    parser.add_argument('--scenario', choices=('smoke', 'reload', 'menu-smoke'), default='smoke')
    parser.add_argument('--seconds', type=int)
    parser.add_argument('--launch', action='store_true')
    parser.add_argument('--inject-loading', action='store_true')
    parser.add_argument('--game', type=Path, default=native.GAME)
    args = parser.parse_args(argv)
    menu_smoke = args.scenario == 'menu-smoke'
    if args.seconds is None:
        args.seconds = 30 if menu_smoke else 100
    if menu_smoke:
        if args.save_template is not None or args.inject_loading:
            parser.error('menu-smoke forbids --save-template and --inject-loading')
        if args.seconds != 30:
            parser.error('menu-smoke requires --seconds 30')
    elif args.save_template is None or not 60 <= args.seconds <= 120:
        parser.error('smoke/reload require --save-template and --seconds 60..120')
    dll = args.dll if args.dll.is_absolute() else playground.ROOT / 'build' / args.dll
    dll = dll.resolve()
    template = args.save_template.resolve() if args.save_template is not None else None
    if not dll.is_file() or not re.fullmatch(r'd2vr_render_probe(?:_[a-z]+)*_v\d+\.dll', dll.name):
        parser.error('--dll must name an existing numbered candidate DLL')
    if template is not None and not template.is_dir():
        parser.error('--save-template must be an existing fs-save template directory')
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        code = native.main(['--game', str(args.game)])
    print(output.getvalue(), end='')
    if code:
        return code
    report_path = Path(output.getvalue().strip().split('Native preparation/startup report: ')[-1])
    preparation = json.loads(report_path.read_text())
    run, stage = Path(preparation['run_dir']), Path(preparation['stage'])
    report = {'schema': 1, 'scenario': args.scenario, 'prepared': False, 'launched': False,
              'run_dir': str(run), 'seconds': args.seconds, 'headset_acceptance': False,
              'desktop_input_used': False, 'passed': False, 'samples': [], 'commands': []}
    reader = native.NativeReader()
    shared_roots = [Path(path) for path in preparation['shared_save_roots']]
    baseline = native.saved_snapshot(shared_roots)
    report['shared_save_baseline'] = baseline
    launcher = injector = None
    launch_log = inject_log = None
    process_handle = None
    template_baseline = None
    injection_started = None
    try:
        provenance = matching_provenance(playground.ROOT, dll.name, native.sha(dll))
        if not provenance:
            raise RuntimeError('Candidate has no verified original workspace source pointer')
        report['original_build_provenance'] = {name: {'path': str(path), 'sha256': native.sha(path)}
                                               for name, path in provenance.items()}
        report['runtime_harness_sha256'] = native.sha(Path(__file__))
        report['runtime_launcher_source_sha256'] = native.sha(playground.ROOT / 'tools' / 'playground_desktop.cpp')
        report['runtime_launcher_sha256'] = native.sha(Path(preparation['command'][0]))
        report['runtime_helper_sha256'] = {name: native.sha(playground.ROOT / 'tools' / name) for name in
            ('playground_native.py', 'game_state.py', 'inject_probe.py')}
        report['scratch_root_is_git_checkout'] = False
        report['full_body_assets'] = 'Embedded player_rig_data.hpp; full-body marker enabled in scratch root'
        if not menu_smoke:
            template_baseline = stage_template(template, stage / 'fs-save')
        with (stage / 'fs-save' / 'base' / 'dishonored2Config.cfg').open('a') as config:
            config.write('win_pauseOnFocusLoss "0"\nin_controlInactiveWindow "1"\n')
        probe_root, candidate = probe_workspace(run, dll)
        report.update(prepared=True, save_template=str(template) if template is not None else None,
                      no_save_startup=menu_smoke, template_baseline=template_baseline,
                      dll=str(candidate), dll_sha256=native.sha(candidate), preparation_report=str(report_path))
        script = probe_root / 'artifacts' / 'neutral.json'
        native.atomic_json(script, {'segments': [{'start_ms': 0, 'end_ms': args.seconds * 1000,
                                               'input': {'move_x': 0.0}}]})
        if args.launch:
            if reader.games():
                raise RuntimeError('Existing Dishonored2 process; owned launch rejected')
            if any(native.sha(stage / name) != digest for name, digest in preparation['files'].items()):
                raise RuntimeError('Staged native image/dependencies changed')
            launch_log = (run / 'repair-launch.log').open('wb')
            launch_command = list(preparation['command'])
            if not menu_smoke and args.inject_loading:
                launch_command.append('--load-slot-one')
            if menu_smoke and launch_command != [preparation['command'][0], '--native-fresh',
                                                 str(args.game.resolve()), 'owned-child.pid']:
                raise RuntimeError('No-save startup requires the exact fresh native launcher command')
            report['launch_command'] = launch_command
            launcher = subprocess.Popen(launch_command, cwd=stage, stdout=launch_log,
                                        stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            report['launched'] = True
            deadline = time.monotonic() + args.seconds
            valid = 0
            mismatch_since = None
            pid = base = None
            module_ready = False
            module_deadline = time.monotonic() + 10
            gameplay_since = reloaded_since = None
            camera_ready_since = None
            dead = False
            last_pad = last_damage = 0.0
            damage_started = False
            def command(line):
                with (probe_root / 'artifacts' / 'vr-command.txt').open('a') as stream:
                    stream.write(line + '\n')
                report['commands'].append({'elapsed': round(args.seconds - (deadline - time.monotonic()), 3),
                                           'line': line})
            while time.monotonic() < deadline - (5 if menu_smoke else 0):
                now = time.monotonic()
                if launcher.poll() is not None:
                    raise RuntimeError('Owned native game exited early')
                pid_file = stage / 'owned-child.pid'
                if pid is None and pid_file.is_file():
                    pid = int(pid_file.read_text().strip())
                    report['owned_pid'] = pid
                if pid and reader.games() - {pid}:
                    raise RuntimeError('Unexpected Dishonored2 process outside owned job')
                if not pid:
                    time.sleep(.1)
                    continue
                module = reader.module(pid)
                if not module:
                    report.setdefault('module_snapshot_errors', []).append(reader.module_error)
                    if not module_ready and now < module_deadline:
                        time.sleep(.1)
                        continue
                    raise RuntimeError('Owned native image unavailable; module error ' + str(reader.module_error))
                base, path, size = module
                if path.resolve() != (stage / 'Dishonored2.exe').resolve() or size != 0x41cb000:
                    raise RuntimeError('Exact owned native module identity mismatch')
                module_ready = True
                values = reader.values(pid, base)
                if not values:
                    valid = 0
                    if injector:
                        raise RuntimeError('Native guards unavailable after injection')
                    time.sleep(.1)
                    continue
                valid, mismatch_since = native.advance_guards(values, preparation['expected_native_values'],
                                                              valid, mismatch_since, now)
                native.atomic_json(run / 'native-live.json', native.live_guard(preparation['nonce'], pid,
                    base, path, values, preparation['expected_native_values'], valid, 'monitoring', stage / 'Dishonored2.exe'))
                if valid >= 3 and not injector:
                    if native.sha(path) != native.PIN or native.sha(candidate) != report['dll_sha256']:
                        raise RuntimeError('Native executable or candidate hash changed before injection')
                    if not process_handle:
                        process_handle, read = read_memory(reader, pid)
                    try:
                        before_mask = game_logic_mask(read, base)
                    except OSError:
                        before_mask = None
                    report['samples'].append({'elapsed': round(args.seconds - (deadline - now), 3), 'logic_mask': before_mask})
                    if before_mask != 2 and not args.inject_loading and not menu_smoke:
                        camera_ready_since = None
                        if now - last_pad >= 3:
                            key = 0x1B if before_mask == 4 else 0x0D
                            if private_menu_key(reader, launcher.pid, pid, key, report.setdefault('private_windows', [])):
                                report['commands'].append({'owned_private_menu_key': key, 'logic_mask': before_mask})
                            last_pad = now
                        time.sleep(.1)
                        continue
                    if camera_ready_since is None:
                        camera_ready_since = now
                    if not args.inject_loading and not menu_smoke and now - camera_ready_since < 1:
                        time.sleep(.1)
                        continue
                    inject_log = (run / 'repair-inject.log').open('wb')
                    duration = 10 if menu_smoke else int(deadline - now) - 10
                    if (menu_smoke and deadline - now < 25) or (not menu_smoke and duration < 30):
                        raise RuntimeError('Insufficient guarded time remains for injection')
                    command_line = [sys.executable, str(probe_root / 'tools' / 'inject_probe.py'), '--pid', str(pid),
                                    '--dll-name', candidate.name, *(SIM_FLAGS[:-1] if menu_smoke else SIM_FLAGS),
                                    *([] if menu_smoke else ['--simulated-gameplay-script', str(script)]),
                                    '--seconds', str(duration)]
                    native.atomic_json(script, {'segments': [{'start_ms': 0, 'end_ms': duration * 1000,
                                                            'input': {'move_x': 0.0}}]})
                    injection_started = time.time()
                    injector = subprocess.Popen(command_line, stdout=inject_log, stderr=subprocess.STDOUT,
                                                creationflags=subprocess.CREATE_NO_WINDOW)
                    report['inject_command'] = command_line
                if injector:
                    mask = game_logic_mask(read, base)
                    report['samples'].append({'elapsed': round(args.seconds - (deadline - now), 3), 'logic_mask': mask})
                    if not menu_smoke and mask == 2:
                        if gameplay_since is None:
                            gameplay_since = now
                        if now - gameplay_since >= 5:
                            report['smoke_gameplay_verified'] = True
                        if dead:
                            if reloaded_since is None:
                                reloaded_since = now
                            if now - reloaded_since >= 5:
                                report['reload_verified'] = True
                        elif args.scenario == 'reload' and report.get('smoke_gameplay_verified') and now - last_damage >= 1:
                            command('console damage player1 components/equipment/damage/bullet_regular_npc.damage')
                            damage_started = True
                            last_damage = now
                    else:
                        gameplay_since = None
                        reloaded_since = None
                    if not menu_smoke and mask == 8192 and damage_started:
                        dead = True
                        report['console_damage_verified'] = True
                    if not menu_smoke and mask != 2 and (not report.get('smoke_gameplay_verified') or dead) and now - last_pad >= 2:
                        command('pad a 200')
                        last_pad = now
                    if injector.poll() is not None:
                        report['inject_exit'] = injector.returncode
                        if injector.returncode:
                            raise RuntimeError('Candidate injector failed; inspect repair-inject.log')
                        break
                time.sleep(.1)
            if not injector or injector.poll() is None:
                raise RuntimeError('Bounded session did not finish and restore before deadline')
            result_file = probe_root / 'artifacts' / 'dxgi-probe-result.json'
            if not result_file.is_file() or result_file.stat().st_mtime < injection_started:
                raise RuntimeError('Fresh candidate restoration report missing')
            restoration = json.loads(result_file.read_text())
            report['restoration'] = restoration
            report['restoration_verified'] = all(restoration.get(key) is True for key in
                ('hook_disabled', 'backend_hook_disabled', 'mode_restored', 'values_restored', 'body_material_restored',
                 'camera_comfort_restored', 'controller_import_restored', 'native_joystick_focus_restored',
                 'temporal_aa_restored', 'cached_output_restored'))
            report['faults_absent'] = all(restoration.get(key) == 0 for key in
                ('first_chance_fault_records', 'recoverable_faults', 'xr_recoveries'))
            variant = candidate.name[len('d2vr_render_probe'):-4].replace('_', '-')
            identity = json.loads((probe_root / 'artifacts' / ('loaded-render-probe' + variant + '.json')).read_text())
            report['loaded_candidate_identity'] = identity
            if identity.get('pid') != pid or identity.get('sha256') != report['dll_sha256']:
                raise RuntimeError('Loaded candidate identity does not match owned PID/hash')
            report['native_gameplay_render_verified'] = (restoration.get('native_eye_pairs', 0) > 0 and
                                                        restoration.get('xr_gameplay_layers', 0) > 0)
            report['candidate_menu_startup_verified'] = menu_smoke and valid >= 3
            if (not report['restoration_verified'] or not report['faults_absent'] or
                    (menu_smoke and not report['candidate_menu_startup_verified']) or
                    (not menu_smoke and (not report.get('smoke_gameplay_verified') or
                                        not report['native_gameplay_render_verified']))):
                raise RuntimeError('Native gameplay or candidate restoration was not verified')
            if args.scenario == 'reload' and not (report.get('console_damage_verified') and report.get('reload_verified')):
                raise RuntimeError('Native damage/death and checkpoint reload were not verified')
            report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        if injection_started:
            try:
                diagnostics = probe_root / 'artifacts' / 'dxgi-probe-result.json'
                if diagnostics.is_file() and diagnostics.stat().st_mtime >= injection_started:
                    report['restoration_diagnostics'] = json.loads(diagnostics.read_text())
                    report['restoration_diagnostics_sha256'] = native.sha(diagnostics)
            except (OSError, ValueError) as error:
                report['restoration_diagnostics_error'] = str(error)
        for process, label in ((injector, 'injector'), (launcher, 'launcher')):
            if process and process.poll() is None:
                process.kill()
                timeout = (max(.01, min(10, deadline - time.monotonic()))
                           if menu_smoke and report['launched'] else 10)
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    report.update(passed=False, error='Owned ' + label + ' cleanup timed out')
        cleanup_deadline = time.monotonic() + 5
        if menu_smoke and report['launched']:
            cleanup_deadline = min(cleanup_deadline, deadline)
        while report.get('owned_pid') in reader.games() and time.monotonic() < cleanup_deadline:
            time.sleep(.1)
        if process_handle:
            reader.close(process_handle)
        for stream in (inject_log, launch_log):
            if stream:
                stream.close()
        report['shared_save_after'] = native.saved_snapshot(shared_roots)
        report['shared_saves_unchanged'] = report['shared_save_after'] == baseline
        report['template_unchanged'] = (menu_smoke or
            (template_baseline is not None and native.saved_snapshot([template]) == template_baseline))
        report['installed_files_unchanged'] = all(native.sha(Path(preparation['game_source']) / name) == digest
                                                for name, digest in preparation['files'].items())
        report['owned_pid_stopped'] = not report.get('owned_pid') or report['owned_pid'] not in reader.games()
        if not all(report[key] for key in ('shared_saves_unchanged', 'template_unchanged', 'installed_files_unchanged')):
            report.update(passed=False, error='Shared saves, template or installed files changed')
        if not report['owned_pid_stopped']:
            report.update(passed=False, error='Owned job child cleanup did not complete')
        native.atomic_json(run / 'repair-results.json', report)
    print(f'Private repair report: {run / "repair-results.json"}')
    return 1 if report.get('error') else 0


if __name__ == '__main__':
    raise SystemExit(main())


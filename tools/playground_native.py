"""Prepare, or explicitly launch, a bounded owned native startup on a private desktop.

No input, save loading, injection or real XR session. Default is preparation only.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import shutil
import struct
import subprocess
import time
import uuid
import playground
from probe_game import PE

PIN = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2')
CVARS = {'fs_savepath': 0x32c71f0, 'fs_basepath': 0x32c7170,
         'net_usePlatformBackend': 0x25aef90, 'r_fullscreen': 0x32fb1d0,
         'r_mode': 0x32fb050,
         'r_windowWidth': 0x32fb250, 'r_windowHeight': 0x32fb2d0}


def native_modes(read, base):
    """Pinned renderer arrays, read only; indexes are diagnostic, not launch arguments."""
    pointer, count = struct.unpack('<QI', read(base+0x32faf90, 12))
    count &= 0xffffff
    if count > 8: raise ValueError('Native monitor count exceeds diagnostic bound')
    if not pointer or not count: return []
    records = read(pointer, count*0x40)
    monitors = []
    for index in range(count):
        raw = records[index*0x40:(index+1)*0x40]
        modes_pointer, modes_count = struct.unpack_from('<QI', raw, 0x20)
        modes_count &= 0xffffff
        if modes_count > 256: raise ValueError('Native mode count exceeds diagnostic bound')
        if modes_count and not modes_pointer: raise ValueError('Native mode array is incomplete')
        packed = read(modes_pointer, modes_count*4) if modes_count else b''
        modes = [{'array_index': i, 'width': value>>16, 'height': value&0xffff}
                 for i, (value,) in enumerate(struct.iter_unpack('<I', packed))]
        monitors.append({'monitor_index': index, 'monitor_handle': struct.unpack_from('<Q',raw)[0],
            'width': struct.unpack_from('<i',raw,8)[0], 'height': struct.unpack_from('<i',raw,12)[0],
            'primary': bool(raw[0x18]), 'modes': modes})
    return monitors


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def shared_saved_roots(executable):
    # Use the exact known-folder GUID passed by the pinned native default getter.
    pe=PE(executable.read_bytes())
    guid=c.create_string_buffer(pe.data[pe.offset(0x1af3c40):pe.offset(0x1af3c40)+16])
    pointer=c.c_void_p()
    shell=c.WinDLL('shell32',use_last_error=True)
    get=api(shell,'SHGetKnownFolderPath',c.c_long,[c.c_void_p,w.DWORD,w.HANDLE,c.POINTER(c.c_void_p)])
    if get(guid,0,None,c.byref(pointer)) < 0: raise RuntimeError('Shared save-root baseline cannot be resolved')
    try: saved=Path(c.wstring_at(pointer))/'Arkane Studios/dishonored2'
    finally:
        free=api(c.WinDLL('ole32'),'CoTaskMemFree',None,[c.c_void_p]);free(pointer)
    steam=GAME.parents[2]/'userdata'
    return [saved]+[p for p in steam.glob('*/403640') if p.is_dir()]


def saved_snapshot(roots):
    return {str(p):sha(p) for root in roots for p in root.rglob('*') if p.is_file()}


class ProcessEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD),
                ('heap', c.c_size_t), ('module', w.DWORD), ('threads', w.DWORD),
                ('parent', w.DWORD), ('priority', w.LONG), ('flags', w.DWORD),
                ('exe', w.WCHAR * 260)]


class ModuleEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('module_id', w.DWORD), ('pid', w.DWORD),
                ('global_usage', w.DWORD), ('usage', w.DWORD), ('base', c.c_void_p),
                ('image_size', w.DWORD), ('module', w.HMODULE), ('name', w.WCHAR * 256),
                ('path', w.WCHAR * 260)]


def api(dll, name, restype, argtypes):
    fn = getattr(dll, name); fn.restype = restype; fn.argtypes = argtypes
    return fn


class NativeReader:
    def __init__(self):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.snapshot = api(self.k, 'CreateToolhelp32Snapshot', w.HANDLE, [w.DWORD, w.DWORD])
        self.close = api(self.k, 'CloseHandle', w.BOOL, [w.HANDLE])
        self.pfirst = api(self.k, 'Process32FirstW', w.BOOL, [w.HANDLE, c.POINTER(ProcessEntry)])
        self.pnext = api(self.k, 'Process32NextW', w.BOOL, [w.HANDLE, c.POINTER(ProcessEntry)])
        self.mfirst = api(self.k, 'Module32FirstW', w.BOOL, [w.HANDLE, c.POINTER(ModuleEntry)])
        self.open = api(self.k, 'OpenProcess', w.HANDLE, [w.DWORD, w.BOOL, w.DWORD])
        self.read = api(self.k, 'ReadProcessMemory', w.BOOL,
                        [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)])
        user = c.WinDLL('user32', use_last_error=True)
        self.window_pid = api(user, 'GetWindowThreadProcessId', w.DWORD, [w.HWND,c.POINTER(w.DWORD)])
        self.client_rect = api(user, 'GetClientRect', w.BOOL, [w.HWND,c.POINTER(w.RECT)])

    def games(self):
        handle = self.snapshot(2, 0)
        if handle == c.c_void_p(-1).value:
            raise OSError(c.get_last_error(), 'Process snapshot failed')
        try:
            item = ProcessEntry(); item.size = c.sizeof(item); result = set()
            ok = self.pfirst(handle, c.byref(item))
            while ok:
                if item.exe.lower() == 'dishonored2.exe': result.add(item.pid)
                ok = self.pnext(handle, c.byref(item))
            return result
        finally:
            self.close(handle)

    def module(self, pid):
        handle = self.snapshot(8 | 16, pid)
        if handle == c.c_void_p(-1).value: return None
        try:
            item = ModuleEntry(); item.size = c.sizeof(item)
            return (item.base, Path(item.path), item.image_size) if self.mfirst(handle, c.byref(item)) else None
        finally:
            self.close(handle)

    def values(self, pid, base):
        handle = self.open(0x10 | 0x1000, False, pid)
        if not handle: raise OSError(c.get_last_error(), 'Owned process read denied')
        def read(address, size):
            buffer = c.create_string_buffer(size); count = c.c_size_t()
            if not self.read(handle, address, buffer, size, c.byref(count)) or count.value != size:
                raise OSError(c.get_last_error(), 'Owned process memory read failed')
            return buffer.raw
        def string(address):
            # Bounded byte reads avoid crossing a committed-page boundary.
            data = bytearray()
            for i in range(4096):
                value = read(address+i, 1)
                if value == b'\0': return data.decode('utf-8')
                data.extend(value)
            raise ValueError('Unbounded native string')
        try:
            result = {}
            for name, rva in CVARS.items():
                raw = read(base+rva, 0x38)
                name_ptr = struct.unpack_from('<Q', raw, 0x30)[0]
                if not name_ptr or string(name_ptr) != name: return None
                if name.startswith('fs_'):
                    ptr = struct.unpack_from('<Q', raw)[0]
                    if not ptr: return None
                    result[name] = string(ptr)
                    if not result[name]: return None
                else: result[name] = struct.unpack_from('<i', raw, 0x28)[0]
            # Steam interface block must remain uninitialized, including RemoteStorage.
            result['steam_user_interface'] = struct.unpack('<Q', read(base+0x3133fa0,8))[0]
            result['steam_remote_interface'] = struct.unpack('<Q', read(base+0x3133fd8,8))[0]
            diagnostics = {}
            try:
                diagnostics['monitors'] = native_modes(read,base)
                diagnostics['selected_monitor_index'] = struct.unpack('<i',read(base+0x32faf6c,4))[0]
                diagnostics['r_mode'] = struct.unpack('<i',read(base+0x32fb050+0x28,4))[0]
                hwnd = struct.unpack('<Q',read(base+0x3137f40,8))[0]
                diagnostics['window_handle'] = hwnd
                owner = w.DWORD(); rect = w.RECT()
                if hwnd and self.window_pid(hwnd,c.byref(owner)) and owner.value == pid:
                    diagnostics['window_owner_verified'] = True
                    if self.client_rect(hwnd,c.byref(rect)):
                        diagnostics['client_rect'] = {'width':rect.right-rect.left,'height':rect.bottom-rect.top}
                else: diagnostics['window_owner_verified'] = False
            except (OSError,ValueError) as exc:
                diagnostics['error'] = str(exc)
            result['renderer_diagnostics'] = diagnostics
            return result
        finally:
            self.close(handle)


def expected_values(stage, game):
    return {'fs_savepath': str(stage / 'fs-save'), 'fs_basepath': str(game),
            'net_usePlatformBackend': 0, 'r_fullscreen': 0, 'r_mode': 0,
            'r_windowWidth': 640, 'r_windowHeight': 480,
            'steam_user_interface': 0, 'steam_remote_interface': 0}


def validate_sample(values, expected):
    def matches(key):
        return (Path(values[key]).resolve()==Path(expected[key]).resolve() if key.startswith('fs_')
                else values[key]==expected[key])
    dimensions=('r_windowWidth','r_windowHeight')
    return (all(matches(k) for k in expected if k not in dimensions),
            all(matches(k) for k in dimensions))


def seed_config(stage):
    path=stage/'fs-save/base/dishonored2Config.cfg';path.parent.mkdir()
    path.write_text('configVersion 10\nbindingVersion 44\nnet_usePlatformBackend "0"\n'
                    'r_fullscreen "0"\nr_mode "0"\nr_windowWidth "640"\nr_windowHeight "480"\n')
    return path


def resolve_native_mode(values):
    # Pinned 0x140345a70 subtracts each earlier monitor's mode count.
    diagnostics=(values or {}).get('renderer_diagnostics',{})
    monitors=diagnostics.get('monitors',[])
    if not monitors: return None
    index=values.get('r_mode',-1)
    if index<0: return {'invalid':True}
    for monitor in monitors:
        modes=monitor.get('modes',[])
        if not modes: return None
        if index<len(modes):
            return dict(modes[index],monitor_index=monitor.get('monitor_index'))
        index-=len(modes)
    return {'invalid':True}


def small_mode_state(values):
    # Captured on this machine in 20261010-223600-15514e6f, then rechecked live.
    mode=resolve_native_mode(values)
    if mode is None: return None
    return (mode=={'monitor_index':0,'array_index':0,'width':640,'height':480}
            and values.get('r_mode')==0)


def verified_small_mode(values):
    return small_mode_state(values) is True


def advance_guards(values,expected,consecutive,mismatch_since,now):
    critical,dimensions=validate_sample(values,expected)
    if not critical: raise RuntimeError('Native save/fullscreen/platform/mode gate mismatch')
    mode=small_mode_state(values)
    if mode is False: raise RuntimeError('Populated native mode table does not identify owned mode 0 as 640x480')
    if dimensions:
        return (consecutive+1 if mode is True else 0),None
    if mismatch_since is None: mismatch_since=now
    if now-mismatch_since>=3:
        raise RuntimeError('Native dimensions failed to settle at 640x480 within 3 seconds')
    return 0,mismatch_since


def live_guard(nonce,pid,base,path,values,expected,consecutive,status,executable):
    critical,dimensions=validate_sample(values,expected) if values else (False,False)
    identity=bool(pid and base and path and Path(path).resolve()==executable.resolve())
    mode_verified=verified_small_mode(values)
    return {'schema':1,'nonce':nonce,'timestamp_utc':datetime.now(timezone.utc).isoformat(),
            'unix_time':time.time(),'status':status,'owned_pid':pid,'owned_path':str(path) if path else None,
            'module_base':base,'module_base_hex':hex(base) if base else None,'values':values,
            'expected_values':expected,'critical_guards_valid':critical,'dimensions_valid':dimensions,
            'consecutive_valid_samples':consecutive,'identity_verified':identity,
            'mode_identity_verified':mode_verified,
            'ready':status=='monitoring' and identity and critical and dimensions and mode_verified and consecutive>=3,
            'desktop_input_used':False,'headset_acceptance':False}


def atomic_json(path,value):
    temporary=path.with_name('.'+path.name+'-'+uuid.uuid4().hex+'.tmp')
    try:
        with temporary.open('x',encoding='utf-8') as stream:
            json.dump(value,stream,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launch', action='store_true', help='explicit bounded fresh native startup; no input or injection')
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--game', type=Path, default=GAME)
    args = parser.parse_args(argv)
    if not 5 <= args.seconds <= 60: parser.error('--seconds must be 5..60')
    game = args.game.resolve()
    source = game / 'Dishonored2.exe'
    if not source.is_file() or sha(source) != PIN: parser.error('Installed game differs from pinned executable')
    reader = NativeReader()
    if reader.games(): parser.error('Close existing Dishonored2 processes; this lane never attaches to them')
    run = playground.ROOT / 'artifacts/playground/native-runs' / (time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
    stage = run / 'game'; stage.mkdir(parents=True); (stage / 'fs-save').mkdir()
    owned_config=seed_config(stage)
    files = [source] + sorted(game.glob('*.dll'))
    hashes = {}
    for path in files:
        target = stage / path.name; shutil.copyfile(path, target)
        hashes[path.name] = sha(target)
        if hashes[path.name] != sha(path): raise RuntimeError('Installed file changed while staging')
    (stage / 'native-playground-owned.marker').write_text(PIN+'\n')
    report = {'schema': 1, 'prepared': True, 'launched': False, 'run_dir': str(run),
              'stage': str(stage), 'game_source': str(game), 'files': hashes,
              'expected_native_values': expected_values(stage, game), 'samples': [],
              'desktop_input_used': False, 'headset_acceptance': False,
              'native_game_interaction_verified': False}
    report['owned_seed_config_sha256']=sha(owned_config)
    report['nonce']=uuid.uuid4().hex
    report['live_journal']=str(run/'native-live.json')
    shared_roots=shared_saved_roots(source)
    baseline=saved_snapshot(shared_roots)
    report['shared_save_roots']=[str(p) for p in shared_roots]
    report['shared_save_baseline']=baseline
    env = playground.toolchain_environment(playground.VS)
    compiler = Path(shutil.which('cl.exe', path=env['PATH']))
    fingerprint = hashlib.sha256(compiler.read_bytes()+playground.VS.read_bytes()+env['LIB'].encode()+env['INCLUDE'].encode()).hexdigest()
    launcher_work = run / 'launcher'; launcher_work.mkdir()
    launcher, build = playground.compile_test('playground_desktop', {'source':'tools/playground_desktop.cpp'},
        launcher_work, playground.ROOT / 'artifacts/playground/cache', env, fingerprint, 90)
    report['launcher_compile'] = build
    command = [str(launcher), '--native-fresh', str(game), 'owned-child.pid']
    report['command'] = command
    child = None;pid=None;base=None;path=None;values=None;valid=0
    def publish(status):
        atomic_json(run/'native-live.json',live_guard(report['nonce'],pid,base,path,values,
            report['expected_native_values'],valid,status,stage/'Dishonored2.exe'))
    publish('prepared')
    try:
        if build['exit_code']: raise RuntimeError('Native launcher compile failed')
        if args.launch:
            if reader.games(): raise RuntimeError('A game started during preparation; launch rejected')
            if any(sha(stage/name) != digest for name,digest in hashes.items()):
                raise RuntimeError('Owned staged executable/dependencies changed before launch')
            log = (run / 'native-launch.log').open('wb')
            child = subprocess.Popen(command, cwd=stage, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            report['launched'] = True
            deadline = time.monotonic()+args.seconds
            dimension_mismatch_since=None
            while time.monotonic() < deadline:
                if child.poll() is not None: raise RuntimeError(f'Owned native startup exited early: {child.returncode}')
                pid_path = stage / 'owned-child.pid'
                if not pid and pid_path.is_file():
                    pid = int(pid_path.read_text().strip()); report['owned_pid'] = pid
                seen = reader.games()
                if pid and seen - {pid}: raise RuntimeError('Unexpected game process outside the owned launch')
                sampled=False
                if pid:
                    module = reader.module(pid)
                    if module:
                        base, path, size = module
                        if path.resolve() != (stage/'Dishonored2.exe').resolve() or size != 0x41cb000:
                            raise RuntimeError('Owned native image identity mismatch')
                        values = reader.values(pid, base)
                        if values:
                            sampled=True
                            critical,dimensions=validate_sample(values,report['expected_native_values'])
                            mode_verified=verified_small_mode(values)
                            report['samples'].append({'seconds_remaining': round(deadline-time.monotonic(),3),
                                'values': values, 'valid': critical and dimensions and mode_verified,
                                'critical_guards_valid': critical, 'dimensions_valid': dimensions,
                                'mode_identity_verified': mode_verified})
                            valid,dimension_mismatch_since=advance_guards(values,report['expected_native_values'],
                                valid,dimension_mismatch_since,time.monotonic())
                if not sampled: valid=0;values=None
                publish('monitoring')
                time.sleep(.1)
            report['startup_guards_verified'] = valid >= 3
            if valid < 3: raise RuntimeError('Native cvar guards never became verifiable')
    except (OSError, RuntimeError, ValueError) as exc:
        report['error'] = str(exc)
        publish('rejected')
    finally:
        if child:
            # Closing the launcher closes its owned job and kills only its fresh child.
            child.kill(); child.wait(timeout=10); log.close()
        publish('stopped' if report['launched'] else 'prepared')
        report['isolated_save_files'] = {str(p.relative_to(stage/'fs-save')): sha(p)
                                      for p in (stage/'fs-save').rglob('*') if p.is_file()}
        report['installed_files_unchanged'] = all(sha(game/name) == digest for name,digest in hashes.items())
        report['shared_saves_unchanged']=saved_snapshot(shared_roots)==baseline
        if not report['installed_files_unchanged'] or not report['shared_saves_unchanged']:
            report['error']='Installed files or shared save baseline changed during owned startup'
        (run/'native-results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Native preparation/startup report: {run / "native-results.json"}')
    return 1 if report.get('error') else 0


if __name__ == '__main__':
    raise SystemExit(main())

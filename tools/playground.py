"""Owned offline VR tests. Never launches the game or an OpenXR runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path(__file__).with_name('playground_manifest.json')
FLAGS = ['/nologo', '/std:c++17', '/utf-8', '/EHsc', '/W4', '/WX', '/O2', '/MT']
LIBS = ['user32.lib', 'gdi32.lib', 'd3d11.lib', 'dxgi.lib', 'd3dcompiler.lib', 'Xinput9_1_0.lib', 'bcrypt.lib']
VS = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')


def digest_files(paths):
    h = hashlib.sha256()
    for path in sorted(paths, key=str):
        h.update(str(path.relative_to(ROOT)).encode())
        h.update(path.read_bytes())
    return h.hexdigest()


def dependency_digest(source):
    # Conservative whole-header closure: catches quoted/system-style local includes,
    # generated fixtures and vendor updates without relying on localized showIncludes.
    paths = [source]
    for folder in ('src', 'tests', 'third_party'):
        paths.extend(p for p in (ROOT / folder).rglob('*') if p.suffix.lower() in ('.hpp', '.h', '.inl'))
    return digest_files(set(paths))


def run_command(command, cwd, log, timeout, env=None):
    start = time.perf_counter()
    flags = (subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name == 'nt' else 0
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, creationflags=flags)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
        else:
            process.kill()
        output, _ = process.communicate()
    log.write_bytes(output)
    return {'exit_code': process.returncode, 'seconds': round(time.perf_counter() - start, 4),
            'timeout': timed_out, 'log': str(log), 'command': command}


def toolchain_environment(vcvars):
    if not vcvars.is_file():
        raise RuntimeError(f'MSVC setup missing: {vcvars}')
    # Paths originate in the installed toolchain, not user-supplied shell text.
    command = f'call "{vcvars}" >nul && set'
    result = subprocess.run(command, shell=True, capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    return {key.upper(): value for key, value in
            (line.split('=', 1) for line in result.stdout.decode(errors='replace').splitlines()
             if '=' in line and not line.startswith('='))}


def load_manifest():
    manifest = json.loads(MANIFEST.read_text())
    for name, entry in manifest.items():
        if not name.replace('_', '').isalnum():
            raise ValueError(f'Invalid test name: {name}')
        path = (ROOT / entry['source']).resolve()
        if not path.is_relative_to(ROOT / 'tests') or path.suffix not in ('.cpp', '.py') or not path.is_file():
            raise ValueError(f'Invalid offline test source: {path}')
    return manifest


def compile_test(name, entry, work, cache, env, compiler_hash, timeout):
    source = ROOT / entry['source']
    before = dependency_digest(source)
    key = hashlib.sha256(json.dumps([before, FLAGS, LIBS, compiler_hash], sort_keys=True).encode()).hexdigest()
    exe = cache / key / (name + '.exe')
    if exe.is_file():
        return exe, {'cached': True, 'seconds': 0, 'key': key, 'source_digest': before, 'exit_code': 0}
    target = work / (name + '.exe')
    command = [shutil.which('cl.exe', path=env['PATH'])] + FLAGS
    command += ['/I' + str(ROOT / 'third_party' / part / 'include') for part in ('minhook', 'openxr')]
    command += [str(source), '/Fo:' + str(work / (name + '.obj')), '/Fe:' + str(target), '/link'] + LIBS
    result = run_command(command, work, work / 'compile.log', timeout, env)
    result.update(cached=False, key=key, source_digest=before)
    if result['exit_code'] == 0:
        if dependency_digest(source) != before:
            result.update(exit_code=3, source_changed=True)
            return target, result
        exe.parent.mkdir(parents=True, exist_ok=True)
        staged = exe.parent / (uuid.uuid4().hex + '.tmp')
        shutil.copyfile(target, staged)
        os.replace(staged, exe)
    return exe, result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('fast', 'gpu', 'replay', 'all'), default='fast')
    parser.add_argument('--test', action='append', help='exact reviewed manifest name; repeatable')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--render', action='store_true', help='render first 3 rig frames with existing installed numpy/Pillow')
    parser.add_argument('--isolated-desktop', action='store_true', help='run owned C++ fixtures on a never activated private Win32 desktop')
    parser.add_argument('--vcvars', type=Path, default=VS)
    parser.add_argument('--timeout', type=int, default=90, help='per compile/test deadline in seconds')
    args = parser.parse_args(argv)
    manifest = load_manifest()
    names = args.test or [n for n, e in manifest.items() if args.suite == 'all' or args.suite in e['suites']]
    if any(n not in manifest for n in names):
        parser.error('Unknown test name; use --list')
    if args.timeout < 1:
        parser.error('--timeout must be positive')
    if args.list:
        print(json.dumps(manifest, indent=2))
        return 0
    base = ROOT / 'artifacts' / 'playground'
    run = base / 'runs' / (time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    report = {'schema': 1, 'suite': args.suite, 'run_dir': str(run), 'tests': [],
              'native_game_interaction_verified': False, 'headset_acceptance': False,
              'desktop_input_used': False, 'live_runtime_used': False}
    report['private_desktop'] = args.isolated_desktop
    start = time.perf_counter()
    env = None
    try:
        if any(manifest[n]['source'].endswith('.cpp') for n in names):
            env = toolchain_environment(args.vcvars.resolve())
            compiler = Path(shutil.which('cl.exe', path=env['PATH']))
            report['compiler'] = str(compiler)
            compiler_hash = hashlib.sha256(compiler.read_bytes() + args.vcvars.read_bytes() +
                                           env.get('INCLUDE', '').encode() + env.get('LIB', '').encode()).hexdigest()
        desktop_exe = None
        if args.isolated_desktop:
            if env is None:
                raise RuntimeError('Private desktop requires a C++ fixture selection')
            desktop_work = run / 'desktop-launcher'
            desktop_work.mkdir()
            desktop_exe, desktop_build = compile_test('playground_desktop',
                {'source': 'tools/playground_desktop.cpp'}, desktop_work, base / 'cache', env, compiler_hash, args.timeout)
            report['desktop_launcher_compile'] = desktop_build
            if desktop_build['exit_code']:
                raise RuntimeError('Private desktop launcher compilation failed; see compile log')
        for name in names:
            work = run / name
            (work / 'artifacts').mkdir(parents=True)
            (work / 'build').mkdir()
            entry = manifest[name]
            result = {'name': name, 'source': entry['source']}
            report['tests'].append(result)
            if entry['source'].endswith('.cpp'):
                exe, build = compile_test(name, entry, work, base / 'cache', env, compiler_hash, args.timeout)
                result['compile'] = build
                if build['exit_code']:
                    result['status'] = 'source_changed' if build.get('source_changed') else 'compile_failed'
                    print(f'{name}: {result["status"]}', flush=True)
                    continue
                commands = [[str(exe)]]
                if name == 'body_ik_check':
                    commands = [[str(exe), str(work / (c + '.json')), c] for c in ('corvo', 'emily')]
                if desktop_exe:
                    owned_name = 'playground-owned-' + name + '.exe'
                    shutil.copyfile(exe, work / owned_name)
                    commands = [[str(desktop_exe), owned_name] + c[1:] for c in commands]
            else:
                commands = [[sys.executable, str(ROOT / entry['source'])]]
            result['runs'] = [run_command(c, work, work / f'run-{i}.log', args.timeout, env)
                              for i, c in enumerate(commands)]
            if args.render and name == 'body_ik_check' and all(r['exit_code'] == 0 for r in result['runs']):
                result['renders'] = [run_command([sys.executable, str(ROOT / 'tools/render_body_ik.py'),
                    str(work / (c + '.json')), str(work / ('render-' + c)), '--frames', '0,1,2'],
                    work, work / ('render-' + c + '.log'), args.timeout) for c in ('corvo', 'emily')]
            result['status'] = 'passed' if all(r['exit_code'] == 0 and not r['timeout']
                for r in result['runs'] + result.get('renders', [])) else 'failed'
            result['captures'] = {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in work.rglob('*') if p.suffix in ('.json', '.png', '.csv')}
            print(f'{name}: {result["status"]}', flush=True)
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        report['error'] = str(exc)
    report['seconds'] = round(time.perf_counter() - start, 4)
    report['passed'] = not report.get('error') and len(report['tests']) == len(names) and all(
        t.get('status') == 'passed' for t in report['tests'])
    (run / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Results: {run / "results.json"} ({report["seconds"]} s)', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

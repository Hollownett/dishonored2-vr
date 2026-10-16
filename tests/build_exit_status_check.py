import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'tools/build-msvc.cmd'
source = BUILD.read_bytes()
lines = source.decode('utf-8-sig').splitlines()
positive = re.compile(r'\s*if\s+errorlevel\s+1\s+goto\s+build_failed\s*', re.IGNORECASE)
negative = re.compile(r'\s*if\s+not\s+errorlevel\s+0\s+goto\s+build_failed\s*', re.IGNORECASE)
guards = []
locations = []
for index, line in enumerate(lines):
    if positive.fullmatch(line):
        assert index + 1 < len(lines) and negative.fullmatch(lines[index + 1]), f'Missing signed failure guard after line{index + 1}'
        guards.append((line.strip(), lines[index + 1].strip()))
        locations.append(index + 1)
    elif negative.fullmatch(line):
        assert index and positive.fullmatch(lines[index - 1]), f'Orphan signed failure guard at line{index + 1}'
assert guards, 'No native command failure guards found'
assert not any(re.fullmatch(r'\s*if\s+(not\s+)?errorlevel\s+[01]\s+exit\s+/b\s+1\s*', line, re.IGNORECASE) for line in lines), 'Unsafe inline FOR exit guard remains'
assert all('%errorlevel%' not in '\n'.join(pair).lower() for pair in guards)
labels = [index for index, line in enumerate(lines) if line.strip().lower() == ':build_failed']
assert len(labels) == 1, 'Exactly one build_failed label is required'
label = labels[0]
assert label > max(locations), 'Failure label must follow all command guards'
assert label > max(index for index, line in enumerate(lines) if line.strip() == ')'), 'Failure label must be outside FOR blocks'
assert label + 1 < len(lines) and lines[label + 1].strip().lower() == 'exit /b 1', 'Failure label must return nonzero'
previous = next(line.strip().lower() for line in reversed(lines[:label]) if line.strip())
assert previous in {'exit /b 0', 'exit /b %errorlevel%'}, 'Normal completion must not fall through to failure'
command = shutil.which('cmd.exe')
assert command, 'Actual Windows CMD is required for this check'
powershell = shutil.which('powershell.exe')
assert powershell, 'Actual native child PowerShell process is required for this check'
artifact_root = ROOT / 'artifacts/build-exit-status-check'
artifact_root.mkdir(parents=True, exist_ok=True)
output = Path(tempfile.mkdtemp(prefix='attempt-', dir=artifact_root))
results = []
environment = os.environ.copy()
for pair_index, pair in enumerate(sorted(set(guards)), 1):
    for child_kind in ['cmd-exit', 'native-powershell-exit']:
      for scope in ['outside', 'inside-for']:
        for code in [0, 1, 3, -1, -1073740791]:
            folder = output / f'{pair_index}-{child_kind}-{scope}-{code}'
            folder.mkdir()
            child = folder / 'child.cmd'
            child.write_bytes(f'@echo off\r\nexit /b {code}\r\n'.encode('ascii'))
            direct = subprocess.run([command, '/d', '/c', str(child)], cwd=folder, env=environment,
                                    capture_output=True, text=True, timeout=10)
            assert direct.returncode in {code, code & 0xffffffff}, (scope, code, direct.returncode, direct.stderr)
            invoke = f'"{command}" /d /c exit /b {code}' if child_kind == 'cmd-exit' else f'"{powershell}" -NoLogo -NoProfile -NonInteractive -Command "[Environment]::Exit({code})"'
            body = [invoke, *pair]
            if scope == 'inside-for':
                body = ['for %%G in (first second) do (', *['  ' + line for line in body], ')']
            batch = '\r\n'.join(['@echo off', *body, 'echo reached>"%~dp0sentinel.txt"', 'exit /b 0', ':build_failed', lines[label + 1].strip(), ''])
            assert '%errorlevel%' not in batch.lower()
            runner = folder / 'runner.cmd'
            runner.write_bytes(batch.encode('ascii'))
            run = subprocess.run([command, '/d', '/c', str(runner)], cwd=folder, env=environment,
                                 capture_output=True, text=True, timeout=20)
            (folder / 'stdout.log').write_text(run.stdout, encoding='utf-8')
            (folder / 'stderr.log').write_text(run.stderr, encoding='utf-8')
            reached = (folder / 'sentinel.txt').exists()
            assert reached == (code == 0), (scope, code, reached, run.returncode, run.stdout, run.stderr)
            assert run.returncode == (0 if code == 0 else 1), (scope, code, run.returncode)
            results.append(dict(guard_pair=pair, child_kind=child_kind, scope=scope, child_exit=code, direct_cmd_returncode=direct.returncode,
                                guarded_returncode=run.returncode, sentinel_reached=reached))
assert BUILD.read_bytes() == source, 'Build script changed during the check'
(output / 'results.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(source).hexdigest(),
    test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), positive_guard_lines=locations,
    guard_count=len(guards), unique_guard_pairs=len(set(guards)), cases=results,
    failure_label_line=label + 1,
    scope='Actual cmd.exe and owned CMD/PowerShell native child exits only; no compilers or game invoked'), indent=2), encoding='utf-8')
print(f'PASS: {len(guards)} paired source guards; {len(results)} actual CMD signed-exit/FOR cases; evidence {output}')

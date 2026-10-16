import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/run_demo.ps1'
ARTIFACTS = ROOT / 'artifacts/full-hand-launch-check'
POWERSHELL = shutil.which('powershell.exe') or shutil.which('pwsh.exe')
assert POWERSHELL, 'PowerShell is required for the actual launcher behavior check'
ARTIFACTS.mkdir(parents=True, exist_ok=True)
OUTPUT = Path(tempfile.mkdtemp(prefix='attempt-', dir=ARTIFACTS))
original_script = SCRIPT.read_bytes()
environment = os.environ.copy()
environment = {key: value for key, value in environment.items() if key.upper() != 'PSMODULEPATH'}
marker_bytes = b'\x00\xff\x80previous marker\r\n\x00'
harness = r'''
param([string]$CasePath)
$ErrorActionPreference='Stop'
$case=Get-Content -LiteralPath (Join-Path $CasePath 'case.json') -Raw | ConvertFrom-Json
$global:FakeGamePath=Join-Path $CasePath 'Dishonored2.exe'
$global:MarkerPath=Join-Path $CasePath 'build\full-hand-query'
$global:InvocationPath=Join-Path $CasePath 'invocation.json'
$global:MockMode=$case.mode
$global:MockCalls=0
$null=Get-Command Get-FileHash
$env:PATH=Join-Path $CasePath 'empty-bin'
function global:Get-Process {
    param([string]$Name,[string]$ErrorAction)
    if($Name -ne 'Dishonored2'){throw 'Unexpected process lookup'}
    [pscustomobject]@{Id=424242;Path=$global:FakeGamePath}
}
function global:Start-Process {throw 'Real process launching is forbidden'}
function global:Start-Sleep {throw 'Live process waiting is forbidden'}
function global:python {
    $global:MockCalls++
    $exists=Test-Path -LiteralPath $global:MarkerPath
    $bytes=if($exists){[Convert]::ToBase64String([IO.File]::ReadAllBytes($global:MarkerPath))}else{$null}
    $record=[ordered]@{arguments=@($args);marker_exists=$exists;marker_bytes=$bytes;cwd=(Get-Location).Path}
    [IO.File]::WriteAllText($global:InvocationPath,($record | ConvertTo-Json -Depth 5))
    if($global:MockMode -eq 'throw'){throw 'Synthetic Python exception'}
    $global:LASTEXITCODE=if($global:MockMode -eq 'nonzero'){7}else{0}
}
$tokens=$null;$parseErrors=$null
[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $CasePath 'tools\run_demo.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw ('Launcher parse failed: '+($parseErrors | Out-String))}
$launch=@{GamePid=424242;CandidateManifest=(Join-Path $CasePath 'artifacts\candidate.json');SkipPreflightWait=$true}
if($case.reject -eq 'NoArms'){$launch.NoArms=$true}
if($case.reject -eq 'Simulated'){$launch.Simulated=$true}
$before=(Get-Location).Path
$failure=$null
try {& (Join-Path $CasePath 'tools\run_demo.ps1') @launch} catch {$failure=$_.Exception.Message}
$exists=Test-Path -LiteralPath $global:MarkerPath
$bytes=if($exists){[Convert]::ToBase64String([IO.File]::ReadAllBytes($global:MarkerPath))}else{$null}
$result=[ordered]@{failure=$failure;marker_exists=$exists;marker_bytes=$bytes;before=$before;after=(Get-Location).Path;parse_errors=$parseErrors.Count;mock_calls=$global:MockCalls}
[IO.File]::WriteAllText((Join-Path $CasePath 'result.json'),($result | ConvertTo-Json -Depth 5))
'''
(OUTPUT / 'harness.ps1').write_text(harness, encoding='utf-8')
cases = []
for enabled in [False, True]:
    for existing in [False, True]:
        for mode in ['success', 'throw', 'nonzero']:
            cases.append(dict(enabled=enabled, existing=existing, mode=mode, reject=''))
for rejection in ['NoArms', 'Simulated', 'missing-fullbody']:
    cases.append(dict(enabled=True, existing=True, mode='success', reject=rejection))

results = []
for number, case in enumerate(cases, 1):
    folder = OUTPUT / f'{number:02d}'
    for name in ['tools', 'build', 'artifacts', 'empty-bin']:
        (folder / name).mkdir(parents=True, exist_ok=True)
    (folder / 'tools/run_demo.ps1').write_bytes(original_script)
    (folder / 'tools/inject_probe.py').write_text('raise AssertionError("Must never execute")\n', encoding='utf-8')
    game = folder / 'Dishonored2.exe'; game.write_bytes(b'owned synthetic game bytes, not executable')
    dll = folder / 'build/d2vr_render_probe_arm_v999999.dll'; dll.write_bytes(b'owned synthetic dll, not loadable')
    if case['reject'] != 'missing-fullbody':
        (folder / 'build/full-body').write_bytes(b'enabled\r\n')
    marker = folder / 'build/full-hand-query'
    if case['existing']:
        marker.write_bytes(marker_bytes)
    manifest = dict(dll_name=dll.name, sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                    game_sha256=hashlib.sha256(game.read_bytes()).hexdigest(),
                    full_hand_response_enabled=case['enabled'], launch_blocked=False)
    (folder / 'artifacts/candidate.json').write_text(json.dumps(manifest), encoding='utf-8')
    (folder / 'case.json').write_text(json.dumps(case), encoding='utf-8')
    process = subprocess.run([POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                              '-File', str(OUTPUT / 'harness.ps1'), '-CasePath', str(folder)],
                             cwd=folder, env=environment, capture_output=True, text=True, timeout=20)
    (folder / 'powershell.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    assert process.returncode == 0, (case, process.returncode, process.stdout, process.stderr)
    result = json.loads((folder / 'result.json').read_text(encoding='utf-8-sig'))
    assert result['parse_errors'] == 0, (case, result)
    assert result['before'] == result['after'], (case, result)
    assert result['marker_exists'] == case['existing'], (case, result)
    assert marker.exists() == case['existing'], (case, result)
    assert result['marker_bytes'] == (base64.b64encode(marker_bytes).decode() if case['existing'] else None), (case, result)
    if case['existing']:
        assert marker.read_bytes() == marker_bytes, case
    invocation = folder / 'invocation.json'
    if case['reject']:
        assert result['mock_calls'] == 0, (case, result)
        assert result['failure'] and 'requires real tracked arms' in result['failure'], (case, result)
        assert not invocation.exists(), (case, result)
    else:
        assert result['mock_calls'] == 1, (case, result)
        assert invocation.exists(), (case, result)
        called = json.loads(invocation.read_text(encoding='utf-8-sig'))
        expected_marker = b'on' if case['enabled'] else (marker_bytes if case['existing'] else None)
        assert called['marker_exists'] == (expected_marker is not None), (case, called)
        assert called['marker_bytes'] == (base64.b64encode(expected_marker).decode() if expected_marker is not None else None), (case, called)
        assert Path(called['cwd']) == folder, (case, called)
        arguments = called['arguments']
        assert Path(arguments[0]) == folder / 'tools/inject_probe.py', (case, arguments)
        assert arguments[arguments.index('--pid') + 1] == '424242', (case, arguments)
        assert '--tracked-arms' in arguments and '--until-stopped' in arguments, (case, arguments)
        assert '--simulated-vr' not in arguments and '--seconds' not in arguments, (case, arguments)
        assert '--wait-for-headset' not in arguments and '--wait-for-game' not in arguments, (case, arguments)
        if case['mode'] == 'success':
            assert result['failure'] is None, (case, result)
        elif case['mode'] == 'throw':
            assert result['failure'] == 'Synthetic Python exception', (case, result)
        else:
            assert 'Demo diagnostic failed (7)' in result['failure'], (case, result)
    assert (folder / 'tools/run_demo.ps1').read_bytes() == original_script, case
    results.append(dict(case=case, result=result))

assert SCRIPT.read_bytes() == original_script, 'Source launcher changed during the check'
(OUTPUT / 'results.json').write_text(json.dumps(dict(cases=results, script_sha256=hashlib.sha256(original_script).hexdigest(),
    test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), scope='Copied real PowerShell launcher with synthetic files and mocked Python/Get-Process; no game, injection or capture'), indent=2), encoding='utf-8')
print(f'PASS: {len(cases)} actual PowerShell launcher cases, parse, exact binary restoration, scoped marker and fail-before-Python guards; evidence {OUTPUT}')

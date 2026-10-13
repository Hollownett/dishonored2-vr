param(
    [switch]$Launch,
    [switch]$Stop,
    [switch]$Direct,
    [string]$CandidateManifest="",
    [switch]$SynchronizedDiagnostics,
    [ValidateRange(0,2147483647)][int]$GamePid=0,
    [ValidateRange(0,300)][int]$Seconds=0
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskManifestPath=if ($CandidateManifest) { [IO.Path]::GetFullPath($CandidateManifest) } else { Join-Path $taskRoot 'artifacts\repair-v220-test-candidate.json' }
if ($Stop -and ($Launch -or $Direct -or $Seconds -or $SynchronizedDiagnostics)) { throw '-Stop accepts only -GamePid; do not combine it with launch, direct or duration options.' }
if ($Stop -and -not $GamePid) { throw '-Stop requires the exact running game PID: -Stop -GamePid <PID>.' }
if ($Launch -and $GamePid) { throw '-Launch selects the single game process; use -GamePid for an already-running game.' }
if ($Seconds -and $Seconds -lt 10) { throw 'Timed test sessions require at least 10 seconds.' }
$taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
if ($taskManifest.dll_name -cnotmatch '^d2vr_render_probe_arm_v[0-9]+\.dll$') { throw 'The repair manifest must select a numbered experimental arm DLL.' }
if ($taskManifest.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $taskManifest.game_sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'The candidate manifest requires verified lowercase DLL and game SHA256 values.' }
$taskDll=Join-Path $taskRoot ('build\'+$taskManifest.dll_name)
if ((Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskManifest.sha256) { throw 'The selected DLL differs from its candidate manifest; no action was taken.' }
Write-Host ('Experimental repair test: '+$taskManifest.dll_name+'. Preserved candidates and saved settings remain separate.')
if ($Stop) {
    $taskGames=@(Get-Process -Name Dishonored2 -ErrorAction SilentlyContinue)
    if ($taskGames.Count -ne 1 -or $taskGames[0].Id -ne $GamePid) { throw 'Stop requires exactly one Dishonored2 process matching the specified -GamePid.' }
    $taskGame=$taskGames[0]
    if ((Get-FileHash -LiteralPath $taskGame.Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskManifest.game_sha256) { throw 'The running game differs from the pinned executable.' }
    $taskLoaded=@($taskGame.Modules | Where-Object { [IO.Path]::GetFileName($_.FileName) -ieq $taskManifest.dll_name })
    if ($taskLoaded.Count -ne 1 -or [IO.Path]::GetFullPath($taskLoaded[0].FileName) -ine [IO.Path]::GetFullPath($taskDll)) { throw 'The exact manifest DLL is not loaded from this workspace in the specified game.' }
    Push-Location -LiteralPath $taskRoot
    try {
        & python (Join-Path $PSScriptRoot 'inject_probe.py') --pid $GamePid --dll-name $taskManifest.dll_name --stop
        if ($LASTEXITCODE -ne 0) { throw "Stop request failed ($LASTEXITCODE); inspect the message above. No new session was started." }
        Write-Host 'Stop requested; projection/input cleanup is asynchronous. If paused material restoration remains pending, resume native gameplay so the next owned body draw can restore it. Start remains blocked while restoration is pending.'
        Write-Host 'Check the archived restoration result; a stop request alone does not establish clean restoration.'
    } finally { Pop-Location }
    return
}
if ($taskManifest.launch_blocked) { throw $taskManifest.launch_blocked_reason }
$taskOptions=@{ CandidateManifest=$taskManifestPath; Seconds=$Seconds }
if ($GamePid) { $taskOptions.GamePid=$GamePid }
if ($Launch) { $taskOptions.Launch=$true }
if ($Direct) { $taskOptions.SkipPreflightWait=$true }
$taskDiagnosticFlag=Join-Path $taskRoot 'build\synchronized-diagnostics'
$taskHadDiagnosticFlag=Test-Path -LiteralPath $taskDiagnosticFlag
$taskDiagnosticBytes=if ($taskHadDiagnosticFlag) { [IO.File]::ReadAllBytes($taskDiagnosticFlag) } else { $null }
try {
    if ($SynchronizedDiagnostics) { [IO.File]::WriteAllText($taskDiagnosticFlag, '1') }
    & (Join-Path $PSScriptRoot 'run_demo.ps1') @taskOptions
} finally {
    if ($SynchronizedDiagnostics) {
        if ($taskHadDiagnosticFlag) { [IO.File]::WriteAllBytes($taskDiagnosticFlag, $taskDiagnosticBytes) }
        else { Remove-Item -LiteralPath $taskDiagnosticFlag -ErrorAction SilentlyContinue }
    }
}

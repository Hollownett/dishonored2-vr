param(
    [int]$GamePid=0,
    [ValidateRange(0,300)][int]$Seconds=0,
    [switch]$Simulated,
    [switch]$NoArms,
    [switch]$Fsr4,
    [switch]$Launch,
    [string]$CandidateManifest='',
    [switch]$SkipPreflightWait
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskManifestPath=if($CandidateManifest){$CandidateManifest}else{Join-Path $taskRoot 'artifacts\demo-candidate.json'}
$taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
if ($taskManifest.launch_blocked) { throw $taskManifest.launch_blocked_reason }
if ($taskManifest.dll_name -cnotmatch '^d2vr_render_probe_arm_v[0-9]+\.dll$') { throw 'Invalid candidate filename.' }
$taskDll=Join-Path $taskRoot ('build\'+$taskManifest.dll_name)
if ((Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskManifest.sha256) {
    throw 'Candidate DLL changed since validation. Build and validate a fresh version before updating the candidate.'
}
$taskGames=@(Get-Process -Name Dishonored2 -ErrorAction SilentlyContinue)
if ($Launch -and -not $taskGames.Count) {
    # Steam app 403640 is Dishonored 2. VR starts once a save is loaded (native gameplay camera required).
    Start-Process 'steam://rungameid/403640'
    Write-Host 'Starting Dishonored 2. Load a save on the desktop; VR starts automatically once you are in the game.'
    $taskDeadline=(Get-Date).AddMinutes(3)
    while (-not $taskGames.Count -and (Get-Date) -lt $taskDeadline) { Start-Sleep -Seconds 2; $taskGames=@(Get-Process -Name Dishonored2 -ErrorAction SilentlyContinue) }
    Start-Sleep -Seconds 10
}
if ($GamePid) { $taskGames=@($taskGames | Where-Object Id -EQ $GamePid) }
if ($taskGames.Count -ne 1) { throw 'Start Dishonored 2 and enter gameplay or its pause menu; exactly one process is required.' }
if ((Get-FileHash -LiteralPath $taskGames[0].Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskManifest.game_sha256) {
    throw 'The running game executable differs from the pinned Steam build.'
}
if ($Seconds -and $Seconds -lt 10) { throw 'Timed sessions require at least 10 seconds.' }
$taskArguments=@('--pid', $taskGames[0].Id.ToString(), '--paired', '--native-eye-capture', '--dll-name', $taskManifest.dll_name,
    '--vr-preview', '--head-tracking', '--controllers', '--symmetric-frustum', '--eye-antialiasing',
    '--native-menu-pointer', '--stable-camera', '--vr-turning', 'snap', '--physical-grab')
# The -Simulated smoke test keeps its fixture's own grip timeline; headset sessions get hands and hand aim.
if (-not $Simulated) { $taskArguments+=@('--native-left-aim-experiment','--controller-aim'); if (-not $NoArms) { $taskArguments+='--tracked-arms' } }
if ($Simulated) {
    if (-not $Seconds) { $Seconds=12 }
    $taskArguments+=@('--simulated-vr','--simulated-input','--seconds',$Seconds.ToString())
    Write-Host 'Development test: simulated Quest poses/actions drive the actual game and native objects.'
} else {
    $taskArguments+=@('--headset-size','--saved-settings')
    if(-not $SkipPreflightWait) {$taskArguments+=@('--wait-for-headset','--wait-for-game')}
    if ($Seconds) { $taskArguments+=@('--seconds',$Seconds.ToString()) }
    else { $taskArguments+='--until-stopped' }
    Write-Host 'Dishonored 2 VR: Ctrl+C stops VR and restores the game. -NoArms hides tracked hands.'
}
Push-Location -LiteralPath $taskRoot
$taskFsr4Flag=Join-Path $taskRoot 'build\fsr4'
$taskFsr4Existed=Test-Path -LiteralPath $taskFsr4Flag
[byte[]]$taskFsr4Before=if($taskFsr4Existed){[IO.File]::ReadAllBytes($taskFsr4Flag)}else{$null}
try {
    if($Fsr4) {[IO.File]::WriteAllText($taskFsr4Flag,'on')}
    & python (Join-Path $PSScriptRoot 'inject_probe.py') @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "Demo diagnostic failed ($LASTEXITCODE); inspect the archived run." }
} finally {
    if($Fsr4) {if($taskFsr4Existed){[IO.File]::WriteAllBytes($taskFsr4Flag,$taskFsr4Before)}else{Remove-Item -LiteralPath $taskFsr4Flag -ErrorAction SilentlyContinue}}
    Pop-Location
}

param([switch]$Launch,[switch]$Stop,[switch]$Direct)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskManifestPath=Join-Path $taskRoot 'artifacts\claude-v217-test-candidate.json'
if($Stop) {
    $taskGames=@(Get-Process -Name Dishonored2 -ErrorAction SilentlyContinue)
    if($taskGames.Count -ne 1) {throw 'Exactly one Dishonored 2 process is required.'}
    Push-Location -LiteralPath $taskRoot
    try {
        & python (Join-Path $PSScriptRoot 'inject_probe.py') --pid $taskGames[0].Id --stop --dll-name d2vr_render_probe_arm_v217.dll
        if($LASTEXITCODE -ne 0) {throw 'VR stop failed; inspect the session log.'}
    } finally {Pop-Location}
} else {
    Write-Host 'Experimental Claude v217 full-body test. Exit the game after testing to clear body material/visibility changes.'
    & (Join-Path $PSScriptRoot 'run_demo.ps1') -CandidateManifest $taskManifestPath -Launch:$Launch -SkipPreflightWait:$Direct
}

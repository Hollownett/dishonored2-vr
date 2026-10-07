param([switch]$Candidate,[switch]$Pipeline,[switch]$Paired,[switch]$ViewSetup)
$ErrorActionPreference = 'Stop'
if (@($Candidate,$Pipeline,$Paired,$ViewSetup).Where({$_.IsPresent}).Count -gt 1) { throw 'Choose one diagnostic output.' }
$taskProject = Split-Path -Parent $PSScriptRoot
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVsWhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$taskVsRoot = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'No installed x64 C++ toolchain found.' }
$env:D2VR_VS_ROOT = $taskVsRoot.Trim()
Push-Location -LiteralPath $taskProject
try {
    if ($ViewSetup) { & $env:ComSpec /d /c 'tools\build-msvc.cmd view_setup' }
    elseif ($Paired) { & $env:ComSpec /d /c 'tools\build-msvc.cmd paired' }
    elseif ($Pipeline) { & $env:ComSpec /d /c 'tools\build-msvc.cmd pipeline' }
    elseif ($Candidate) { & $env:ComSpec /d /c 'tools\build-msvc.cmd candidate' }
    else { & $env:ComSpec /d /c tools\build-msvc.cmd }
    if ($LASTEXITCODE -ne 0) { throw "C++ build/check failed: $LASTEXITCODE" }
} finally {
    Pop-Location
    Remove-Item Env:\D2VR_VS_ROOT -ErrorAction SilentlyContinue
}

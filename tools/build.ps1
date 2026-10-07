param([switch]$Candidate)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVsWhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$taskVsRoot = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'No installed x64 C++ toolchain found.' }
$env:D2VR_VS_ROOT = $taskVsRoot.Trim()
Push-Location -LiteralPath $taskProject
try {
    if ($Candidate) { & $env:ComSpec /d /c 'tools\build-msvc.cmd candidate' }
    else { & $env:ComSpec /d /c tools\build-msvc.cmd }
    if ($LASTEXITCODE -ne 0) { throw "C++ build/check failed: $LASTEXITCODE" }
} finally {
    Pop-Location
    Remove-Item Env:\D2VR_VS_ROOT -ErrorAction SilentlyContinue
}

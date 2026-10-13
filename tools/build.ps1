param([switch]$Candidate,[switch]$Pipeline,[switch]$Paired,[switch]$ViewSetup,[string]$OutputName='')
$ErrorActionPreference = 'Stop'
if (@($Candidate,$Pipeline,$Paired,$ViewSetup).Where({$_.IsPresent}).Count -gt 1) { throw 'Choose one diagnostic output.' }
if ($OutputName -and $OutputName -cnotmatch '^d2vr_render_probe(_[a-z0-9_]+)?\.dll$') { throw 'OutputName must be a diagnostic DLL filename with lowercase letters, digits and underscores.' }
$taskProject = Split-Path -Parent $PSScriptRoot
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVsWhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$taskVsRoot = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'No installed x64 C++ toolchain found.' }
$env:D2VR_VS_ROOT = $taskVsRoot.Trim()
Push-Location -LiteralPath $taskProject
try {
    $taskBuildMode = if ($ViewSetup) {'view_setup'} elseif ($Paired) {'paired'} elseif ($Pipeline) {'pipeline'} elseif ($Candidate) {'candidate'} else {'standard'}
    $taskDiagnosticName = if ($OutputName) { $OutputName } else { switch ($taskBuildMode) { 'view_setup' {'d2vr_render_probe_view_setup.dll'} 'paired' {'d2vr_render_probe_paired.dll'} 'pipeline' {'d2vr_render_probe_pipeline.dll'} 'candidate' {'d2vr_render_probe_candidate.dll'} default {'d2vr_render_probe.dll'} } }
    if ($taskDiagnosticName -cmatch '_v[0-9]+\.dll$' -and (Test-Path -LiteralPath (Join-Path $taskProject "build\$taskDiagnosticName"))) { throw 'Numbered candidates are preserved. Choose a fresh version; no build was started.' }
    $taskPython = (Get-Command python -ErrorAction Stop).Source
    $taskScriptArgs = @('tools/build.ps1')
    if ($Candidate) { $taskScriptArgs += '-Candidate' }
    if ($Pipeline) { $taskScriptArgs += '-Pipeline' }
    if ($Paired) { $taskScriptArgs += '-Paired' }
    if ($ViewSetup) { $taskScriptArgs += '-ViewSetup' }
    if ($OutputName) { $taskScriptArgs += @('-OutputName', $OutputName) }
    $taskCompilerArgs = @('tools/build-msvc.cmd', $taskBuildMode, $taskDiagnosticName)
    $taskArchiveArgs = @('begin', '--root', $taskProject, '--dll-name', $taskDiagnosticName, '--mode', $taskBuildMode)
    foreach ($taskArg in $taskScriptArgs) { $taskArchiveArgs += "--script-arg=$taskArg" }
    foreach ($taskArg in $taskCompilerArgs) { $taskArchiveArgs += "--compiler-arg=$taskArg" }
    $taskSessionOutput = & $taskPython (Join-Path $PSScriptRoot 'archive_build_source.py') @taskArchiveArgs
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the pre-build source archive.' }
    $taskSessionJson = $taskSessionOutput -join "`n"
    $taskSession = ($taskSessionJson | ConvertFrom-Json).session
    if (-not $taskSession) { throw 'The source archive did not return a build session.' }
    & $env:ComSpec /d /c "tools\build-msvc.cmd $taskBuildMode $taskDiagnosticName"
    $taskBuildExitCode = $LASTEXITCODE
    if ($taskBuildExitCode -ne 0) {
        & $taskPython (Join-Path $PSScriptRoot 'archive_build_source.py') failed --root $taskProject --session $taskSession --exit-code $taskBuildExitCode | Out-Null
        throw "C++ build/check failed: $taskBuildExitCode"
    }
    $taskFinished = & $taskPython (Join-Path $PSScriptRoot 'archive_build_source.py') finish --root $taskProject --session $taskSession --dll (Join-Path $taskProject "build\$taskDiagnosticName")
    if ($LASTEXITCODE -ne 0) { throw 'Build completed, but compiler source changed during the build; no provenance pointer was published.' }
} finally {
    Pop-Location
    Remove-Item Env:\D2VR_VS_ROOT -ErrorAction SilentlyContinue
}

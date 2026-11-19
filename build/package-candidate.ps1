param([Parameter(Mandatory)][string]$Version,[string]$ReleaseNote,[string]$SourceCommit,[string]$SimEvidence,[string[]]$Limitations)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$rel  = 'C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags'   # release line: source of gitignored binaries (FSR4/OpenXR/dinput8 dlls)
$note = 'Test candidate v1384 = v1383 + hang fix: the game froze after Stilton''s mission. A game caller kept a physics body id from the previous level and switched the new level''s body in that slot (a trigger) to dynamic through setBodyMotionType 0xc86800; destroying the trigger then looped forever. The mod''s guard on 0xc86800 now skips an id whose slot holds another live body. Unit-checked; not headset-tested, not simulated.'
# The note above is v1384's; pass -ReleaseNote (PowerShell names ignore case: not -Note, that IS $note), or an existing manifest's "state" is reused (2026-11-19).
$name = "d2vr_render_probe_arm_v$Version.dll"
$dll  = Join-Path $repo "build\$name"
if ($Version -notmatch '^\d+$') { throw "version must be digits" }
if (-not (Test-Path $dll)) { throw "missing $dll" }
$sha = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower()
$commit = if ($SourceCommit) { $SourceCommit } else { (git -C $repo rev-parse --short HEAD).Trim() }
$dirty  = if ($SourceCommit) { $false } else { [bool](git -C $repo status --porcelain --untracked-files=no) }
$art = Join-Path $repo 'artifacts'; New-Item -ItemType Directory -Force $art | Out-Null
$manifest = Join-Path $art "repair-v$Version-test-candidate.json"
if ($ReleaseNote) { $note = $ReleaseNote } elseif (Test-Path $manifest) { $note = (Get-Content $manifest -Raw | ConvertFrom-Json).state }
if (-not $SimEvidence) { $SimEvidence = 'none' }
if (-not $Limitations) { $Limitations = @('Not headset-tested and not simulated.') }
[ordered]@{
  schema=1; version=[int]$Version; state=$note; dll_name=$name; sha256=$sha; dll_sha256=$sha
  dll_bytes=(Get-Item $dll).Length; game_sha256='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
  source_commit=$commit; source_dirty=$dirty; build_host='Windows MSVC 2022 x64 (build/compile-v'+$Version+'.cmd)'
  full_required_build_passed=$false; launch_blocked=$false; headset_validated=$false; headset_tested=$false
  sim_evidence=$SimEvidence; limitations=@($Limitations)
} | ConvertTo-Json | Set-Content $manifest -Encoding UTF8

# Stage a repo-shaped tree (the csproj embeds files relative to launcher\..); this worktree lacks the gitignored dlls.
$s = Join-Path $repo 'build\pkg-stage'
if (Test-Path $s) { Remove-Item $s -Recurse -Force }
New-Item -ItemType Directory -Force "$s\launcher","$s\artifacts","$s\build","$s\tools","$s\docs" | Out-Null
robocopy "$repo\launcher" "$s\launcher" /E /XD dist obj bin /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy launcher failed" }
foreach ($f in 'tools\default_features.json','docs\VR-CONTROLS.md','THIRD_PARTY_NOTICES.md') {
  New-Item -ItemType Directory -Force (Split-Path "$s\$f") | Out-Null; Copy-Item "$repo\$f" "$s\$f" }
foreach ($f in 'fsr4\licenses\license.md','fsr4\licenses\3rdpartynotice.md','openxr\bin\LICENSE','openxr\bin\source.json','minhook\LICENSE.txt','smaa\LICENSE.txt','fidelityfx\LICENSE.txt','dlss\LICENSE.txt','dishonored_vr\LICENSE') {
  New-Item -ItemType Directory -Force (Split-Path "$s\third_party\$f") | Out-Null; Copy-Item "$repo\third_party\$f" "$s\third_party\$f" }
foreach ($f in 'fsr4\bin\amd_fidelityfx_upscaler_dx12.dll','fsr4\bin\amd_fidelityfx_loader_dx12.dll','openxr\bin\openxr_loader.dll') {
  New-Item -ItemType Directory -Force (Split-Path "$s\third_party\$f") | Out-Null
  $src = if (Test-Path "$repo\third_party\$f") { "$repo\third_party\$f" } else { "$rel\third_party\$f" }
  Copy-Item $src "$s\third_party\$f" }
$d8 = if (Test-Path "$rel\build\dinput8.dll") { "$rel\build\dinput8.dll" } else { "$repo\build\dinput8.dll" }   # this PC has no release-line folder
Copy-Item $d8 "$s\build\dinput8.dll"
Copy-Item $dll "$s\build\$name"; Copy-Item $manifest "$s\artifacts\"
if (Test-Path "$dll.source.json") { Copy-Item "$dll.source.json" "$s\build\" }

Push-Location "$s\launcher"
try { dotnet publish -c Release "-p:LauncherVersion=v$Version-test"; if ($LASTEXITCODE) { throw "dotnet publish failed" } } finally { Pop-Location }
$exe = "$s\launcher\dist\DishonoredVRLauncher.exe"
if (-not (Test-Path $exe)) { throw "no exe produced" }

# Embed check: load the pre-bundle managed assembly (resources stored raw) and hash the DLL resource.
$asm = Get-ChildItem "$s\launcher\obj" -Recurse -Filter DishonoredVRLauncher.dll | Where-Object { $_.FullName -match 'Release' -and $_.FullName -notmatch 'ref' } | Select-Object -First 1
if (-not $asm) { throw "built assembly not found" }
$a = [Reflection.Assembly]::Load([IO.File]::ReadAllBytes($asm.FullName))   # LoadFile refuses a .claude\worktrees path ("network location")
$st = $a.GetManifestResourceStream("home/build/$name"); if (-not $st) { throw "DLL resource missing in assembly" }
$h = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($st)).Replace('-','').ToLower()
if ($h -ne $sha) { throw "embedded DLL sha $h != $sha" }
$ms = $a.GetManifestResourceNames() | Where-Object { $_ -like 'home/artifacts/repair-v*' }
if (@($ms).Count -ne 1 -or $ms -ne "home/artifacts/repair-v$Version-test-candidate.json") { throw "unexpected manifest resource(s): $ms" }
Write-Host "embed check OK: $name sha256 $h, manifest $ms"

New-Item -ItemType Directory -Force "$repo\launcher\dist" | Out-Null
$prev = "$repo\launcher\dist\DishonoredVRLauncher.exe"
$backup = "$repo\launcher\dist\DishonoredVRLauncher.before-v$Version.exe"   # first build of a version only: a rebuild must not replace it
if ((Test-Path $prev) -and -not (Test-Path $backup)) { Copy-Item $prev $backup }
Copy-Item $exe "$repo\launcher\dist\DishonoredVRLauncher.exe" -Force
$out = "$repo\launcher\dist\DishonoredVRLauncher.exe"
Write-Host "EXE: $out  ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"
Write-Host "EXE sha256: $((Get-FileHash $out -Algorithm SHA256).Hash.ToLower())"











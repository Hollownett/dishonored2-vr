param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$rel  = 'C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags'   # release line: source of gitignored binaries (FSR4/OpenXR/dinput8 dlls)
$note = 'Test candidate v1365 = v1381 (v1380 + v1363 + v1371) with physically held props in stereo drawn as in the release v1297. User, v1381 stereo: held props, the sticky grenade and the stun mine render badly; v1297 was fine. Sim on the save of the user (the teapot the user held): v1381 showed the teapot in the hand in 2 of 16 walking frames, v1297 in 16 of 16. Cause: the stale-view match of v1320 (widened in v1363) placed draws through a view record that was another camera 44 deg away from the image they are drawn into, so the prop was drawn away from the hand. It is now off (build/stale-view-match turns it back on); those draws show the engine pose, which is the drawn hold. Same sim on v1381 with the match off: 16 of 16; on v1365: 16 of 16 in the right hand, steady in the left hand too, and the teapot in the hand in all 6 eye pairs (v1381: empty hand at 4, 8 and 10.5 s). The gadgets are unchanged (the sim could not reproduce them: an equipped gadget lands in the left hand there and looks steady). Diagnostics on: artifacts/eye-records.csv and more columns in held-draw-trace.csv. Not headset-tested.'
$name = "d2vr_render_probe_arm_v$Version.dll"
$dll  = Join-Path $repo "build\$name"
if ($Version -notmatch '^\d+$') { throw "version must be digits" }
if (-not (Test-Path $dll)) { throw "missing $dll" }
$sha = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower()
$commit = (git -C $repo rev-parse --short HEAD).Trim()
$dirty  = [bool](git -C $repo status --porcelain)
$art = Join-Path $repo 'artifacts'; New-Item -ItemType Directory -Force $art | Out-Null
$manifest = Join-Path $art "repair-v$Version-test-candidate.json"
[ordered]@{
  schema=1; version=[int]$Version; state=$note; dll_name=$name; sha256=$sha; dll_sha256=$sha
  dll_bytes=(Get-Item $dll).Length; game_sha256='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
  source_commit=$commit; source_dirty=$dirty; build_host='Windows MSVC 2022 x64 (build/compile-v'+$Version+'.cmd)'
  full_required_build_passed=$false; launch_blocked=$false; headset_validated=$false; headset_tested=$false
  sim_evidence='none'; limitations=@('Not headset-tested and not simulated.')
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
Copy-Item "$rel\build\dinput8.dll" "$s\build\dinput8.dll"
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
Copy-Item $exe "$repo\launcher\dist\DishonoredVRLauncher.exe" -Force
$out = "$repo\launcher\dist\DishonoredVRLauncher.exe"
Write-Host "EXE: $out  ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"
Write-Host "EXE sha256: $((Get-FileHash $out -Algorithm SHA256).Hash.ToLower())"











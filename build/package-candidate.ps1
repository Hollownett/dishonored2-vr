param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$rel  = 'C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags'   # release line: source of gitignored binaries (FSR4/OpenXR/dinput8 dlls)
$note = 'Test candidate v1383 = v1381 + v1365-v1367 + v1372-v1374. (1) Stereo, physically held props: drawn in the hand in every frame. The eye camera record was first taken at the start of the view Setup, while it still held the flat game camera, so about half the renders placed or refused held things with the wrong camera (v1320 stale-view match drew the prop away from the hand). Records are now taken at the Setup exit and cached views are rebuilt when a record changes; the stale-view match is off. Sim (teapot the user held): v1381 showed it in the hand in 2 of 16 walking frames, v1367 in all frames with 0 unplaced renders. (2) Stereo, sticky grenade and stun mine in the right hand: the engine bind of a crossed gadget turned it up to 152 deg; a binding is now used only within 45 deg of where the game draws the weapon, else the game pose (as the release). Sim: mine and grenade steady in all 16 frames (v1365 flipped them). (3) Bolts: a crossbow or pistol chosen into a hand shows the bolt that hand chose. (4) Switching guns in one hand starts each gun from its own remembered grip. (5) A dual-wield gun copy stays in the hand at walls and furniture (no barrel wall response for copies). Knobs: legacy-eye-record-entry, legacy-views-cache, no-bind-gate, stale-view-match, copy-blade-collision. Not headset-tested.'
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











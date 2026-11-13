@echo off
rem v1284 = v1283 + plan 2026-11-13 phases B, C, D: near-draw path cheap reject (match spheres before any inverse/lock; no-weapon-near-cull),
rem  per-thread VP inverse cache, lock-free held-frames snapshot and rig-target book cache, diagnostics-only census/hand_lock/match counters;
rem  writer notes carried by their arm model to the Setup frame before the drawn-hand check (no-weapon-note-carry), unbound weapons published
rem  at B = F (no-weapon-provisional), one-view own previous rows from the eye's previous render at any gap; Held::timepiece, only a power
rem  changes arms (POWER SWAP / v1090), superseded wheel tickets held over; bolt ownership rules factored (pure), instance ammo forgotten on a load.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_arm_v1284.dll
set L=C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags\third_party
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:build\dxgi_probe_v1284.pdb /MD /LD /I. /FI"src\dlss_eye_upscaler.hpp" /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\dxgi_probe_v1284.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link /MAP:build\d2vr_render_probe_arm_v1284.map /DEBUG /OPT:REF /OPT:ICF %L%\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib %L%\fidelityfx\libs\ffx_backend_dx11_x64.lib %L%\fidelityfx\libs\ffx_fsr3_x64.lib %L%\fidelityfx\libs\ffx_fsr3upscaler_x64.lib %L%\fidelityfx\libs\ffx_frameinterpolation_x64.lib %L%\fidelityfx\libs\ffx_opticalflow_x64.lib
echo dll_exit=%errorlevel%








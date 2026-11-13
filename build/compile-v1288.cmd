@echo off
rem v1288 = v1288 + the UAV replay mask as an R32_UINT key (InterlockedMax depth<<2|class, decoded and zeroed by the rig copy at u2):
rem  plain mask stores of different replayed draws land in no fixed order (the frayed held Heart in the v1288 rebuilt eye);
rem  tests/rig_replay_uav_check.cpp thin held parts before the body, edges inside 2x2 quads.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_arm_v1288.dll
set L=C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags\third_party
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:build\dxgi_probe_v1288.pdb /MD /LD /I. /FI"src\dlss_eye_upscaler.hpp" /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\dxgi_probe_v1288.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link /MAP:build\d2vr_render_probe_arm_v1288.map /DEBUG /OPT:REF /OPT:ICF %L%\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib %L%\fidelityfx\libs\ffx_backend_dx11_x64.lib %L%\fidelityfx\libs\ffx_fsr3_x64.lib %L%\fidelityfx\libs\ffx_fsr3upscaler_x64.lib %L%\fidelityfx\libs\ffx_frameinterpolation_x64.lib %L%\fidelityfx\libs\ffx_opticalflow_x64.lib
echo dll_exit=%errorlevel%









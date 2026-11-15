@echo off
rem v1320 = v1319 + stereo weapon parts through their place on the weapon, no held_miss probe per draw, instanced held-part machinery only with legacy-part-batching
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_arm_v1320.dll
set L=C:\Users\hollo\Documents\ChatGPT\VR-mods-amd-ags\third_party
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:build\dxgi_probe_v1320.pdb /MD /LD /I. /FI"src\dlss_eye_upscaler.hpp" /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\dxgi_probe_v1320.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link /MAP:build\d2vr_render_probe_arm_v1320.map /DEBUG /OPT:REF /OPT:ICF %L%\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib %L%\fidelityfx\libs\ffx_backend_dx11_x64.lib %L%\fidelityfx\libs\ffx_fsr3_x64.lib %L%\fidelityfx\libs\ffx_fsr3upscaler_x64.lib %L%\fidelityfx\libs\ffx_frameinterpolation_x64.lib %L%\fidelityfx\libs\ffx_opticalflow_x64.lib
echo dll_exit=%errorlevel%
























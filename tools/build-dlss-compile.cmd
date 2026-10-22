@echo off
rem Compile/link ONLY: no executable, GPU test, NGX init or provider is run.
rem Local SDK /MD x64 library: NVIDIA/DLSS 9981c6350dc58c0ce21f024fd65e31e851c31689.
rem SHA256 bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c
rem See third_party\dlss\source.json. The library is intentionally gitignored.
if not exist third_party\dlss\lib\nvsdk_ngx_d.lib exit /b 1
powershell -NoProfile -NonInteractive -Command "if ((Get-FileHash -LiteralPath 'third_party\dlss\lib\nvsdk_ngx_d.lib' -Algorithm SHA256).Hash -ne 'bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c') { exit 1 }"
if errorlevel 1 exit /b 1
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Isrc tests\dlss_sdk_compile_check.cpp /Fo:build\dlss_sdk_compile_check.obj /Fe:build\dlss_sdk_compile_check.exe /link third_party\dlss\lib\nvsdk_ngx_d.lib d3d11.lib dxgi.lib advapi32.lib user32.lib uuid.lib
if errorlevel 1 exit /b 1
rem Duplicate the normal /MD production lane deliberately; NEVER call the normal
rem all-checks script here. Keep source/libs aligned with its final DLL command.
if not exist build\md mkdir build\md
cl /nologo /c /TC /W3 /O2 /MD /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fo:build\md\
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /bigobj /Zi /Fd:build\dxgi_probe.pdb /MD /LD /FIsrc\dlss_eye_upscaler.hpp /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\dxgi_probe.obj /Fe:build\d2vr_dlss_compile_only.dll /link /DEBUG /OPT:REF /OPT:ICF third_party\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib third_party\fidelityfx\libs\ffx_fsr3_x64.lib third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
exit /b %errorlevel%

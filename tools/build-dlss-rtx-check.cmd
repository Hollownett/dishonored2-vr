@echo off
rem Compile/link ONLY. Root explicitly runs the synthetic RTX NGX check separately.
rem No executable, provider, existing checks or production build is run here.
if not exist third_party\dlss\lib\nvsdk_ngx_d.lib exit /b 1
powershell -NoProfile -NonInteractive -Command "if ((Get-FileHash -LiteralPath 'third_party\dlss\lib\nvsdk_ngx_d.lib' -Algorithm SHA256).Hash -ne 'bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c') { exit 1 }"
if errorlevel 1 exit /b 1
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Isrc tests\dlss_rtx_gpu_check.cpp /Fo:build\dlss_rtx_gpu_check.obj /Fe:build\dlss_rtx_gpu_check.exe /link third_party\dlss\lib\nvsdk_ngx_d.lib d3d11.lib dxgi.lib advapi32.lib user32.lib uuid.lib
exit /b %errorlevel%

@echo off
rem Compile/link ONLY; root explicitly runs the NVIDIA hardware test separately.
rem No NGX SDK/provider dependency and no production/other-test execution.
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Isrc tests\d3d11_temporal_owner_gpu_check.cpp /Fo:build\d3d11_temporal_owner_gpu_check.obj /Fe:build\d3d11_temporal_owner_gpu_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib
exit /b %errorlevel%

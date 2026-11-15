@echo off
rem Fast mod-DLL build: compiles MinHook, Dear ImGui and src\dxgi_probe.cpp only, skipping the full check suite of build-msvc.cmd.
rem Usage: tools\build-dll-fast.cmd [output.dll]   (default build\d2vr_render_probe.dll)
rem The PureDark AFW backend needs no header or import library: PDAFWPlugin.dll is loaded at run time from the game folder.
setlocal
cd /d "%~dp0.."
if defined D2VR_VS_ROOT set "VSROOT=%D2VR_VS_ROOT%"
for %%e in (BuildTools Community Professional Enterprise) do (
  if not defined VSROOT if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e"
  if not defined VSROOT if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
)
if not defined VSROOT (echo No x64 C++ toolchain found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\md mkdir build\md
if not exist build\imgui mkdir build\imgui
set "OUT=build\d2vr_render_probe.dll"
if not "%~1"=="" set "OUT=build\%~1"
cl /nologo /c /TC /W3 /O2 /MD /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fo:build\md\ || exit /b 1
cl /nologo /c /std:c++17 /utf-8 /EHsc /W3 /O2 /MD /Ithird_party\imgui third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_dx11.cpp /Fo:build\imgui\ || exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:build\dxgi_probe.pdb /MD /LD /FI"%CD%\src\dlss_eye_upscaler.hpp" /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx /Ithird_party\imgui src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj build\imgui\imgui.obj build\imgui\imgui_draw.obj build\imgui\imgui_tables.obj build\imgui\imgui_widgets.obj build\imgui\imgui_impl_dx11.obj /Fo:build\dxgi_probe.obj /Fe:%OUT% /link /DEBUG /OPT:REF /OPT:ICF third_party\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib third_party\fidelityfx\libs\ffx_fsr3_x64.lib third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
exit /b %errorlevel%

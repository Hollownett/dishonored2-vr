@echo off
rem Port-target DLL (default: Death of the Outsider): regenerate the target's tree from src\ (tools\doto\port.py), then
rem compile it with the release flags of tools\build-msvc.cmd plus /FI src\game_target.hpp (the target's define).
rem Usage: tools\doto\build-doto.cmd <version>   ->  build\doto\d2vr_render_probe_arm_v<version>.dll
rem Another target: set D2VR_PORT_TARGET=<name> first (tools\doto\targets\<name>.json; its src_out and dll_out are used).
rem The DLL keeps the proxy's d2vr_render_probe_arm_v*.dll name; it lives in the target's mod home, never in build\.
setlocal
cd /d "%~dp0..\.."
set R=%CD%
set V=%1
if "%V%"=="" (echo usage: build-doto.cmd ^<version^> & exit /b 2)
python tools\doto\port.py || exit /b 1
set SRC=
set OUT=
for /f "usebackq tokens=1,* delims==" %%A in (`python tools\doto\target.py folders`) do set %%A=%%B
if not defined OUT (echo cannot read the target folders & exit /b 1)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist "%OUT%" mkdir "%OUT%"
pushd "%SRC%"
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:%OUT%\dxgi_probe_v%V%.pdb /MD /LD /FIsrc\game_target.hpp /FIsrc\dlss_eye_upscaler.hpp /I. /I%R% /I%R%\third_party\minhook\include /I%R%\third_party\openxr\include /I%R%\third_party\fidelityfx /I%R%\third_party\imgui /I%R%\src src\dxgi_probe.cpp %R%\build\imgui\imgui.obj %R%\build\imgui\imgui_draw.obj %R%\build\imgui\imgui_tables.obj %R%\build\imgui\imgui_widgets.obj %R%\build\imgui\imgui_impl_dx11.obj %R%\build\md\buffer.obj %R%\build\md\hook.obj %R%\build\md\trampoline.obj %R%\build\md\hde64.obj /Fo:%OUT%\dxgi_probe_v%V%.obj /Fe:%OUT%\d2vr_render_probe_arm_v%V%.dll /link /LIBPATH:%R% /MAP:%OUT%\d2vr_render_probe_arm_v%V%.map /DEBUG /OPT:REF /OPT:ICF %R%\third_party\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib %R%\third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib %R%\third_party\fidelityfx\libs\ffx_fsr3_x64.lib %R%\third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib %R%\third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib %R%\third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
set E=%errorlevel%
popd
echo dll_exit=%E%
exit /b %E%

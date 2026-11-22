@echo off
rem Death of the Outsider DLL: regenerate build\doto-src from src\ (tools\doto\port.py), then compile it with the
rem release flags of tools\build-msvc.cmd plus /FI src\game_doto.hpp (D2VR_GAME_DOTO=1).
rem Usage: tools\doto\build-doto.cmd <version>   ->  build\doto\d2vr_render_probe_arm_v<version>.dll
rem The DOTO DLL keeps the proxy's d2vr_render_probe_arm_v*.dll name; it lives in the DOTO mod home, never in build\.
setlocal
cd /d "%~dp0..\.."
set R=%CD%
set V=%1
if "%V%"=="" (echo usage: build-doto.cmd ^<version^> & exit /b 2)
python tools\doto\port.py || exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist build\doto mkdir build\doto
pushd build\doto-src
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /bigobj /Zi /Fd:%R%\build\doto\dxgi_probe_v%V%.pdb /MD /LD /FIsrc\game_doto.hpp /FIsrc\dlss_eye_upscaler.hpp /I. /I%R% /I%R%\third_party\minhook\include /I%R%\third_party\openxr\include /I%R%\third_party\fidelityfx /I%R%\third_party\imgui /I%R%\src src\dxgi_probe.cpp %R%\build\imgui\imgui.obj %R%\build\imgui\imgui_draw.obj %R%\build\imgui\imgui_tables.obj %R%\build\imgui\imgui_widgets.obj %R%\build\imgui\imgui_impl_dx11.obj %R%\build\md\buffer.obj %R%\build\md\hook.obj %R%\build\md\trampoline.obj %R%\build\md\hde64.obj /Fo:%R%\build\doto\dxgi_probe_v%V%.obj /Fe:%R%\build\doto\d2vr_render_probe_arm_v%V%.dll /link /LIBPATH:%R% /MAP:%R%\build\doto\d2vr_render_probe_arm_v%V%.map /DEBUG /OPT:REF /OPT:ICF %R%\third_party\dlss\lib\nvsdk_ngx_d.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib %R%\third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib %R%\third_party\fidelityfx\libs\ffx_fsr3_x64.lib %R%\third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib %R%\third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib %R%\third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
set E=%errorlevel%
popd
echo dll_exit=%E%
exit /b %E%

@echo off
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\arm_ik_check.cpp /Fo:build\arm_ik_check.obj /Fe:build\arm_ik_check.exe
if errorlevel 1 exit /b 1
build\arm_ik_check.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\game_camera_check.cpp /Fo:build\game_camera_check.obj /Fe:build\game_camera_check.exe
if errorlevel 1 exit /b 1
build\game_camera_check.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\game_stereo_check.cpp /Fo:build\game_stereo_check.obj /Fe:build\game_stereo_check.exe
if errorlevel 1 exit /b 1
build\game_stereo_check.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\tracked_pose_check.cpp /Fo:build\tracked_pose_check.obj /Fe:build\tracked_pose_check.exe
if errorlevel 1 exit /b 1
build\tracked_pose_check.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\openxr\include src\xr_probe.cpp /Fo:build\xr_probe.obj /Fe:build\xr_probe.exe /link d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
cl /nologo /c /TC /W3 /O2 /MT /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fo:build\
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\hook_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\hook_check.obj /Fe:build\hook_check.exe
if errorlevel 1 exit /b 1
build\hook_check.exe
if errorlevel 1 exit /b 1
set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe.dll
if "%~1"=="candidate" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_candidate.dll
if "%~1"=="pipeline" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_pipeline.dll
if "%~1"=="paired" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_paired.dll
if "%~1"=="view_setup" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_view_setup.dll
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /LD /Ithird_party\minhook\include src\dxgi_probe.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\dxgi_probe.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link d3d11.lib dxgi.lib bcrypt.lib user32.lib
exit /b %errorlevel%

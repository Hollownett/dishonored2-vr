@echo off
rem Compile tools\hand_pose_lab.cpp -> artifacts\hand-lab\bin\hand_pose_lab.exe, on demand (not part of build-msvc.cmd).
rem Same include paths and flags as the checks in tools\build-msvc.cmd, minus /WX: a warning in someone's unfinished src\ must not block the lab.
cd /d "%~dp0.."
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
if "%D2VR_VS_ROOT%"=="" set "D2VR_VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist artifacts\hand-lab\bin mkdir artifacts\hand-lab\bin
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /O2 /MT /Ithird_party\minhook\include /Ithird_party\openxr\include tools\hand_pose_lab.cpp /Fo:artifacts\hand-lab\bin\hand_pose_lab.obj /Fe:artifacts\hand-lab\bin\hand_pose_lab.exe /link user32.lib gdi32.lib Xinput9_1_0.lib d3d11.lib dxgi.lib d3dcompiler.lib
exit /b %errorlevel%

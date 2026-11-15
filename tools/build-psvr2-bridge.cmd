@echo off
rem Builds DVR_psvr2\dvr_psvr2_bridge.exe (third_party/psvr2/Bridge.cpp, protocol v2: both triggers) into build\.
setlocal
cd /d "%~dp0.."
for %%e in (BuildTools Community Professional Enterprise) do if not defined VSROOT if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\psvr2 mkdir build\psvr2
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE third_party\psvr2\Bridge.cpp /Fo:build\psvr2\ /Fe:build\dvr_psvr2_bridge.exe /link user32.lib

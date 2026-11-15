@echo off
rem v1163/v1327: builds tests\rig_mask_check.cpp (the rig mask replay and copy on WARP through the real RigMask code) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++20 /utf-8 /EHsc /O2 /I src tests\rig_mask_check.cpp /Fo:build\rig_mask_check.obj /Fe:build\rig_mask_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\rig_mask_check.exe
echo check_exit=%errorlevel%

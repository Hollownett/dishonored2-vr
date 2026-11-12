@echo off
rem v1243: builds tests\hand_lock_check.cpp (a held thing's pose in the drawn hand's joint-59 frame: image binding 0, old sample = velocity x age, late arm composition = player step) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 tests\hand_lock_check.cpp /Fo:build\hand_lock_check.obj /Fe:build\hand_lock_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\hand_lock_check.exe
echo check_exit=%errorlevel%

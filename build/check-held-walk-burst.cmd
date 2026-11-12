@echo off
rem v1249: builds testsheld_walk_burst_check.cpp (the held_walk_burst trigger state machine: arm, 16 frames, cooldown, one per kind, run tier) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 tests\held_walk_burst_check.cpp /Fo:build\held_walk_burst_check.obj /Fe:build\held_walk_burst_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\held_walk_burst_check.exe
echo check_exit=%errorlevel%

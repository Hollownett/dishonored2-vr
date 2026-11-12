@echo off
rem v1246: builds tests\secondary_view_guard_check.cpp (secondary-view/Timepiece lens guard: begin classification, camera follow test, replay gate, history spans) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /I src tests\secondary_view_guard_check.cpp /Fo:build\secondary_view_guard_check.obj /Fe:build\secondary_view_guard_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\secondary_view_guard_check.exe
echo check_exit=%errorlevel%

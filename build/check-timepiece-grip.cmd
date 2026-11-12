@echo off
rem v1251: builds tests\timepiece_grip_check.cpp (Timepiece raised/folded by the slot-1 grip via native Use pulses) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /I src tests\timepiece_grip_check.cpp /Fo:build\timepiece_grip_check.obj /Fe:build\timepiece_grip_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\timepiece_grip_check.exe
echo check_exit=%errorlevel%

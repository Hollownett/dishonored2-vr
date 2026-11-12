@echo off
rem v1250: builds tests\body_target_check.cpp (downed-body prompt target: pointing geometry, one stable body target, hysteresis, anchor easing) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /I src tests\body_target_check.cpp /Fo:build\body_target_check.obj /Fe:build\body_target_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\body_target_check.exe
echo check_exit=%errorlevel%

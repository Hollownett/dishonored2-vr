@echo off
rem v1232: builds tests\vr_controls_check.cpp (grip intent, choke grip owner, choke hand / spoken-for gate, talk Y owner) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /I src tests\vr_controls_check.cpp /Fo:build\vr_controls_check.obj /Fe:build\vr_controls_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\vr_controls_check.exe
echo check_exit=%errorlevel%

@echo off
rem v1238: builds tests\hold_feedforward_check.cpp (the sticking hold's feed: the goal's own velocity; constant-velocity walk/run, turn, stop, capped yank) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 tests\hold_feedforward_check.cpp /Fo:build\hold_feedforward_check.obj /Fe:build\hold_feedforward_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\hold_feedforward_check.exe
echo check_exit=%errorlevel%

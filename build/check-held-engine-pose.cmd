@echo off
rem v1254: builds tests\held_engine_pose_check.cpp (the engine render frame of a held prop packs back into itself, scales kept, a non-rigid frame
rem refused, an unchanged frame a no-op) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 tests\held_engine_pose_check.cpp /Fo:build\held_engine_pose_check.obj /Fe:build\held_engine_pose_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\held_engine_pose_check.exe
echo check_exit=%errorlevel%

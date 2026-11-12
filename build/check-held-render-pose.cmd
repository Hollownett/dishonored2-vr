@echo off
rem v1249: builds tests\held_render_pose_check.cpp (a held prop's engine render pose = the hold's goal; wall; release blend; stalled publication) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /Ithird_party\minhook\include tests\held_render_pose_check.cpp /Fo:build\held_render_pose_check.obj /Fe:build\held_render_pose_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\held_render_pose_check.exe
echo check_exit=%errorlevel%

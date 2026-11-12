@echo off
rem v1258: builds tests\held_one_pose_check.cpp (the v1256 headset replay: 57 %% of held-prop renders unplaced at the engine pose, 90 Hz renders on a
rem 64 Hz tick; v1256 flips 4 cm against the drawn hand, v1258's drawn-hold engine pose keeps one pose per frame) and runs it.
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /Ithird_party\minhook\include tests\held_one_pose_check.cpp /Fo:build\held_one_pose_check.obj /Fe:build\held_one_pose_check.exe
if errorlevel 1 (echo check_exit=1 & exit /b 1)
build\held_one_pose_check.exe
echo check_exit=%errorlevel%

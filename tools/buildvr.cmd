@echo off
rem Build one numbered DLL with all checks: tools\buildvr.cmd d2vr_render_probe_arm_vNNN.dll (log: artifacts\build-logs\build-<name>.log).
rem tools\build.ps1 fails from a plain PowerShell (vswhere not on PATH); this wrapper sets it up. Grep the log for "error" / "Assertion" / "exit=".
cd /d "%~dp0.."
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set "D2VR_VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -OutputName %1 > "artifacts\build-logs\build-%~n1.log" 2>&1
echo exit=%errorlevel% >> "artifacts\build-logs\build-%~n1.log"

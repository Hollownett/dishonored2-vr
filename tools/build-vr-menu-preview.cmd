@echo off
rem Builds and runs tools\vr_menu_preview.cpp: PNG renders of the ImGui VR settings panel in artifacts\.
setlocal
cd /d "%~dp0.."
if defined D2VR_VS_ROOT set "VSROOT=%D2VR_VS_ROOT%"
for %%e in (BuildTools Community Professional Enterprise) do (
  if not defined VSROOT if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e"
  if not defined VSROOT if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
)
if not defined VSROOT (echo No x64 C++ toolchain found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\preview mkdir build\preview
if not exist artifacts mkdir artifacts
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /O2 /MD /Isrc /Ithird_party\imgui tools\vr_menu_preview.cpp third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_dx11.cpp /Fo:build\preview\ /Fe:build\vr_menu_preview.exe || exit /b 1
build\vr_menu_preview.exe

@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T=%TMP%\pgc
mkdir %T% 2>nul
cd /d "%~dp0.."
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /Isrc tests\physical_grab_check.cpp /Fo:%T%\ /Fe:%T%\pgc.exe && %T%\pgc.exe

@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T=%TMP%\zz
mkdir %T% 2>nul
cd /d "%~dp0.."
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /Isrc tests\held_bind_check.cpp /Fo:%T%\ /Fe:%T%\zz.exe && %T%\zz.exe

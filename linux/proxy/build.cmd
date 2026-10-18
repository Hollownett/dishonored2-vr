@echo off
rem Builds the Linux/Proton dinput8 proxy with MSVC (run from the repo root in a VS x64 prompt).
if not exist build\linux-proxy mkdir build\linux-proxy
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /Ilinux\proxy /Fo:build\linux-proxy\ /LD linux\proxy\dinput8_proxy.cpp /Fe:build\linux-proxy\dinput8.dll /link /DEF:linux\proxy\dinput8.def kernel32.lib user32.lib

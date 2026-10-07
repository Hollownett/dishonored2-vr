# Third-party notices

## Khronos OpenXR SDK headers

Only the generated headers and licenses are included. Source: KhronosGroup/OpenXR-SDK,
tag `release-1.1.63`, commit `f2448a8797c85814aa892efc1ab8707900fbcc78`.
The original copyright/SPDX notices are retained in each header; license texts
are in `third_party/openxr/LICENSES/`. `source.json` records each downloaded file's SHA-256.
No replacement runtime or loader is installed or distributed. The diagnostic
uses the OpenXR loader already installed with SteamVR.

## IGCS Dishonored 2 research

The four camera/Ansel instruction signatures in `tools/probe_game.py` come from
[Frans Bouma's IGCS Dishonored 2 InterceptorHelper.cpp](https://github.com/FransBouma/InjectableGenericCameraSystem/blob/25f91babdca23fc83f4b6ab6064d33ba18e92c1c/Cameras/Dishonored2/InjectableGenericCameraSystem/InterceptorHelper.cpp).
No executable patching, Ansel session bypass, or camera injection code is copied.
Their presence in a file is a research lead, not evidence that gameplay VR works.

Copyright (c) 2017, Frans Bouma. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## MinHook

The DXGI diagnostic includes [TsudaKageyu/MinHook](https://github.com/TsudaKageyu/minhook),
tag `v1.3.4`, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`.
Original notices and the full BSD license are retained in
`third_party/minhook/LICENSE.txt` and the source files. `source.json` records
each downloaded file's SHA-256. Only the x64 implementation is included.

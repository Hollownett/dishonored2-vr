# Third-party notices

## Dishonored-VR grab mathematics, snap-turn and menu-tap policy

`third_party/dishonored_vr/grab_pose.h` is copied verbatim from
[Dishonored-VR](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/hands/grab_pose.h),
commit `b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec`. The complete original
zlib/libpng license, including the VR modifications copyright, is retained in
`third_party/dishonored_vr/LICENSE`; `source.json` records the source hashes.
`src/finger_pose.hpp` adapts the upstream pure screw interpolation to the
verified native joint packing and parent-relative chain reconstruction.

`src/vr_turning.hpp` is an explicitly marked altered, portable adaptation of
the same repository's `src/game/dishonored/snap_turn.cpp` (VR-219). It retains
the threshold/rearm and stale-step policy, uses radians rather than UE units,
owns the queue atomically through a mutex, and cancels queued steps on input
scope/focus changes. It is not the upstream engine writer. The policy and
finger adapter are tested offline. The snap policy now feeds the independently
guarded native physics adapter. Finger interpolation can use live grip input
only with compatible captured native empty-hand references and verified empty
occupancy; otherwise it preserves the authored native pose. No accepted closed
references are installed yet. The former guessed geometric fist was rejected.
The dynamic-body velocity
motor in `src/physical_grab.hpp` and its Dishonored 2 adapter are original code.

`src/menu_tap.hpp` is an explicitly marked altered adaptation of the same
commit's `src/core/input/controller_emulation.h`. It retains the 150 ms Start
pulse after releasing the physical menu button, excludes upstream modifier,
chord and journal bindings, and cancels held gesture tails after focus loss.

## Khronos OpenXR SDK headers

Only the generated headers and licenses are included. Source: KhronosGroup/OpenXR-SDK,
tag `release-1.1.63`, commit `f2448a8797c85814aa892efc1ab8707900fbcc78`.
The original copyright/SPDX notices are retained in each header; license texts
are in `third_party/openxr/LICENSES/`. `source.json` records each downloaded file's SHA-256.
No OpenXR runtime is installed or distributed.

## Khronos OpenXR loader (binary)

`third_party/openxr/bin/openxr_loader.dll` is the unmodified x64 release build of the Khronos OpenXR loader 1.1.63
(`openxr_loader_windows-1.1.63.zip` from KhronosGroup/OpenXR-SDK-Source releases; PGP signature verified, see
`third_party/openxr/bin/source.json`), Apache License 2.0 (`third_party/openxr/bin/LICENSE`). The mod loads it so any
active OpenXR runtime (Virtual Desktop, Oculus, SteamVR) works without SteamVR installed; SteamVR's copy is a fallback.
The launcher installs it beside the mod.

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

## Dishonored 2 headtracking camera research

The native `idRenderView::Setup` ABI, source/render-copy offsets, basis row
interpretation and game-logic manager profile used by the pose hook/state reader
were corroborated against [itsloopyo/dishonored-2-headtracking](https://github.com/itsloopyo/dishonored-2-headtracking/tree/0dc9036a9b5e9ce3161c38d7f89ffeac34a20e47),
commit `0dc9036a9b5e9ce3161c38d7f89ffeac34a20e47`. The installed PE fingerprint
matches its Steam profile; the hook entry bytes are checked independently.
The observer mode is read-only; the new XR pose mode temporarily overrides native
view inputs with this repository's tracked-pose conversion. No OpenTrack protocol
or collision implementation is incorporated. Retained license:

MIT License

Copyright (c) 2026 itsloopyo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## SMAA

The shader and lookup textures are from [iryoku/smaa](https://github.com/iryoku/smaa), commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`. The original MIT license and copyright notices are retained in `third_party/smaa/LICENSE.txt` and the source files. `source.json` pins each file hash. `tools/embed_smaa.py` verifies the source and embeds the shader for runtime compilation. The integration uses SMAA 1x Ultra colour-edge detection and linear-space neighborhood blending independently for each eye, without sharpening or temporal reprojection.

## AMD FidelityFX SDK (FSR 3 upscaler, DX11 backend)

Headers and prebuilt release x64 static libraries (`ffx_backend_dx11`, `ffx_fsr3`, `ffx_fsr3upscaler`, `ffx_frameinterpolation`, `ffx_opticalflow`) are copied from the [Luma Framework](https://github.com/Filoppi/Luma-Framework) clone's `Source/External/FidelityFX` (SDK 1.0.1 headers, AMD copyright 2023-2024, MIT). The MIT license text is retained in `third_party/fidelityfx/LICENSE.txt`. The libraries are built with `/MD` by the third party and were not rebuilt here. Used by `src/fsr3_eye_upscaler.hpp` for the DX11 path and fallback.

## AMD FSR SDK 2.3 (optional FSR 4.1.1 upscaler)

The optional DX12 bridge uses unmodified public API headers and AMD-signed
loader/upscaler DLLs from the official [FSR SDK v2.3.0 release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.3.0),
commit `60f4ea81909200d8542eca14dccb2628b763a9a3`. Source URLs, file hashes
and signature evidence are retained in `third_party/fsr4/manifest.json`.
The complete AMD license and third-party notices are retained in
`third_party/fsr4/licenses/license.md`; copyright notices remain in the headers.
The DLLs have not been modified or rebuilt. Local compatibility headers forward
to the vendor's original directory structure. No frame generation is integrated.

## bcdec (BC7 partition tables in the launcher)

`launcher/ArtImport.cs` decodes BC7 game textures with partition tables taken from
[iOrange/bcdec](https://github.com/iOrange/bcdec) `bcdec.h` v0.985 (Copyright (c) 2022 Sergii Kudlai),
available under the MIT License or the Unlicense; used here under the MIT License:

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
> persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
> and this permission notice shall be included in all copies or substantial portions of the Software.
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

The rest of the decoder (BC7 modes, BC1 colour, BC3/BC4 alpha) follows the public D3D11 BC7 format specification and
reproduces Pillow's BC1/BC3 integer rounding.

## Launcher bundle

`launcher/dist/DishonoredVRLauncher.exe` is a self-contained .NET 8 application (the .NET runtime is MIT licensed). It
embeds the mod DLL (which statically links MinHook, SMAA and the FidelityFX FSR 3 libraries above), the AMD FSR 4
loader/upscaler DLLs, this file and the license files listed above (`third_party/*/LICENSE*`, `third_party/fsr4/licenses/*`),
and writes them to the user's mod home (`%LOCALAPPDATA%\DishonoredVR`). It contains no Dishonored 2 assets: game art is
read from the user's own install at runtime.

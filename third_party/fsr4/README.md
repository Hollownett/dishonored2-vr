# Official FSR4 SDK subset

Pinned official AMD FSR SDK release v2.3.0, commit
`60f4ea81909200d8542eca14dccb2628b763a9a3`.
[Release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.3.0)
[AMD SDK page](https://gpuopen.com/amd-fsr-sdk/)

This directory contains the signed DX12 loader and upscaler, their public API
headers, the AMD license/third-party notices, and relevant official integration
guides. Files were fetched individually from the pinned official GitHub Kits
paths; the127MB prebuilt sample archive was not downloaded. No DLL was loaded
or executed during preparation. `manifest.json` records every source URL,
SHA256, file size and Authenticode result.

Both DLL signatures verified Valid with publisher Advanced Micro Devices and
certificate thumbprint33D35682079E201671B738B7209B4586103BC271:

| File | File version | SHA256 |
| --- | --- | --- |
| bin/amd_fidelityfx_loader_dx12.dll | 2.3.0.2740 | e2d85aa05a9bd9ed8b38935fdf5199372cca6f74c12015143bb6f945ee1608aa |
| bin/amd_fidelityfx_upscaler_dx12.dll | 4.1.1.2740 | d0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46 |

Keep these two DLLs together under their official names. Load the loader with
an absolute trusted path and a restricted DLL search policy; do not permit a
current-working-directory substitution. The loader contains no effect code;
the companion upscaler contains ML FSR4 plus legacy providers. Signature/version
metadata alone does not prove a created context uses FSR4.

## Header layout

Unmodified official headers retain their relative include layout:

- api/include/ffx_api.h, ffx_api_types.h, ffx_api_loader.h and ffx_api.hpp
- api/include/dx12/ffx_api_dx12.h and ffx_api_dx12.hpp
- upscalers/include/ffx_upscale.h and ffx_upscale.hpp

Use `/Ithird_party/fsr4/api/include /Ithird_party/fsr4/upscalers/include`.
The `include/` files are small local compatibility forwarders to those
canonical vendor files; they are not modified vendor headers. This also avoids
redefinition from loading duplicate `#pragma once` headers at different paths.
A compile-only C++17 check including C/C++/DX12/loader headers together passes.
The loader helper requires `_WINDOWS` or `PLATFORM_WINDOWS` to be defined.

## Genuine provider selection

1. Resolve the five public exports using `ffxLoadFunctions` or GetProcAddress.
2. Query with null context and `ffxQueryDescGetVersions` (type4),
   `createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE`, and the actual
   ID3D12Device. First count; then IDs and names together. Copy names immediately
   because later queries may overwrite global name storage. Do not hardcode IDs.
3. Select the reported FSR4 provider. If none is offered, report unavailable;
   do not label a legacy3.1 provider FSR4.
4. Chain `ffxCreateContextDescUpscale`, `ffxCreateBackendDX12Desc`,
   `ffxOverrideVersion` (type5, enumerated ID), and
   `ffxCreateContextDescUpscaleVersion` (version=FFX_UPSCALER_VERSION).
   The API-version descriptor is mandatory since SDK2.1; this header version
   is4.1.1. Preserve device/message/allocator lifetimes per the API contract.
5. Query the created context with `ffxQueryGetProviderVersion` (type6) and
   record the actual ID/name. Reject unintended provider selection.
6. Query `ffxQueryDescUpscaleGetResourceRequirements` on the context to validate
   which inputs this provider requires. Use the same version override for
   null-context effect queries as for context creation.

`FSR4_GUIDE.md` is the official ML upscaler guide; `UPSCALER_GUIDE.md` provides
shared FSR3 integration details. Required temporal dispatch data includes color,
depth,2D motion vectors, output, jitter in render-pixel units, motion-vector
scale, render/upscale dimensions, frame delta in milliseconds, pre-exposure,
reset, near/far/vertical FOV and view-units-to-meters. Use separate temporal
contexts for left and right eyes. Reset on discontinuities and recreation.

Input resources must be in D3D12 NON_PIXEL_SHADER_RESOURCE state before dispatch;
output must support unordered access with correct resource-state declarations.
Share/fence proof is separate from correct temporal data and actual FSR4
provider dispatch. Do not execute on resources still owned by D3D11 without the
verified cross-API GPU fence sequence. Prefer linear color; otherwise use the
explicit nonlinear-color context and dispatch flags appropriate to the source.

## Runtime and Agility requirements

The official ML guide explicitly requires HLSL CS6_6 and the signed binary
API on DX12. The official v2.3.0 third-party notice lists Agility SDK1.616.1
and DirectX-Headers1.616.0 for SDK samples. That sample dependency is not a
verified minimum runtime requirement for the DLL provider. No Agility runtime
was bundled here and no host EXE SDK exports were changed. A real provider
query/context/create/dispatch on the existing system D3D12 runtime remains
necessary; do not infer successful FSR4 execution from Shader Model support.

SDK sample/readme metadata used in this assessment is preserved separately in
`artifacts/research/fsr4-sdk-v2.3.0/`.

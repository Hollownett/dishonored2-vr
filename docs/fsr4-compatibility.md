# FSR 4 compatibility assessment

Verified on 2026-10-10. v204 implements an optional experimental FSR 4.1.1
backend. Provider dispatch and the stereo wrapper pass hardware checks;
native-game and headset validation remain pending.

AMD's [FSR 4 integration guide](https://gpuopen.com/fidelityfx-super-resolution-4/)
describes an easy upgrade from the FidelityFX API introduced in FSR 3.1, but
lists DirectX 12 as the supported graphics API. The current
[FSR SDK](https://gpuopen.com/amd-fsr-sdk/) provides FSR Upscaling 4.1.1 through
prebuilt signed loader/upscaler DLLs and likewise lists DirectX 12. RX 9000
hardware meets the listed GPU requirement; hardware support alone does not
make a DirectX 11 integration compatible.

## Local integration

- `src/fsr3_eye_upscaler.hpp` uses `ffxGetInterfaceDX11`,
  `ffxFsr3ContextCreate` and `ffxFsr3ContextDispatchUpscale` with D3D11 resources.
- Installed `ffx_fsr3.h` and `ffx_fsr3upscaler.h` identify FSR 3.1.2.
- `tools/build-msvc.cmd` statically links the copied Luma Framework DX11
  FidelityFX backend and effect libraries, documented in THIRD_PARTY_NOTICES.md.
- The original DX11 implementation does not have the provider API used by the
  usual FSR 4 upgrade. v204 adds `src/fsr4_eye_upscaler.hpp`, using the official
  AMD-signed DX12 provider staged in `third_party/fsr4`. This is a separate
  backend with GPU resource exchange, rather than a replacement DX11 DLL.

## Implemented optional backend

A separate optional DX12 upscale backend replaces the FSR 3.1 upscale
dispatch, using the same native reduced-resolution color, depth, motion vectors,
exposure, jitter and per-eye reset inputs. It must not upscale the already
upscaled FSR 3.1 result. Preserve the D3D11 game and OpenXR output path.

Microsoft documents [D3D11/D3D12 shared-resource and shared-fence interoperability](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle).
The local probe passes shared-resource/fence checks for HDR color, linear depth,
motion vectors and RGBA8 output on the RX 9070 XT. This establishes the bridge
prerequisites, not headset performance. Implementation checks cover:

1. The same adapter, shader-model support and real shared texture/fence
   behavior for color, depth, motion vectors and output on this machine.
2. The official signed AMD provider and query-selected 4.1.1 implementation;
   the actual context is queried again after creation. An analytical fallback
   is reported as FSR 3, never as FSR 4.
3. Independent stereo contexts and exchange of inputs/output using bounded
   GPU synchronization, with no CPU readback or per-frame CPU wait.
4. Resource states, format/device ownership, context recreation and deferred
   retirement of in-flight allocations. Missing-provider initialization keeps
   the working DX11 FSR 3.1 route. Backend changes reset temporal history.
5. Actual stereo dispatch in the existing deferred-prefix/upscale/remainder
   wrapper, using Native, Quality (77%) and Balanced (67%). These are the
   existing mod ratios, not the SDK's standard Quality/Balanced presets.

No renderer CPU readback or CPU completion wait is introduced. The bridge
submits one D3D11 Flush per successful handoff so the DX12 queue can consume
the input signal. Standalone test completion waits/readbacks are outside the
production renderer. The fixed retirement registry keeps uncertain failed
handoffs until process exit; repeated failures fall back rather than growing
that registry without bound.

## Testing and use

Run `tools/run_demo.ps1 -Fsr4` to request the experimental backend. The launcher
restores its temporary flag after the session and preserves saved VR settings.
The native VR settings image labels the backend actually dispatched. Without
the option, the existing FSR 3.1 path remains the default.

The full v204 build includes 80 genuine FSR 4 dispatches per mode (240 total),
expected-color output checks, HDR/R11G11B10/LDR format tests, cropped inputs,
deferred-owner rejection and missing-provider fallback for both eyes. See
`artifacts/feedback-v204-build-validation.json` and `artifacts/build-v204.log`.

Synthetic stereo wrapper spans were approximately 6.30 ms Native, 7.82 ms
Quality and 7.51 ms Balanced; the original FSR 3.1 checks were approximately
4.52, 3.35 and 3.52 ms respectively. These sequential checks are not a controlled
whole-game performance comparison; spans include queue handoff/submission.
Diagnostic CPU completion waits were outside the timestamp interval. FSR 4
cost more in these checks and remains opt-in. Better reconstruction quality
does not establish higher headset FPS.

Native game resource/pose behavior, focus/recenter recovery, headset quality
and matched headset performance remain to be verified. World-space native
interaction prompts and objective markers are separate unfinished work.

# Dishonored 2 VR performance investigation, 15 October 2026

The hardware makes a Medium VR target reasonable to investigate. Low performance is confirmed at 100% VR resolution, not only at the final 120% setting. The completed v555 session does not establish stable VR performance, and it does not establish that this PC is incapable of it. Most saved native details are already Medium. The next work should measure engine/rendering and upscaler costs at 100% before making further visual sacrifices or declaring a default optimized.

## Verified machine and workload

Read-only Windows queries confirmed a Ryzen 7 9800X3D, RX 9070 XT and 61.61 GiB usable physical memory. Bethesda's original recommended specification is an i7-4770/FX-8350, 16 GB RAM and GTX 1060 6 GB/RX 480 8 GB. That supports the expectation that this machine comfortably exceeds the original monitor-game requirements. It cannot certify a third-party stereo pipeline at a particular resolution or frame rate. [Bethesda requirements](https://help.bethesda.net/app/answers/detail/a_id/36558), [Bethesda performance guidance](https://help.bethesda.net/app/answers/detail/a_id/36691/~/what-do-i-do-if-dishonored-2-has-performance-or-framerate-issues-on-pc).

The v555 runtime prediction period is 13.889 ms, corresponding to a 72 Hz target; 90 Hz would allow only 11.111 ms. A 60 FPS monitor result permits 16.667 ms. Predicted timing is not a measurement of delivered headset frames. [OpenXR specification, XrFrameState](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#XrFrameState).

At the end of v555 the recorded output and FSR input were 2534 x 2765 per eye, multiplier 1.2, FSR4 Native, right-eye desktop mirror and inactive resolution governor. This is 14.01 million pixels per stereo pair, 1.69 times a flat 4K image, plus two view submissions and reconstruction/copy costs. FSR Native performs temporal reconstruction without reducing input pixels. Saved preferences request SMAA, although the final status reports zero SMAA eye passes; verify the effective path rather than equating the saved toggle with GPU work.

## Completed-session evidence

Recomputed all 175,005 complete, deduplicated timing rows from the completed v555 archive, rather than stopping at the earlier interim observation. [Detailed report](../artifacts/performance-investigation-20261015/report.md), [JSON with source hashes](../artifacts/performance-investigation-20261015/report.json).

| Measurement | Result |
|---|---|
| Worst uninterrupted eligible 10 seconds | 39.34 application frames/s; interval median/p95/p99 24.29/32.92/39.61 ms |
| Worst uninterrupted eligible 30 seconds | 50.86 application frames/s; median/p95/p99 19.95/24.07/26.40 ms |
| Engine gap in those windows, median | 23.07 ms / 18.86 ms |
| XR bridge in those windows, median | 0.282 ms / 0.248 ms |
| Final native GPU timing | Disabled; zero completed frames/lists |

Both worst windows use FSR4 Quality at 77%, not the final Native setting. The 10-second window outputs 2112 x 2304 per eye; the 30-second window outputs 2534 x 2765. Scene, recording load and configuration differ. These are descriptive slow windows, not an A/B resolution benchmark. The later 30-second result supersedes the interim report's 56.85 Hz.

Follow-up verification: all ten archived status samples inside the worst 10-second window explicitly report requested and effective multiplier 1.0, matching the 2112 x 2304 frame rows. Thus 39.34 application frames/s occurred at 100% output with already reduced 1626 x 1774 scene input per eye. The final 120%/Native configuration cannot explain this earlier slowdown. Evidence: [resolution-100-confirmation.json](../artifacts/performance-investigation-20261015/resolution-100-confirmation.json). Treat 100% as the diagnostic baseline; changing back to it is not an established repair.

The engine gap dominates the sustained slow intervals. It includes native CPU simulation/render work, native rendering, FSR work executed between native lists and possible GPU/driver backpressure. Small Present/XR bridge CPU spans do not exonerate mod rendering inside that gap. In particular, FSR4 uses D3D11/D3D12 copies, fences and a Flush in `src/fsr4_eye_upscaler.hpp`. Its observed per-eye GPU wrapper includes synchronization/copies; asynchronous per-eye medians cannot be added into a complete GPU frame or called pure shader time.

The slowest 30-second window also contains a separate 198.94 ms frame, including 180.41 ms in desktop Present. Address those spikes separately from the steady engine gap. The earlier v551 material repair has offline reader-call evidence, but no matched full-frame performance win; it remains an unvalidated contributor, not the answer to all missing FPS.

Resource samples, including menus, show game whole-machine CPU median/p95 21.27/26.08% and busiest per-process GPU-engine 60.28/76.92%. Neither percentage settles CPU versus GPU limitation. A serial thread or synchronization dependency can stall while other cores/engines remain idle. The resource sampler itself used median 1.34% of the machine and roughly 2.06 seconds per query cycle. Recording, readbacks and concurrent work prevent treating this as clean maximum-performance evidence. Only four known compile intervals are excluded; later concurrent work may remain.

## Prepared native Medium profile

Added `--profile vr-medium` and read-only `--preview` to the existing graphics-profile tool. Six detail enums were verified as Medium = 2 directly from the hash-pinned executable's description strings. Exact file offsets and current before/after values are in [preset-preview.json](../artifacts/performance-investigation-20261015/preset-preview.json). The overall preset enum has a different description; it is deliberately untouched.

| Native detail | Current saved value | Medium candidate |
|---|---|---|
| Textures | Ultra (5) | Medium (2) |
| Models, shadows, view distance, ocean, decals | Medium (2) | Medium (2) |

These are current saved values, not a historical native-cvar readback for every benchmark frame. Consequently this profile would presently change only textures. Texture reduction may help streaming or allocation pressure; no gain is established, and there is no basis to promise it will cure the sustained slowdown. Retain High/Ultra textures if matched measurements show no useful improvement from reducing them.

Preview from the repository root:

```powershell
python tools/vr_graphics_profile.py --preview --profile vr-medium
```

The preview writes no settings, state or backup. Hash checks confirmed the game config, graphics-profile state and VR preferences remained byte-identical, and no backup was created. Six focused tests pass, covering detail-only edits, restoration and existing preservation/rejection behavior. The legacy High profile remains the existing default; no render/game DLL was built or launched.

An old graphics baseline remains marked applied from 8 October and differs from today's choices. The tool continues to reject another apply over that baseline. Do not blindly restore the old baseline or discard it to bypass protection. Any later approved application needs an explicitly resolved baseline, closed game and a fresh retained copy of today's configuration. The Medium profile owns only native detail settings; AA, resolution, comfort, turning and calibration remain separate.

## Candidate VR defaults and validation sequence

Prepare the following comparison candidate, without silently replacing saved preferences:

| Setting | Candidate |
|---|---|
| Runtime target | Existing 72 Hz mode first; 90 Hz is a separate target |
| Native details | Medium comparison above; retain sharper textures if lowering them gives no measured gain |
| VR output multiplier | Hold 1.0 as the diagnostic baseline, where low cadence is already confirmed; log actual dimensions |
| Temporal reconstruction | Compare FSR3 Quality and FSR4 Quality at the same 77% input, plus SMAA-only/native control |
| Additional SMAA | Compare off/on separately with temporal FSR; require actual pass counts and headset edge-quality judgment |
| Mirror | Preserve the existing single right-eye mirror |
| Comfort and controls | Preserve existing motion-blur/DOF/SSAO defaults, accepted movement/turning, calibration and HUD choices |
| Dynamic resolution | Keep the currently inactive governor inactive during fixed-resolution comparisons |

For this session's sizing, multiplier 1.0 corresponds to 2112 x 2304 per eye. That is about 31% fewer output pixels than multiplier 1.2. A 77% input is 1626 x 1774 per eye, about 5.77 million scene input pixels per stereo pair. These are pixel-count calculations, not an FPS prediction: output reconstruction, view submission and synchronization costs remain. AMD documents that FSR4 performance depends strongly on output resolution. The mod's 77%/67% labels are custom ratios, different from AMD's standard Quality/Balanced ratios; use exact dimensions in comparisons. [AMD FSR4 integration manual](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/).

1. Coordinate one clean run through the main repair lane, with the exact combined DLL/source identity. Preserve saves, current settings and all accepted controls. Do not launch the older isolated v560 performance candidate over unrelated repairs.
2. Warm the same save/scene. Retain stationary and repeatable movement/combat windows, at least 30 uninterrupted seconds per case, repeating baseline after candidates to expose drift. Keep refresh, route, mirror, settings and actual view/input dimensions fixed within a case. No video, screenshots/readbacks, builds or heavy resource sampler during timing windows.
3. Enable the existing native command-list GPU diagnostic only for the coordinated bounded measurement, preserving prior flag bytes. It disables independent FSR disjoint brackets; require advancing completed queries and record coverage. Native list sums omit immediate draws and elapsed queue gaps, so retain CPU/queue spans too. Profile FSR4 copy/wait/dispatch separately rather than removing required synchronization.
4. At fixed 100% output, prioritize attribution of the engine gap and compare FSR3/FSR4 at identical 77% input/output sizes, retaining a native/SMAA-only control. Then isolate additional SMAA and texture changes. A later resolution comparison can measure scaling sensitivity; returning from 120% to 100% is not the proposed solution to the already confirmed 100% slowdown. Do not change every knob simultaneously and attribute the result to one setting. If graphics reductions leave the engine gap unchanged, prioritize serial engine/mod work and synchronization rather than lowering more detail.
5. Choose a default only after repeatable sustained application timing fits the 13.889 ms budget with headroom, p95/p99 and worst 10/30-second windows are acceptable, and compositor/Virtual Desktop counters confirm delivered frames, drops and reprojection. Validate image clarity, shimmer, hands/weapons, comfort and accepted controls in the actual headset. Stable 90 Hz requires its separate 11.111 ms budget.

Status: archived evidence reproduced, native preset tooling prepared and tested, protected settings preserved. No verified optimal default or headset FPS improvement is claimed. The remaining gate is the controlled real-headset measurement, not another offline build result.

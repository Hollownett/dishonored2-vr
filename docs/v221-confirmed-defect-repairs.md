# Confirmed v221 defects: source repairs, 2026-10-11

Historical first repair pass. The subsequent independent measurements, native chest fitting and source-clock integration are recorded in [v221-independent-fit-repairs.md](v221-independent-fit-repairs.md). The evidence and limitations below describe this earlier pass.

v221 remains rejected and launch-blocked. Its DLL/source/run archives are preserved. This work does not create a new DLL, run the game, change the demo selection, edit saves/settings, or establish headset acceptance.

## Changes

- Equal-millisecond publications now consume zero temporal time without resetting velocity, plants, step phase or native-ownership migration. Backward time, reference changes and real long gaps still reset. The regression replays archived sampled wall intervals, including duplicates; poses are synthetic because sampled records are not a complete raw tracking stream.
- The live integrated arm path solves the current tracked publication from the retained native body, and returns palette/source/fit/ownership in one locked result. Both arm model frames convert that same world solution. Arm publication returns its own source stamp atomically. The alternate full-arm fallback and unqualified cached-palettes reuse are removed from this live path. Standalone arms no longer add a model-origin correction to world grips or move shoulders toward wrists.
- T-pose calibration measures total controller span. It now retains native shoulder width unless independently supplied, explicitly reports unmeasured shoulders/individual arm lengths/torso width, and does not advertise complete player-body fitting. Reuse accepts independent shoulder dimensions instead of requiring proportional scaling with span. Unsupported geometric limb fits produce a wizard error instead of restarting XR. Reapplying identical calibration no longer increments arm mapping identity.
- Tracked arm, attachment and physical-grab source freshness use one 250 ms constant. Equipped palettes carry source time/revision, pose/reference/mapping identity, animation generation and per-side model frame. Rejected/native-owned uploads revoke cached attachment state. Advancing tracking preserves the last admitted rendered palette until replacement; new admission still requires the latest source revision. Backward sequencing and reference/mapping changes revoke it. Physics velocity updates/decays on every accepted positive interval through 250 ms; discontinuities are rejected before motor or release.
- Mixed game-logic discovery slots must be aligned plausible pointers with a completely readable committed range. Every covered memory region is checked; guard/no-access pages are rejected. Backref/id guards and SEH for concurrent memory changes remain.
- `present-boundaries.csv` measures full callback/pre-desktop/post-XR boundaries, original desktop Present, discovery, lean, grab filtering, takedown/crouch and controller work. It retains two 20,000-row chunks and is preserved by both archive tools. Analysis merges only exact Present/XR end/epoch matches. Nested span totals must not be added together. Logging after the final clock sample is excluded from the callback duration.
- Clean cadence reports consecutive slow windows and breaks at captures, missing Presents, mode/configuration/reference transitions. Reanalysis of the unchanged v221 archive still identifies all 301 clean slow intervals. Window duration includes the first slow interval beginning at its predecessor; this differs from measuring only first-to-last slow row timestamps.

## Evidence and remaining gates

Final evidence is indexed in `artifacts/repair-v221/repair-report.json`. Focused fixtures exercise arm-before-body, translated/rotated separate arm frames, 200 ms source age, repeated ticks, ownership revoke, stale/rejected replacement, fast movement followed by slow stationary release, guard pages and long stalls hidden by a fast median. These establish bounded math/API behavior only.

All 11 focused C++ checks and 40 Python checks passed.

The complete renderer translation unit was compiled with MSVC `/W4 /WX /O2`. `/wd4324` suppresses the existing intentional padding warning in aligned `NativeWeaponDryLease::Lease`; all other warnings remain errors. No link/DLL build was performed.

Still unresolved: actual independently measured torso width/transverse fitting; the cause of the recorded sustained stalls; visible gait/shoulder/skin acceptance; equipped query versus render synchronization when native model frames change; and identification of the better morning build. Native body/opposite-arm authored sources can be retained from prior callbacks. Do not call items stable or the candidate ready from these fixtures.

Native attachment signatures (seven-/five-argument readers), offsets and authored ownership remain unchanged and were checked against existing decompilation evidence. Public references: [Microsoft VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery), [memory protection flags](https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants), and [OpenXR xrLocateSpace](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrLocateSpace.html).

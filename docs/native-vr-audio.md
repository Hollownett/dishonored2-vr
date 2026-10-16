# Native VR audio ownership and ABI

Static research, 2026-10-15. Pinned Steam 17576867 executable, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`, image base `0x140000000`. Addresses below are RVAs. No game launch, injection, live calls or headset validation was performed for this research. Bounded byte-level disassembly: `artifacts/vr-audio-20261015/research/native-disassembly.txt`.

## Verified native ownership

- `arkCpntAudioListener` constructor `0x70A0D0` assigns vtable `0x1C4E808`. Its complete-object locator `0x1FCDAF8` references type descriptor `0x231CAA8`, whose name at `0x231CAB8` is `.?AVarkCpntAudioListener@@`.
- Component fields: `+0x8` declaration, `+0x10` owning entity, `+0x40` acquired listener, `+0x48` native lock. The pose resolver explicitly reads the entity at component `+0x10`; fallback position is entity `+0x300`, top axis `+0x324`, front axis `+0x30C`.
- Acquire path `0x70A250` reads declaration listener index at `+0x68`, calls `arkSoundWorld::AcquireListener` at `0xC7520`, and stores result at component `+0x40`. Acquire computes listener address `soundWorld + 8 + index * 0x90`, marks ownership and refuses an already acquired listener (explicit diagnostic string).
- Component stop path `0x70A760` releases the listener through `0xC7F90` then clears component `+0x40`.
- Native listener update `0x70AAE0` runs from component iteration phase `0x953060`, call at `0x95309F`. It honors `s_freezeSoundListener`, takes component `+0x48` lock, resolves pose using `0x70A7B0`, submits it via `0xC1BE0`, updates cached component position and unlocks.
- Initial/acquire update `0x70AA20` uses the same resolver and calls `0xC1C90` instead.

Selecting the typed component attached to the exact current player, checking `component+0x10 == currentPlayer`, its declaration, acquired listener identity and listener index against declaration `+0x68` is a defensible integration guard. **Not verified live:** that every gameplay/cinematic state puts its effective audio listener on the current player. If ownership cannot be proved, retain native behavior.

## Verified setter ABI and terminal consumer

Both listener setter functions use Windows x64 ABI:

```cpp
void set_pending(void* listener, const Vec3* position, const Vec3* top, const Vec3* front);
```

- `0xC1BE0`: RCX listener, RDX position, R8 top/up, R9 front/forward. Copies 3 floats each to `listener+0x34`, `+0x40`, `+0x4C`. This function has one static direct caller: `0x70AB5F`, return address `0x70AB64`.
- `0xC1C90`: same ABI and copies, then calls terminal publisher `0xC18B0` and clears velocity. One static direct caller: `0x70AA9F`, return `0x70AAA4`.
- Global sound-world update `0xC7DD0` loops eight listeners, stride `0x90`, calling `0xC1A10`. This promotes pending position/top/front to current fields `+0x10`, `+0x1C`, `+0x28`, calculates displacement and spatial-area ownership from current position, then tailcalls `0xC18B0`.
- Terminal publisher `0xC18B0` converts current position/top/front through `0xC1830`: `wwise = (-native.y, native.z, native.x)`. It submits an old-style Wwise listener transform with front at struct `+0`, top at `+12`, position at `+24` to `0x10CA140`.
- `0x10CA140`: RCX pointer to this 36-byte transform, EDX listener index (`listener+0`). It posts command `0x10` to the Wwise queue, orthogonalizes/normalizes orientation, copies position and index, returns `1`. Identification as old `AK::SoundEngine::SetListenerPosition` is inferred from exact behavior and legacy indexed-listener shape; native behavior and ABI are byte-verified.

Hooking the native pending setter preserves downstream area selection, native unit conversion and Wwise submission cadence. Hooking terminal Wwise directly would require separate coordinate conversion and loses the native pending/current coherence unless handled deliberately. Neither approach constitutes HRTF support.

### Entry bytes

| RVA | First 24 bytes |
| --- | --- |
| `0xC1BE0` | `8b 02 89 41 34 8b 42 04 89 41 38 8b 42 08 89 41 3c 41 8b 00 89 41 40 41` |
| `0xC1C90` | `40 53 48 83 ec 20 8b 02 48 8b d9 89 41 34 8b 42 04 89 41 38 8b 42 08 89` |
| `0xC18B0` | `48 89 5c 24 08 57 48 83 ec 50 48 8b f9 48 83 c1 10 48 8d 54 24 38 8b 47` |
| `0x70AAE0` | `40 57 48 83 ec 60 48 c7 44 24 20 fe ff ff ff 48 89 5c 24 70 48 8b f9 83` |

These are byte comparisons, not wildcard signatures. Validate complete instructions at a hook boundary and pinned image identity.

## Verified headphone panning control

- Cvar name `s_useHeadphoneMode`, object RVA `0x2BFACD0`, cached integer `+0x28` at `0x2BFACF8`. Registration `0x13581F0` uses default string `0` and flags `0x10001`.
- Native description: headphone panning is 180 degrees; speaker panning is 90 degrees. This is panning behavior, not a binaural/HRTF filter.
- Registered change callback `0x6506D0` is bound through cvar callback registration `0x102020` at `0x6508CE..0x6508EC`. It reads main object global `0x2286080` and cached cvar, then calls/tailcalls `0xC9FD0(main, bool)`.
- `0xC9FD0` requires main-state flags `0x4` and `0x20`, queues a panning-rule change via `0x10CA520` with ECX `bool == 1`, EDX `0x20`, R8D `0`, and tracks mode in main flags bit `0x40`.
- Merely changing cached integer bypasses the verified callback path. Use normal cvar mutation/notification; preserving and restoring original mode needs the same notification path. Live dynamic reapplication has not been exercised.

## Primary reference and limits

Audiokinetic explicitly requires continuous HMD head tracking to reach Wwise as listener orientation: [Using ambisonics for head-mounted devices](https://www.audiokinetic.com/library/edge/?id=ambisoincs_hmd&source=Help). This supports head-correct listener integration; it does not prove the game ships an ambisonic/HRTF pipeline. Front/top orthogonality is documented in [Audiokinetic listener guidance](https://www.audiokinetic.com/download/documents/WwiseProjectAdventure_en.pdf).

No stereo panning, front/back discrimination, height perception, occlusion/reverb quality, comfort, output-device routing or headset sound quality is accepted from this static research. Those require real listening with the exact built DLL and active headset output.

## Headphone session adapter

`src/native_headphone_mode.hpp` verifies the complete 92-byte native panning queue API `0x10CA520` and cvar name. It queues `(headphones=1, native output selector=0x20, output id=0)` without writing the native cvar pair, Wwise main flags or persistent settings. The native caller uses these exact output arguments; the semantic label of old-SDK selector `0x20` is not independently established and is not a channel mask claim.

The adapter saves the original effective main headphone bit and full cvar cache pair. It queues restoration only if the same main remains alive and the native cache pair and headphone bit are unchanged. Foreign native/user changes win. Adapter calls must be serialized; `enabled()` reports an accepted pending queue request plus unchanged ownership evidence, not acoustic confirmation.

`tests/native_headphone_mode_check.cpp` invokes a typed fake queue via the actual production call path and checks arguments, queue failures, apply/restoration, idempotence, no native cache/flag mutation, foreign changes, invalid code/name and inactive-audio deferral. These fixtures do not execute Wwise or prove headset quality.

## Offline playable-player ownership confirmation

Patch-final resource declarations were extracted read-only with the existing `artifacts/claude/impact/scripts/declx.py` parser. Saved extracts: `artifacts/vr-audio-20261015/research/declarations/`. The base `models/characters/player/base/body.def` class is `arkPlayer` and its `m_componentDecls` item `D00000` inherits `components/characters/player/base/audio_listener.cpntaudiolistener`. Both Corvo and Emily body declarations inherit this base body. The primary listener uses `m_mainjointName = center_root_jnt`; the constructor defaults its index to zero. Separate keyhole/spyglass listener entities use index one and `m_startOnEntitySpawn = false`. Thus typed current-player component gating is backed by the shipped playable-player declarations. This still does not establish live ownership in every special gameplay state.

## Notification thread evidence

The terminal headphone-mode API at `0x10CA520` posts command `0x2A` through the native Wwise queue allocator `0x10E8B70`, fills its payload and atomically decrements the queue writer count. It does not call the renderer synchronously. However the surrounding native callback reads the Wwise main pointer and mutates its mode flag byte through `0xC9FD0`; no static lock was found around that byte. Calling install/restore from the same Present thread serializes this adapter with itself but does not independently prove serialization with native audio setting callbacks or shutdown. Main pointer/flag checks reduce unavailable-state calls but are not a lifetime fence. Present-thread use therefore remains an integration assumption requiring live lifecycle review, not a property established by these fixtures.

### Queue-only integration decision

The adapter now calls only the native queue API, avoiding the callback's unguarded native-main flag write entirely. Audiokinetic's own [2017.2 SoundEngine header](https://github.com/audiokinetic/WwiseIncludes/blob/master/SDK/include/AK/SoundEngine/Common/AkSoundEngine.h) documents SetPanningRule as changeable after initialization through the internal message queue. The shipping executable uses an older three-argument ABI, verified from its native caller and payload stores; modern two-argument headers must not be substituted. Presentation-thread submissions are appropriate for the queue API while initialized; proper native shutdown synchronization is still the host engine's lifecycle contract.

## Implemented VR listener route

`audio_listener_pose.hpp` derives one listener from the uncanted physical eye centre. It uses the render camera's admitted room-scale/crouch offset and recenter/turning frame. Authored scenes keep the renderer's native centre and axes. The audio change does not write locomotion, turning or camera fields.

`native_audio_listener.hpp` hooks the pending listener setter only for the verified component-update caller and the exact current player's started listener/camera identities. A publication older than 100 ms, changed player/component/camera, menu, lost tracking/focus, reference change or stopped route falls back to the original arguments. Original native dispatch runs once; emitter state and caller-owned vectors are untouched. `audio` in the live state records accepted head updates, native fallbacks, headphone queue ownership and pending restoration. These counters do not prove acoustic output.

The headphone adapter is submitted from the native Present thread and restored alongside XR retirement/recovery. A deferred restoration remains visible as `audio.restore_pending`; a later Present retries it. Native setting changes take precedence over the saved panning baseline.

## Listening acceptance still required

Use the exact audio candidate and the headset's active audio output. Check a stationary sound on the left/right, then turn the head without using the stick: its apparent position should remain attached to the world. Repeat after a snap turn, lean, physical crouch, recenter and save reload. Check scripted scenes, spyglass/keyhole, pause/resume, focus loss/reconnection and VR exit; listen for channel reversal, double rotation, abrupt position jumps and stuck headphone mode. Existing stereo panning is not an HRTF implementation, and front/back or height discrimination must not be claimed from these fixtures.

## Built audio candidate

`build/d2vr_render_probe_audio_v548.dll` SHA-256: `bcaa008cda3072bb7c40394cf35fbbbf0fce9d1896ca07fa45b5f554e515caeb`.

The DLL was compiled and linked with MSVC `/W4 /WX /O2 /bigobj` from a separate frozen copy because another session was editing the shared checkout. Its exported `.source.json` verifies the archived compiler inputs. The candidate includes checkout work in progress; it is not a replacement for accepted v547 or a headset-approved release. The launcher was not changed.

All three focused audio checks pass, including a real MinHook synthetic native-setter fixture. The broader required checks completed across resumed phases with unchanged headers/tests. Missing copied glyph/HUD fixtures were supplied and replayed successfully. Build-only repairs were the existing weapon-visibility test's missing MinHook include path and MSVC's required large-object flag. The frozen integration's concurrent door field-name mismatch was reconciled with the already-declared `Publication::proxies` member before final compilation; no shared gameplay changes were made for that reconciliation. Third-party FidelityFX missing-debug-PDB linker warnings remain; linking succeeded.

`artifacts/vr-audio-20261015/implementation-result.json` records the DLL identity, logs and evidence limits. `final-audio-checks.log`, `fixture-replay.log`, the resumed regression logs and `isolated-final-link.log` preserve validation. No live game/audio output or headset listening claim is made.

## Audio ownership follow-up for the coordinated build

The shared combined candidate is owned by the coordinating chat. The earlier separate audio DLL is preserved as evidence and does not include this follow-up.

`NativeHeadphoneMode` now distinguishes a readable replaced/null main pointer from unavailable state on the same owner. Replacement retires the old receipt without queueing its saved baseline to a different audio instance. A later install, including an install that first detects replacement, binds the verified current main and captures its own baseline. Unreadable identity, same-owner inactive flags, and queue failure retain deferral. Owner identity is checked on both sides of the flags/cache read and again before queue submission. This does not turn pointer observations into a native shutdown fence.

`AudioListenerOwner` now captures the declaration, main, sound world, listener index, and selected current-player component-list slot/data/count. Read-only native evidence confirms `0x23A2E0` returns main+8, making the listener address main+0x10+index*0x90. The declaration RTTI resolves `arkDeclCpntAudioListener` to vtable RVA `0x1C4E8E8`; the acquired listener stores its index at +0 and acquired bit at +0x88, while the sound world tracks acquired indices at +0. Routing requires index <8, the exact current main/world slot, matching typed declaration +0x68, both acquisition bits, and selected component membership. Currentness rechecks membership, identities, declaration/index, and acquisition anchors; merely preserving list data/count is insufficient.

The focused fixture uses real MinHook dispatch and rejects declaration/index/world/acquisition/member mismatches, mutations during the validation reads, replaced main ownership, and stale publications. Slot seven and moved/reordered component membership are covered. Disable/reinstall verifies retained trampoline reuse. Panning fixtures cover replacement on restore/install, null/reappearing main, changed preferences, unreadable identity, inactive same owner, prequeue owner change, and queue failures. Evidence and exact hashes are in `artifacts/vr-audio-20261015/ownership-fix/` and its frozen handoff.

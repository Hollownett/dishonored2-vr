# Native carry must revoke Empty-hand eligibility

The legitimate native bottle pickup can leave both inventory slots null and
both equipment graph entries named Empty. Those fields alone cannot permit a
second physical grab or an authored empty-hand fist.

Read-only paused PID44016 inspection found exact owned FSM states:

| State | Pool index | State vtable RVA | Declaration vtable RVA |
|---|---:|---:|---:|
|arkPlayerStateHoldObject|49|1c7a998|1c7a868|
|arkPlayerStateCarryCorpse|45|1c77748|1c77618|

The pool is FSM+90, with fixed entries at pool+10+index*8. Each state stores
index at+8, owning FSM at+10, declaration at+18 and active boolean at+20.
Declaration+60 must match the same index. Static853d40 sets active20 to1;
8541a0 clears it. HoldObject entry83c460 invokes its shared activation through
9ffc00→vtable+50; exit83ce70 invokes8541a0. These are observed native functions,
not functions called by the mod's guard.

`native_carry_snapshot.hpp` discovers exactly one started primary-player FSM
only during full hand-state acquisition. It verifies the body back-reference,
component membership, generation, typed fixed states/declarations and repeated
active reads. Callback-time `native_carry_current` checks the captured fixed
membership slot and owner/state/declaration identities without enumerating the
component list or following an unknown carried-object pointer.

`NativeHandState` retains this guard and a carrying flag. Active object OR
corpse carry clears both Empty candidate bits. Unknown state data rejects the
whole hand snapshot. `native_empty_side` checks the fixed carry guard before
and after its existing slot/graph checks; a mid-callback pickup revokes the
result immediately. The occupancy latch clears on carry and requires its
existing one-second vacancy window again after carry ends. Existing NativeGrab
acquisition and callback gates consume these same Empty bits.

Actual compiled read-only reader: HoldObject active, CarryCorpse inactive,
inventory slots0/0, Empty candidate bits0, callback Empty masks0/0. Artifact:
native-arm-assets/native-carry-compiled-guard-v95-44016.json. The broader FSM
enumeration and RTTI proof is native-carry-fsm-v95-44016.json. Neither reader
writes memory, invokes engine methods, injects code or controls input.

Synthetic tests cover each carry state, unknown boolean, wrong index/type,
owner/pool/declaration replacement, transition during read, no callback list
scan, same-tick revocation and vacancy rearming. MSVC /W4 /WX and NativeGrab
include compilation pass. Native pickup/drop/corpse behavior with the integrated
build remains a separate acceptance step owned by the parent agent.

## Python source capture carry and run evidence

`inspect_native_hands.py` now acquires the exact owned FSM and typed HoldObject49/CarryCorpse45 state/declaration pair. Both started identities, FSM/body ownership, component membership, pool slots, declaration indices and boolean active bytes must validate. Two reads plus root revalidation reject transitions. Each hand snapshot retains `native_carry`, `carry_before`, `carry_after` and `carry_consistent`; each pose repeats the hand snapshot after geometry capture. Active carry blocks both Empty bits even when slots and equip graphs are Empty. Unknown carry clears hysteresis. Diagnostic occupied capture also currently refuses active carry, conservatively.

Fresh captures include Unix start/end and per-pose timestamps. Legacy captures without typed per-pose carry evidence remain uncertified; a mid-interval C++ carry sample cannot upgrade them.

After the matching injector run ends, run:

```powershell
python tools/verify_native_hand_source_run.py --capture <capture-dir>/capture.json --run-config <archived-run-config.json> --diagnostics <run-dir>/dxgi-probe-result.json --output <capture-dir>/run-provenance.json
```

The archived run config must record `pid`, pinned `executable_sha256`, `started_unix`, `ended_unix`, `tracked_arms_requested=false`, and `arm_palette_copy_requested=false`. Use actual launcher configuration and run interval, not a reconstructed assertion. Diagnostics must have matching PID, both requests false, `native_arm_pose_calls=[0,0]`, `native_arm_copy_calls=[0,0]`, `hook_disabled=true` and zero first-chance faults. The run must cover the entire capture interval. The companion hashes the actual config, diagnostics, capture and each matching pose, and checks every pose's before/after carry tuple and timestamp. It records writer absence for those exact files; original reports remain unchanged. Configuration evidence is only as trustworthy as its launcher provenance; this tool cannot authenticate manually invented configuration.

The companion does not confirm an authored pose or fist, certify palette equality, certify headset quality, or relax the captured-reference exporter. Those independent checks remain required. No existing capture is automatically upgraded.

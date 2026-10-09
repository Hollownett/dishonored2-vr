# Default-off Sword numeric input experiment

Review-only source, not activated by the helper. No build or live run was performed for this change.

Configure `trace.input_experiment(requested)` before `install()`. Default is false. Enabling an already active installation throws. Requested installation requires frozen tracking and eligibility readers and checks the exact `54130b -> 25efe0` call as well as the existing unpack/outer/query prologs and caller pins. Root must retain its existing exact upload prolog/hash review.

Root integration replaces the existing single upload original invocation with:

```cpp
trace.invoke_input_upload(arm, model, native_source, baseline_submitted_source,
    count, native_model_base60, caller,
    [&](void* native_model,const void* selected,UINT native_count) {
        original_arm_upload(native_model,selected,native_count);
    });
```

Use the current native `rig::Upload::model_frame` for `native_model_base60`, and the actual hook caller return address. When no validated arm/upload is available, pass an empty arm/base; the wrapper calls the existing baseline once and records rejection. The baseline pointer must remain alive through the synchronous call. Do not add an outer catch that calls original again.

`match_upload()` remains an observer. Only when requested, it defers the GPU dry boundary to `invoke_input_upload()` so that the native original is immediately preceded by qualification. Invoke the wrapper even for a rejected upload belonging to a pending primary chain, so fallback is recorded. Disabled behavior calls the existing baseline once without experiment counters or input CSV.

Primary `09b900` uses only caller return `53d539`, owned outer/compose scopes, saved typed Sword equipment guards, current native source/model/frame, frozen revision, rig/epoch, ordinary logic and current eligibility/calibration. GPU `25efe0` uses only `541310`, count69, the exact same retained primary lease, full60-byte model input equality, and fresh repeated dry guards. Lease storage has a 16-byte aligned posed member; its 69*64-byte extent and allocation/member addresses are checked. Secondary `53d705` is always native and counted as forbidden fallback. No wrapper writes native input or output buffers; original native output arguments and count are unchanged.

`native_weapon_input_call()` owns the numeric lease by value through return/unwind and calls its callable once. Qualification holds no observer mutex during original. Revocation removes future eligibility while active copied values survive. Original exceptions propagate, without rollback or a second call. This provides call-local copied-input lifetime; it does not claim a global scheduler fence or coherent cross-consumer commit.

`native-weapon-input.csv` is bounded to8192 rows. It separates `actually_substituted`, `input_value_changed`, `original_normal_return`, `native_effect_possible` and `partial_fallback`. Possible effect means meaningful numeric input changed and original was invoked; it is not visual acceptance. Site0 partial rows record pending primary revocation. Original unwind, pending primary replacement, or upload rejection after primary increments partial failures. Secondary native fallback remains explicit even when primary/GPU succeed.

Result counters: `input_primary_substitutions()`, `input_gpu_substitutions()`, `input_secondary_fallbacks()`, `input_fallbacks()`, `input_partial_fallbacks()`, `input_unwinds()`, `input_value_changes()`. Reasons:0 selected,1 disabled,2 unqualified,3 wrong caller,4 wrong span/alignment,5 invalid pose,6 secondary forbidden. Append this CSV and counters to root's archive/result JSON.

Root's opt-in profile must require tracked arms plus native attachment trace, explicit10..30-second duration, and reject left aim, physical grab, custom script, saved settings, cleanup and untimed runs. Camera trace and fault observer are compatible. Profile negatives belong to root's launcher validation. This helper changed no launcher, main integration, engine query/refcount APIs or runtime settings.

Authored fixtures: `tests/native_weapon_input_check.cpp` checks exact caller/span/alignment/typed negatives, shared palette/socket, native source immutability, single invocation, exception and ownership after publication removal. Existing attachment trace fixture adds disabled/default and requested-without-lease upload fallback plus original exception once. These fixtures are not headset quality or complete weapon proof. Root must compile/run them and review prologs before activation.

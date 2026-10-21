# VR performance on the laptop (sim rig numbers)

Sim-rig numbers are GPU and CPU cost without a real HMD. They are valid for before/after comparison on the same scenario and
save, not for absolute headset fps.

## Comfort baseline

Command: `linux/d2vr-linux sim --scenario idle --lead 28 --seconds 120 --candidate-dll <current build, named like the installed DLL> --status-field comfort`
(the 120 s session cap leaves 28 s of lead-in for 90 s of idle; the save may still be loading when the lead ends).

Run 2026-10-21 08:22 (fix round 1 rig; candidate = Task 1 source built under the installed name d2vr_render_probe_arm_v600.dll, sha256
860971e56ada5c7764490952a1161092045e2305931c78ff251d34c073c038b7). Result OK, gameplay true. An earlier attempt at 07:57 had been
refused by Steam (`AppError_5 "FamilySharing"`); after the account switch it launched.

`comfort` block (the plan-D "before" numbers):

```
{"authored_ms":0,"authored_entries":0,"anchor_v_peak":0,"anchor_jerk_events":0,"view_yaw_rate_peak":1.99371e-05,
 "view_yaw_events":0,"unrequested_yaw_rate_peak":0,"unrequested_yaw_events":0,"samples":6328,"skipped":0}
```

Reading these numbers: `view_yaw_*` measures the engine anchor (body) yaw. Scripted HMD yaw does not rotate the anchor, so
about 0 is the correct reading while standing still; it is not a sign that the scripted yaw failed. The idle scenario therefore
cannot show wobble. Later before/after comparisons of the comfort meter need a scenario where the player moves (oil_tank_drop at
least); idle is only good for the cost (GPU/CPU) baseline and as a floor.

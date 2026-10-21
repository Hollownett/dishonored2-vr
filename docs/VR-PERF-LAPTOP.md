# VR performance on the laptop (sim rig numbers)

Sim-rig numbers are GPU and CPU cost without a real HMD. They are valid for before/after comparison on the same scenario and
save, not for absolute headset fps.

## Comfort baseline

Command: `linux/d2vr-linux sim --scenario idle --lead 28 --seconds 120 --candidate-dll <current build, named like the installed DLL> --status-field comfort`
(the 120 s session cap leaves 28 s of lead-in for 90 s of idle; the save may still be loading when the lead ends).

baseline outstanding: the run was attempted on 2026-10-21 (candidate built from the Task 1 commit under the installed DLL's
name, d2vr_render_probe_arm_v600.dll) but Steam refused the launch: `steam -applaunch 403640` logged
`LaunchApp failed with AppError_5 with "FamilySharing"` (console_log.txt, 07:57:56), so no `StartProbe begin` appeared within the
480 s start timeout and no `comfort` block was captured. The rig restored the installed DLL (SHA-256 verified) and the saves.
Re-run when Dishonored 2 launches from this Steam account (the family-sharing lender must not be playing).

# v609 headset feedback: checklist (Tier C)

Status: not run. Tier A (unit and routing checks) and Tier B (sim runs) proved the logic and the measured residuals only. The sim could not reproduce the arm and running problem, so the headset trace is the ground truth. Whether things feel right on your hands and in your eyes can only be judged here.

If anything below misbehaves, switch off the matching change (see the knobs in each item) and report the item.

## Before you start

- Note the DLL hash of `Dishonored2.exe`'s mod DLL you are testing, and the runtime (Steam Link or Virtual Desktop).
- Dev knobs must not be left in `build/` except the ones you are testing. Each knob below is an off-switch that is absent by default. Create the file to turn the change off.
- After each item, copy `xr-live-status.json` and keep the block named in the item.

## Running knobs (arms and hands while moving)

All three fixes shipped, each with an off switch:

- `build/legacy-arm-root-carry`: restores the old root carry for the arms.
- `build/no-hand-body-feedforward`: turns off the feedforward on the hand body. A sudden stop can then leave the body a few centimetres short, but never more than the old path.
- `build/no-moving-controller-visual`: keeps the drawn controller on the sensor pose while moving.

The diagnostic trace is `build/locomotion-trace`. It writes `artifacts/locomotion-hands.csv`. Watch these columns: `root_gap_valid` (1 when the root gap was measured, 0 when it was not, so a 0 gap from an unmeasured frame is not a true zero) and `ff_dot_target` (the feedforward direction against the target).

## Items

1. **Run and sprint with empty hands, the sword and the gun.** Watch for lag, shake and drift of the hands and weapons while moving. Create `build/locomotion-trace`, do a run and a sprint in each hand state, then save `artifacts/locomotion-hands.csv`. Record which of the three knobs shipped in your build. An A/B with each knob is optional. Expect: hands and weapons stay on the controllers while running, with no bursts on a sudden stop.

2. **Regrab the sword the other way round, five times.** Include grabs across the blade. Create `build/sword-grip-trace`. Expect: a reversed regrab shows reversed from the first frame, with no settle and no spin. Save the trace. A forward draw still settles for about 0.5 s; this is pre-existing. Tell me if you see it, and whether it is distracting.

3. **Fist with an empty hand.** Expect: no draw from a fist. The over-shoulder draw still works. Block and choke still work.

4. **Crosshair on the gun and two powers, at near and far targets, in both eyes.** Turn the crosshair off and on in the game. Check the HUD at 65 % and 200 %. Save the `crosshair` status block. Expect: the crosshair sits on the target in each eye, with no double image. Off hides it at once.

5. **Fresh-install AA on the RTX laptop.** Expect DLAA. Check the `aa_first_start` status block and the log, which lists the removed legacy knob lines. Then change AA in the game, tab back to the launcher, and check it shows the same value. Also check that HUD 125 %, 150 % and 200 % saves show in the launcher.

6. **Drag a corpse.** Expect it to be 2.5 times lighter than v608 and still controlled. Check that a prop still feels as it did in v608. Lab tests showed the old pull limit rarely bound in stiff lifts, so the corpse may still feel heavy. Tell me whether corpses really feel lighter. If they do not, say so: the weight comes from somewhere else, and that is the next thing to look at.

7. **Pick up a paper, a note and a stamp.** Expect the fingers to stop on the item. Save the `finger_ik.hold` block, with `thin_holds` and `box_holds`.

8. **Make a full fist with an empty hand.** Expect the fingertips to rest on the palm, not through it.

## Report

For each item: pass or fail, the DLL hash, the runtime, and the saved status block or trace for any fail. For item 6, give a yes or no on whether corpses feel lighter.

## Developer item (not a headset item)

- `tests/body_ik_check.cpp` could not be compiled on Linux because `player_clip_fixture.hpp` is absent. It needs a run in the Windows harness before the release.

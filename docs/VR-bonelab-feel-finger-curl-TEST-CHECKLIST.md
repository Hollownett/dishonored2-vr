# Bonelab feel Part 3: fingers follow the touch sensors, headset checklist (Tier C)

Status: not run. Tier A (the curl maths) and Tier B (scripted touch pose, S10) proved the mapping and the status block only. Whether
it looks and feels right on your hands can only be judged here. The curl is visual only: it never changes grab, Use, attack, aim,
gestures or Blink. If anything below misbehaves, create an empty file `build/legacy-finger-curl` (or switch off "Fingers follow the
controller's touch sensors" in the launcher Features) and report the item.

## Before you start

- Note the DLL hash. Dev knobs must not be left in `build/` except the ones you are tuning (`finger-touch.cfg`).
- Use the Quest controllers through your usual runtime. Item 6 is about the runtime, so run it for Steam Link and again for
  Virtual Desktop.
- After each item, copy `xr-live-status.json` and keep its `fingers` block: `mapping`, `touch_available`, `touch_silent`, `raw`,
  `curl` (thumb, index, middle, ring, pinky per hand), `floor_active`, `touch_bound`.

## Items

1. **Rest a finger on the trigger.** Hand empty, rest the index on the trigger without pressing. Expect: the index sits about half
   bent (0.3), the other fingers open, and `fingers.mapping` is `touch` for that hand. Lift the finger: the index opens in about
   a tenth of a second.
2. **Point.** Make a fist and keep the index off the trigger (grip held). Expect: the index stays straight, middle, ring and
   pinky curl, the thumb follows what it touches.
3. **Thumbs-up.** Grip held, thumb clear of the stick, face buttons and thumbrest. Expect: the thumb is straight up and the fingers
   curled. Put the thumb back on a button: it curls within about a twentieth of a second without a visible pop.
4. **Hold a bottle and a body.** Pick up a bottle, then drag a body by a limb. Expect: the fingers wrap and stay at least firm
   (never more open than about 0.6 on the four fingers) even if you relax the grip a little while holding. Release: the hand opens.
5. **Chatter.** Slowly press and release the trigger, squeeze the grip slowly and roll the thumb across the stick. Report any
   flicker, twitching or jumping fingers, and in which pose it happened (and whether `touch_silent` flipped).
6. **Your runtime's sensors.** In the status, check `touch_available` and `touch_silent` for both hands while you touch and release
   the trigger and thumb sensors. Expect: `touch_available` true, `touch_silent` false, `raw.trigger_touch` and `raw.thumb_touch`
   follow your fingers. If the runtime reports no touch (`touch_silent` true, `mapping` legacy), note which runtime it was; the
   hand then keeps today's controller-value mapping.
7. **Resting index half-bend.** Is the resting index bend (0.3) right for you? Tune it in `build/finger-touch.cfg` (touch value,
   both slews, smoothing rate, floor); a malformed file falls back to defaults with a note in `finger-touch.txt`.
8. **Sword and gun hands.** Draw a sword and a gun. Expect: the hand holding the weapon still shows the native fist, unchanged
   from before; the empty hand uses the touch curl. Holster and the hand returns to the touch curl.

## Report

For each item: pass or fail, the DLL hash, the runtime, and the saved `fingers` block for any fail.

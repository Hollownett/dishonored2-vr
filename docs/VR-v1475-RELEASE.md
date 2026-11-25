# v1475 test build: Death of the Outsider in VR, and the grip cancels a power (pre-release, 2026-11-25)

A test build from the branch `feat/doto-vr`. **It has not been played in a headset yet.** Everything below was checked in simulated
VR sessions of the real games: scripted head and controller input, the game's own state read back, and eye captures. Each item
says how far it was checked: **sim-proven** (seen working in a simulated session), **built** (in the build but not seen working
yet), or **not done**. Comfort and feel always still need a person in a headset. If you want the last headset-tested build for
Dishonored 2, stay on v1457.

## Death of the Outsider (new)

How to start it: in the launcher's **Play** tab, pick **Death of the Outsider** in the game switch above the page, then press
**Launch VR** as for Dishonored 2. The Outsider has its own mod folder (`%LOCALAPPDATA%\DishonoredVR-DOTO`) and can copy your
Dishonored 2 VR settings. **Uninstall proxy (DOTO)** on the same page makes the game flat again.

### Billie's powers
- **Displace** (sim-proven): hold the trigger and point with your hand; the game aims along your hand, not your head. Release
  places the marker; a quick tap swaps you to it, with a short fade. Not yet measured: exactly where the marker lands
  relative to the hand.
- **Foresight** (sim-proven): your head tracking and turning keep working while you scout as the spirit. They stopped before
  this build. Billie's body does not hang under the spirit, and there is a short fade when you snap back. Not done: tagging by
  pointing, and a line back to your body.
- **Semblance** (built): choke someone, then pull the power trigger. The trigger now reaches the game even with that hand in a
  fist. Not seen working yet: no test save had a guard to choke.
- **Void Strike** (built): the game needs a 0.5 s held attack, and the VR sword charge pressed for only 0.34 s. It now holds long
  enough. Not seen working yet: no test save owns Void Strike.

### Voltaic Gun on your forearms
- **Drawn on the forearm** (sim-proven): the game keeps the equipped gun hidden, so the left forearm looked empty before. Eye
  captures show the gun on the right forearm(s) with one gun on either arm or both.
- **Stays on the forearm while firing** (sim-proven): during its fire animation the left gun was drawn up near the ceiling for a
  moment. It now stays on your forearm.
- **Both guns fire** (right gun sim-proven, left partly): the right gun's shot created a projectile that flew. The left gun's
  fire input reached the game and its fire animation played, but nothing counted its projectile.
- **Bolt type per hand** (sim-proven): each hand's weapon wheel picks the bolt for that hand's gun, and each gun keeps its own.
  Not checked: that the right gun's projectile looks different per bolt.
- **The sword hand** (sim-proven): a hand holding the sword shows no crosshair for its forearm gun and never fires it.

### Hook Mine
- **Lethal / non-lethal** (sim-proven): hold the trigger with the mine in your hand for 0.3 s, as holding the button does in the
  flat game, and the game switches the mine's mode. You feel a tick when it switches and a double tick when you let go. A short
  press throws it. Checked with the game's own mode flag: lethal, then back to non-lethal, with no throw from the holds. The
  mine in your hand does not change colour; the game shows the mode elsewhere.
- **Thrown mines stick** (sim-proven): throw it with your hand (open the grip while you swing) and the game places it along the
  throw, as its own throw does. Opening the hand slowly still just drops it.

## Both games

- **The grip cancels a power you are aiming** (GitHub issue #6, sim-proven in both games): aim Blink, Far Reach, Displace or any
  held power with the trigger, then squeeze the grip of that hand. The aim ends without casting, with a short tick. A normal
  press and release still casts. Checked with Displace and with Blink. Your saves had no Far Reach, but it goes through the same
  game code.
- **Takedown reach** is back on. It had been switched off by mistake in the test builds of this branch since v145743.

## Dishonored 2

- **Possession** (built): while you possess a creature or a person, the stick moves it where you look and the right stick turns
  it. Not seen working yet: no test save had Possession. Please try this one first and say how it feels.

## If something feels wrong

An empty file with one of these names undoes that change. Put it in `%LOCALAPPDATA%\DishonoredVR\build` for Dishonored 2 or in
`%LOCALAPPDATA%\DishonoredVR-DOTO\build` for the Outsider. The first five are also rows in the launcher's feature list.
- `no-power-grip-cancel`: the grip power cancel
- `no-possession-view-move`: Possession movement
- `no-mine-throw-stick`: thrown mines stick
- `no-mine-mode-hold`: the Hook Mine trigger pass-through
- `no-teleport-fade`: the fades on Displace and Foresight
- `no-sword-hand-reticle`: hiding the sword hand's gun crosshair
- `no-voltaic-native-place`: keeping the left gun on the forearm while it fires

## Known issues

- Not tested in a headset. The full list of what to check is in `docs/VR-DOTO-2026-11-24-TEST-CHECKLIST.md`.
- Both DLLs and the launcher were built on Linux, not with the usual Windows build.

## Linux (Proton)

Dishonored 2: `linux/d2vr-linux install --zip d2vr-v1475-dll.zip`. Death of the Outsider:
`linux/d2vr-linux --game doto install --zip d2vr-doto-v145774-dll.zip`. See `linux/README.md`.

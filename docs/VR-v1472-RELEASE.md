# v1472 test build: Death of the Outsider in VR, and the grip cancels a power (pre-release, 2026-11-25)

A test build from the branch `feat/doto-vr`. **It has not been played in a headset yet.** Everything below was built and checked
in simulated VR sessions of the real games (scripted head and controller input, status logs and eye captures). Comfort and feel
still need a person in a headset. If you want the last headset-tested build for Dishonored 2, stay on v1457.

## Death of the Outsider (new)

How to start it: in the launcher's **Play** tab, pick **Death of the Outsider** in the game switch above the page, then press
**Launch VR** as for Dishonored 2. The Outsider has its own mod folder (`%LOCALAPPDATA%\DishonoredVR-DOTO`) and can copy your
Dishonored 2 VR settings. **Uninstall proxy (DOTO)** on the same page makes the game flat again.

Billie's powers:
- **Displace:** hold the trigger and point with your hand: the marker follows the hand, not your head. Release places it; a quick
  tap swaps you to it, with a short fade.
- **Foresight:** your head and turning keep working while you scout as the spirit; a short fade when you snap back to your body.
- **Semblance:** choke someone, then pull the power trigger: the trigger works even with that hand closed in a fist.
- **Void Strike:** the sword charge (trigger held with the sword still, or the fist-by-the-ear pose) now holds the attack long
  enough for the game to turn it into a Void Strike once you own it.

Voltaic Gun on your forearms:
- The gun is drawn on the left forearm when it is equipped. Before, the game kept it hidden, so the forearm looked empty.
- Each forearm keeps its own bolt type. Each hand's weapon wheel picks the bolt for that hand's gun.
- A hand holding the sword shows no crosshair for its forearm gun and never fires it. Blocking with the sword works again when that
  forearm also carries a gun.

Hook Mine:
- Hold the trigger with the mine in your hand to switch it between lethal and non-lethal, as holding the button does in the flat
  game. You feel one tick when the switch starts and a double tick when you let go. A short press throws it.
- Throw it with your hand (open the grip while you swing) and it sticks where it lands, like the flat game's throw. Opening the
  hand slowly still just drops it.

## Both games

- **The grip cancels a power you are aiming** (GitHub issue #6). Aim Blink, Far Reach, Displace or any held power with the
  trigger, then squeeze the grip of that hand: the aim ends without casting, with a short tick. Before, the grip cast the power.
  A normal press and release still casts.

## Dishonored 2

- **Possession:** while you possess a creature or a person, the stick moves it where you look, and the right stick turns it.
  This is the least tested change. No test save had Possession, so it has not even been run in a simulated session. Please try
  it and say how it feels.

## If something feels wrong

An empty file with one of these names undoes that change. Put it in `%LOCALAPPDATA%\DishonoredVR\build` for Dishonored 2 or in
`%LOCALAPPDATA%\DishonoredVR-DOTO\build` for the Outsider. Each one except the last is also a row in the launcher's feature list.
- `no-power-grip-cancel`: the grip power cancel
- `no-possession-view-move`: Possession movement
- `no-mine-throw-stick`: thrown mines stick
- `no-mine-mode-hold`: the Hook Mine trigger hold
- `no-teleport-fade`: the fades on Displace and Foresight
- `no-sword-hand-reticle`: hiding the sword hand's gun crosshair

## Known issues

- Not tested in a headset. The full list of what to check is in `docs/VR-DOTO-2026-11-24-TEST-CHECKLIST.md`.
- The right forearm's gun is a copy the mod adds. Its shots use its own bolt, but whether its model shows that bolt was not
  checked.
- Both DLLs were built on Linux with clang-cl, not with the usual Windows build.

## Linux (Proton)

Dishonored 2: `linux/d2vr-linux install --zip d2vr-v1472-dll.zip`. Death of the Outsider:
`linux/d2vr-linux --game doto install --zip d2vr-doto-v145770-dll.zip`. See `linux/README.md`.

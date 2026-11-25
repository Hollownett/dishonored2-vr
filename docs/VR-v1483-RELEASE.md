# v1483 test build: Death of the Outsider in VR, AFW performance mode, Billie's powers (pre-release, 2026-11-25)

A test build from the branch `feat/doto-vr`: Dishonored 2 **v1483** and Death of the Outsider **v145785**. It replaces the unpublished
v1475 / v145774 package. **It has not been played in a headset yet.** Everything below was checked in simulated VR sessions of the
real games: scripted head and controller input, the game's own state read back, and eye captures.

Each item says how far it was checked:
- **sim-proven:** seen working in a simulated session.
- **built:** in the build, but not seen working yet.
- **not done.**

Comfort and feel always still need a person in a headset. If you want the last headset-tested build for Dishonored 2, stay on v1457.

## New: AFW in performance mode (both games)

Performance mode draws one eye per frame and rebuilds the other, for a higher frame rate. It now offers two ways to rebuild that eye:

- **Built-in warp:** the mod's own. It moves the eye's last image.
- **AFW (alternate frame warping)** (sim-proven): VR Stereo Hub's open-source rebuild, brought in by **Indurhra** (PR #7) and compiled
  into the mod; there is nothing to install. It rebuilds the eye from the fresh eye and its own last image. In the simulator it kept
  moving arms and hands solid where the built-in warp breaks them into see-through dots: 8-13% less error around moving arms, with a
  pistol held. The whole picture is about 10% softer. It also costs about half the GPU time of the built-in warp (0.6 ms against
  1.1 ms on a laptop at simulator size).

**How to choose:** in the launcher's Settings, set **Performance mode** to **Off**, **On (built-in warp)** or **On (AFW)**. In the
game, the VR menu's **Renderer** row switches between Native stereo, Alternate eye (built-in warp) and AFW live, with no restart.

Requirements:
- **Spacewarp off.** Spacewarp is still the default and comes first: with it on, the game renders both eyes every frame and the
  Renderer row says so. Turning Performance mode on in the launcher turns Spacewarp off.
- **An FSR or DLSS anti-aliasing mode.** Without one, AFW falls back to the built-in warp.

Checked in the simulator:
- Both games switched live between the three renderers with no failures and no pauses: 8,383 one-eye frames in Dishonored 2,
  10,171 in the Outsider. A session that started on AFW used to pause for about 7 s the first time it switched to the built-in
  warp (its shaders were compiled on the spot). They are now prepared during the menus, as are AFW's.
- With a Quest 3 field of view, AFW did 7,314 rebuilds and refused none.
- With Spacewarp on, the session stayed in native stereo and Spacewarp generated its frames.

## Death of the Outsider

How to start it: in the launcher's **Play** tab, pick **Death of the Outsider** in the game switch above the page, then press
**Launch VR** as for Dishonored 2. The Outsider has its own mod folder (`%LOCALAPPDATA%\DishonoredVR-DOTO`) and can copy your
Dishonored 2 VR settings. **Uninstall proxy (DOTO)** on the same page makes the game flat again.

### Billie's powers
- **Displace** (sim-proven): hold the trigger and point with your hand; the game aims along your hand, not your head. Release
  places the marker; a quick tap swaps you to it, with a short fade.
- **Foresight** (sim-proven): head tracking and turning keep working while you scout as the spirit; a short fade when you snap
  back. Not done: tagging by pointing, and a line back to your body.
- **Semblance** (sim-proven): walk up behind someone who has not noticed you until the game offers the choke, then press the power
  trigger and **hold it** until the face is taken. You can also take a face from a sleeping body: press the power trigger at the
  body. Fixed in this build: the power's own trigger no longer acts as a forearm gun, and a body under your hand no longer takes the
  power hand's trigger. Seen once in the simulator: in a busy bar the disguise dropped right after it was taken (not understood yet;
  possibly the game's witness rule).
- **Void Strike** (sim-proven): draw the sword from your shoulder and hold a charged strike; both strikes in the test committed.
  Fixed: the first strike of a session was released too early.

### Voltaic Gun on your forearms
- **Drawn on the forearm** and **stays there while firing** (sim-proven).
- **Charged shot** (sim-proven, both guns): hold the trigger. At 2 s you feel a tick and the shot is charged; at 4 s it fires
  overcharged by itself. The engine's gun shows its red charge lights; the second gun (the copy on the other forearm) does not, so
  its tick is the cue.
- **Bolt type per hand** (sim-proven): each hand's weapon wheel picks that gun's bolt.
- **The sword hand** (sim-proven): a hand holding the sword shows no crosshair for its forearm gun and never fires it.

### Hook Mine
- **Lethal / non-lethal** (sim-proven): hold the trigger with the mine in your hand for 0.3 s, as holding the button does in the
  flat game; a tick when it switches, a double tick when you let go. A short press throws it.
- **Thrown mines stick** (sim-proven): throw it with your hand (open the grip while you swing) and the game places it along the throw.

## Both games
- **The grip cancels a power you are aiming** (GitHub issue #6, sim-proven in both games): aim a held power with the trigger, then
  squeeze that hand's grip. The aim ends without casting, with a short tick. A normal press and release still casts.
- **DLSS** (from PR #7): DLSS features are always created with output regions. A session no longer falls back to FSR 3 with NGX
  error 0xBAD00005.
- **Faster memory checks** (from PR #7): the mod's safe memory reads keep a bigger cache. In Indurhra's measurements this saves
  about 3% of main-thread time in gameplay.

## Dishonored 2
- **Possession** (built): while you possess a creature or a person, the stick moves it where you look. This build also covers the
  time spent inside the host, not only the fly-in. Not seen working yet: a scripted possession in the simulator never got a host.
  Please try this one first and say how it feels.

## If something feels wrong

**AFW:** pick **Alternate eye (built-in warp)** or **Native stereo** in the VR menu's Renderer row, or set Performance mode to Off.

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
- AFW is softer than the real image and leaves small blocky edges behind fast-moving arms. While you walk, neither warp places the
  hands exactly where the game draws them.
- Both DLLs and the launcher were built on Linux, not with the usual Windows build.

## Credits
AFW: **Indurhra** (PR #7: the Dishonored 2 adapter, the region cache, the DLSS fix) and **VR Stereo Hub** (the open-source rebuild
from their Dishonored 1 VR mod, zlib license, `third_party/vsh_afw`; see `THIRD_PARTY_NOTICES.md`).

## Linux (Proton)

Dishonored 2: `linux/d2vr-linux install --zip d2vr-v1483-dll.zip`. Death of the Outsider:
`linux/d2vr-linux --game doto install --zip d2vr-doto-v145785-dll.zip`. See `linux/README.md`.

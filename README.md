# Dishonored 2 VR

A full VR conversion of **Dishonored 2** (Steam, PC) for PC VR headsets through OpenXR.
You play in first person with stereo 3D, head tracking and both hands: swing and throw the sword,
draw the pistol from your hip, grab guards and bodies, push doors open, read notes held in your hand,
and pick powers from the game's own wheel.

> **Status: test builds.** Each release is a numbered test candidate (for example v713). It is playable,
> but expect rough edges. Please report what you see in the headset.

## Features

- **Stereo 3D with head tracking** at your headset's resolution, with a comfortable snap or smooth turn.
- **Your own hands.** The player's hands and arms follow your controllers, with fingers that close as you squeeze.
- **Physical melee.** Swing the sword to attack, hold it in front of you to block, throw it and call it back.
  Unaware guards can be taken down from behind or choked out.
- **Body holsters.** Sword over the right shoulder, pistol or crossbow on the left hip. Health, mana and ammo are on a watch on the back of your left wrist.
- **Physical objects.** Grab, carry and throw bottles, bodies and loose props. Push doors open with your hand.
- **The game's own UI in VR.** Menus, the Quick Access Wheel, the journal and notes appear in front of you;
  point with the right controller to select.
- **Upscaling and anti-aliasing.** FSR 3 (default), experimental FSR 4, SMAA Ultra 2-pass, game TAA, and DLAA/DLSS on NVIDIA
  (with your own `nvngx_dlss.dll` from another game; nothing NVIDIA is bundled).
- **Cutscenes** in 3D with a comfort vignette, or on a flat theatre screen.
- **A launcher** that installs the mod, checks your setup, edits settings and starts the game in VR.

## Requirements

- Windows 10 or 11, 64-bit.
- **Dishonored 2, Steam edition**, installed and updated.
- A PC VR headset with an OpenXR runtime. Tested with **Meta Quest 3 / Quest 2** through
  **Virtual Desktop** (VDXR runtime, recommended). SteamVR / Steam Link also works.
- SteamVR installed (the mod loads the OpenXR loader from SteamVR's folder, even with Virtual Desktop).
- The Microsoft Visual C++ 2015-2022 x64 runtime.
- A strong GPU: the game renders both eyes at your headset's resolution. The game must run windowed or borderless.

Linux (Proton + SteamVR + Steam Link) has an experimental path; see [linux/README.md](linux/README.md).

## Installation

1. Download `DishonoredVRLauncher.exe` from the [Releases](https://github.com/Hollownett/dishonored2-vr/releases) page.
2. Put it in any folder and run it. Nothing else is needed: the launcher contains the mod and its runtime.
3. On first start it installs the mod into `%LOCALAPPDATA%\DishonoredVR`, and imports menu art and
   renders the tutorial pictures from **your own** Dishonored 2 install (no game files are distributed).
4. The **Play** page shows setup checks. Fix anything red; each item says how.

The launcher places a small loader (`dinput8.dll`) next to `Dishonored2.exe`. It only loads this mod and
never replaces a `dinput8.dll` from another mod.

## How to play

1. Start Virtual Desktop and connect your headset (or start SteamVR).
   In Virtual Desktop, set **Synchronous Spacewarp: Disabled** and the frame rate to 72 or 90 Hz.
2. Open the launcher and press **LAUNCH VR**. It starts the game through Steam, attaches VR at the title
   screen and continues your last save.
3. Put on the headset. VR starts about 20 seconds after the game window appears.
4. To stop VR, press **Stop VR** in the launcher, or just quit the game.

**Note:** while the loader is installed, every start of Dishonored 2 (also from Steam) starts in VR.
To play flat, use **Tools > Uninstall proxy** in the launcher with the game closed.
LAUNCH VR installs it again.

Settings: open the pause menu in the game and choose **VR Settings**, or use the launcher's **Settings** page.
You can change resolution, anti-aliasing and upscaling, sharpening, HUD size, turning, the monitor view and cutscene mode.
Optional features (full body, performance mode, Quest button labels and more) are under **Settings > Features**.

## Controls

Made for Meta Quest Touch controllers. **Grip** is the side button under your middle finger,
**trigger** is under your index finger.

<p align="center">
  <img src="launcher/assets/touch-plus-left.png" alt="Left Touch Plus controller" height="260">
  <img src="launcher/assets/touch-plus-right.png" alt="Right Touch Plus controller" height="260">
</p>

### Left controller

| Input | Action |
|---|---|
| Stick | Move where you look; click to sprint |
| X (hold) | Quick Access Wheel: point at an item, release to equip |
| Y | Talk (the trigger also talks); hold for the pause menu |
| Menu | Pause; hold for the journal; hold and flick the right stick for the quick slots |
| Trigger | Talk or pickpocket with a free hand; fire the gun or cast the power in a weapon hand |
| Grip | Use what the hand points at, or hold what it touches; hold at a keyhole to keep the peek; at your left hip, draw the pistol or crossbow |

### Right controller

| Input | Action |
|---|---|
| Stick | Turn (snap or smooth); flick down to crouch; click to zoom |
| A | Jump and climb |
| B (hold) | Put all weapons away |
| Trigger | Select in menus and on the wheel; talk or pickpocket with a free hand; with the sword, hold to charge a power strike and release to swing it |
| Grip | Use or hold, like the left one; over your right shoulder, draw the sword |

### Body and gestures

The sword and your hands need no buttons: attack, block, grab and throw with real movements.

| Where / what | Action |
|---|---|
| Right grip at your **right temple** | Open the controls guide |
| Either grip at your **left temple** | Open the journal |
| Right grip **over your right shoulder** | Draw the sword; release there to sheathe |
| Left grip at your **left hip** | Draw the pistol or crossbow; release to holster |
| Turn the **back of your left wrist** to your face | Read health, mana and ammo |
| **Swing** the sword | Attack. From behind an unaware guard it is a silent takedown |
| **Grip + trigger** with the sword in hand, held still | Charge a power strike (pulses rise in the hand); release to swing it hard. A real swing cancels the charge |
| Sword held **in front of you** | Block (the trigger is the charge, not a block) |
| **Release** the right grip mid-swing | Throw the sword. Grip over your shoulder calls it back |
| **Palm on a guard** + grip, then pull hard | Hold him, then throw him down |
| Grip an unaware guard's **neck from behind** | Choke him out |
| Grip a **limb or the head** of a body | Drag it; let go at your shoulder to carry it there |
| Grenade or mine: make a **fist**, open it mid-throw | Throw it (open it with the palm on a surface to place it) |
| **Open hand** against a door | Push it open; push slowly to peek |
| **Both hands up** over your head | Jump |
| **Lean or crouch** in real life | The character leans or crouches with you |
| In menus | Point with the right controller, pull the right trigger to select |

The same guide, with pictures, is in the game (right grip at your right temple, or VR Settings >
Controls & gestures) and on the launcher's **Controls** page. Full list: [docs/VR-CONTROLS.md](docs/VR-CONTROLS.md).

## Troubleshooting

- **Walking looks steppy or the game runs at half rate:** disable Synchronous Spacewarp in Virtual Desktop.
- **Nothing happens in the headset:** check the launcher's setup checks. The OpenXR runtime must be active
  (Virtual Desktop or SteamVR) and SteamVR must be installed.
- **The headset disconnected:** the mod reconnects by itself after a second or two.
- **Low frame rate:** try FSR Quality or Balanced, or a lower resolution scale in VR Settings, or the
  performance mode under Settings > Features.
- Logs and run records are in `%LOCALAPPDATA%\DishonoredVR\artifacts` (the launcher's Tools page opens it).
  Please attach them to bug reports.

## Uninstall

1. Close the game. In the launcher, choose **Tools > Uninstall proxy**.
2. Delete the launcher and the folder `%LOCALAPPDATA%\DishonoredVR`.

The mod does not modify game files. Settings it changes in the game while VR is on (motion blur,
depth of field, camera bob, HUD size) are restored when VR stops.

## Building from source

- The mod DLL: Visual Studio 2022 (C++ x64), `tools\build-msvc.cmd`. Checks and the simulator are described in
  [docs/TESTING.md](docs/TESTING.md).
- The launcher: .NET 8+ SDK, `cd launcher` then `dotnet publish -c Release`; see [docs/LAUNCHER.md](docs/LAUNCHER.md).
- The controller pictures: `python tools\render_touch_plus.py` (renders SteamVR's Touch Plus models).
- Detailed playing notes: [docs/PLAYING.md](docs/PLAYING.md). Development history: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Credits and license

The mod's own code is released under the [MIT License](LICENSE).

- Grab mathematics, snap-turn and menu-tap policy adapted from
  [Dishonored-VR](https://github.com/VR-Stereo-Hub/Dishonored-VR) (zlib license).
- Khronos OpenXR SDK and loader, MinHook, SMAA, AMD FidelityFX FSR 3 and FSR 4, NVIDIA DLSS (not bundled).
  Their licenses and exact sources are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- No game assets are included: all art the mod shows is read from your own install at run time.

Dishonored 2 is a trademark of ZeniMax Media Inc. (Arkane Studios, Bethesda Softworks). Meta Quest and Touch Plus
are trademarks of Meta Platforms, Inc. This is an unofficial fan project, not affiliated with or endorsed by them.


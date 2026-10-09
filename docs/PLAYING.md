# Playing Dishonored 2 in VR

## Start
1. Connect the Quest through Virtual Desktop (VDXR runtime).
   In Virtual Desktop Settings > Streaming, set Synchronous Spacewarp to Disabled and the frame rate to 72 or 90.
   With SSW on, the game runs at half rate and walking looks steppy.
2. In PowerShell in this folder, run `.\tools\run_demo.ps1 -Launch`. This starts Dishonored 2 through Steam if it isn't running.
3. Load a save. The game must be windowed or borderless.
   VR starts as soon as you are in the game, playing or paused, and the headset is available.
   Ctrl+C returns to the desktop and restores all settings.

If the headset disconnects or you switch Virtual Desktop between desktop and VR, the mod reconnects by itself after a second or two.

## Controls (Quest Touch)
| Input | Action |
|---|---|
| Left stick | Move in the direction you look; click to sprint |
| Right stick left/right | Snap turn (45°) |
| Right stick click | Zoom (spyglass) |
| X held | Quick Access Wheel: pick with the right stick, release X to equip |
| A | Jump and mantle |
| B | Holster weapons |
| Right stick down (flick) | Crouch / stand |
| Grip of the hand you point with | Interact with what that hand points at (doors, loot, levers). Reach out with a hand to make it the pointing hand |
| Y held + right stick | Lean. You can also lean physically with your head |
| A fast right-hand swing | Sword attack (the right trigger only interacts) |
| Left trigger | Use the power or weapon in your left hand, aimed by your left hand |
| Grips | Close the hand. Near a loose object, grab it; let go to drop it, or let go while swinging to throw. A trigger pulled while grabbing never attacks or fires. A hand already holding a pistol or crossbow does not grab |
| Right grip with nothing to grab | Block. Behind an enemy, it chokes them |
| Menu button | Pause. Hold for the journal; hold with the right stick for the quick slots |

In menus, point with the right controller and pull the right trigger to click.

In gameplay a small ring floats ahead of the hand you point with: powers and weapons aim along that ray.
Raise your left hand in front of your face and turn it like checking a watch to see a controls card.

## VR settings
Press Y while the game is paused. From there you can set:
- resolution scale
- temporal AA (off by default: in stereo it still leaves a doubled outline on moving objects)
- edge smoothing, SMAA (on by default)
- sharpening (off, low, medium, high, max; default medium)
- aim reticle (on/off) and controls card on the left wrist (on/off)
- monitor view: what the desktop game window shows while you play - both eyes side by side (default), left eye or right eye
- turning (snap or smooth)
- recenter
- body calibration: stretch both arms sideways, hold both grips closed and keep still until the line fills

Settings are saved for the next session.

## What changes in the game while VR is on
These change only while VR is on, and are restored on exit:
- camera bob and shake, camera motion blur, depth of field and dynamic resolution are switched off;
- the game's TAA runs with a separate history per eye when Temporal AA is on, and is skipped when it is off;
- texture filtering is set to 16x anisotropic.

In menus and while the VR settings panel is open the desktop window shows the normal game image.

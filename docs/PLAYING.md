# Playing Dishonored 2 in VR

## Start
1. Connect the Quest through Virtual Desktop (VDXR runtime).
   In Virtual Desktop Settings > Streaming, set Synchronous Spacewarp to Disabled and the frame rate to 72 or 90.
   With SSW on, the game runs at half rate and walking looks steppy.
2. In PowerShell in this folder, run `.\tools\run_demo.ps1 -Launch`. This starts Dishonored 2 through Steam if it isn't running.
3. Load a save. The game must be windowed or borderless.
   Enter gameplay for initial startup; you can pause once VR is running.
   Restarting while paused can fail if the native renderer has no prepared scene camera.
   Ctrl+C returns to the desktop and restores all settings.

If the headset disconnects or you switch Virtual Desktop between desktop and VR, the mod reconnects by itself after a second or two.

v205 also restores the owned headset-sized native viewport after a window/focus
reset. The old mismatch could leave a small image in the top-left of the VR texture.

The experimental FSR 4.1.1 backend is requested with
`.\tools\run_demo.ps1 -Fsr4` (add `-Launch` to start the game through Steam).
Your saved Native/Quality/Balanced selection is preserved. The VR settings image
shows FSR 4 when the genuine backend is active, or FSR 3 on fallback. GPU checks
pass; genuine FSR 4 dispatches are verified in the native game, while headset
quality and performance gains remain unverified. FSR 4
cost more than FSR 3.1 in the synthetic checks, so normal launches keep FSR 3.1.

## Controls (Quest Touch)
| Input | Action |
|---|---|
| Left stick | Move in the direction you look; click to sprint |
| Right stick left/right | Snap turn (45°) |
| Right stick click | Zoom (spyglass) |
| X held | Quick Access Wheel: pick with the right stick or move your left hand toward the item (a light tick per item), release X to equip |
| A held (about 0.35 s) | Same Quick Access Wheel for the right hand: pick with the left stick or move your right hand toward the item, release A to equip. A quick tap is still a jump. Whichever hand opens the wheel owns it until release |
| A | Jump and mantle |
| B | Holster weapons |
| Right stick down (flick) | Crouch / stand |
| Sit or crouch down in real life | The character crouches; stand up to stand. The view keeps your real head height |
| Grip of the hand you point with | Interact with what that hand points at (doors, loot, levers). Reach out with a hand to make it the pointing hand |
| Y held + right stick | Lean. You can also lean physically with your head |
| A fast right-hand swing | Sword attack |
| Right trigger | Block; the sword hand keeps following your controller |
| Sword held sideways across your body, in front of your chest or face | Block, like the right trigger |
| Behind an unaware enemy: a quick sideways slash of the right hand near your neck height | Lethal takedown (a normal sword swing also works). Right grip/trigger held = choke |
| Left trigger | Use the power or weapon in your left hand, aimed by your left hand |
| Grips | Close the hand. Near a loose object, grab it; let go to drop it, or let go while swinging to throw. Either hand can grab while a sword, pistol or crossbow is equipped. A trigger pulled while grabbing never attacks or fires |
| Right grip with nothing to grab | Block. Behind an enemy, it chokes them |
| Menu button | Pause. Hold for the journal; hold with the right stick for the quick slots |

In menus, point with the right controller and pull the right trigger to click.

Raise your left hand in front of your face and turn it like checking a watch to see a controls card.

## VR settings
Open the game's pause menu and choose **VR Settings**. Its controls appear in
the native menu image, beside the game's navigation. Point with the right
controller and use its trigger to select. Y while paused remains a shortcut.
From there you can set:
- resolution scale
- anti-aliasing: FSR Native, FSR Quality (renders at 77% and upscales), FSR Balanced (67% and upscales), Game TAA or Off. The displayed generation reflects the active backend. Your saved selection is preserved. Reduced input resolution is verified; headset FPS gains remain unmeasured.
  The FSR 3 Quality/Balanced modes upscale after the game's tonemapping, so the HUD stays sharp. Combine Quality with a higher resolution scale for a sharper image at the same cost
- edge smoothing, SMAA (on by default; skipped automatically while FSR 3 is on)
- sharpening (off, low, medium, high, max; default medium)
- HUD size (55%, 65% default, 75%, 85%, 100% = the game's own layout) and controls card on the left wrist (on/off)
- monitor view: what the desktop game window shows while you play - both eyes side by side (default), left eye or right eye
- turning (snap or smooth)
- recenter
- legacy avatar calibration; it is not required by the current hands-only mode

Settings are saved for the next session.

The current launch profile uses FSR 3 Native. Resolution, movement, snap angle,
smooth-turn speed, HUD size and monitor-view preferences were preserved.

## What changes in the game while VR is on
These change only while VR is on, and are restored on exit:
- camera bob and shake, camera motion blur, depth of field and dynamic resolution are switched off;
- with FSR 3 or Game TAA, each eye keeps its own history and the projection gets the game's sub-pixel jitter (it was missing, which is why TAA used to blur and double edges);
- the HUD is drawn smaller about the centre (65%) so health, mana and prompts stay in view;
- texture filtering is set to 16x anisotropic.

In menus and while the VR settings panel is open the desktop window shows the normal game image.

## Aiming, grabbing and the magic line
- The game's crosshair sits on your left hand: powers, pistol and crossbow aim and fire along it. It is drawn in your view where the left hand points, not on the HUD screen.
  With the pistol (or crossbow) drawn, the crosshair and the shots follow the gun's own barrel, so aim by pointing the gun.
  A fine trim, if ever needed: `%LOCALAPPDATA%\DishonoredVR\build\aim-offset` with "yaw pitch" in degrees (left, up).
- While the pistol or crossbow is drawn, holding the sword in front does not block (the game would put the gun away to block).
  Put the gun away to block again.
- Use prompts ("Take", "Open", "Eat") appear beside the hand that points at the object, left or right. Kill/choke takedown prompts float above the guard (at head height when you are close). Messages without a target appear on the HUD screen below your gaze.
- Health and mana sit in the top-left corner of the HUD screen, ammo just below. They are also a small watch on the back of each wrist: turn the back of the wrist toward your face. Palm toward your face shows the controls card.
- Journal (objectives, notes): touch your left temple with either hand and squeeze grip. Holding Menu still works.
- Sword: reach your right hand over your right shoulder and squeeze grip to sheathe or draw it.
- Attacks need a real arm swing; turning only the wrist never attacks. A fast blade that touches an enemy hits at that moment (the game's own damage, reactions and alerts); a takedown from behind stays the game's assassination.
- The sword, pistol and crossbow stop at walls and push loose props; touching a surface plays the game's own sparks, dust and impact sound, quieter than a hit.
- Either hand can grab or use things. A native interaction keeps its selected hand until that grip is released. When a hand points at something the game highlights,
  a thin blue ink line connects the hand to it; squeeze that hand's grip to use it (open a door, take loot, pull a lever).
- Near a loose prop the grip grabs it instead. Release to drop, or release while swinging to throw.
- The cue also marks eligible nearby physics props. It disappears while holding
  the prop and uses the same target selection as pickup. Native collision still
  applies; a closed cabinet can prevent a grabbed object moving freely.

## Menus and cutscenes
- The Quick Access Wheel is a world panel at your left hand's opening position;
  it stays in place while you select. Journal and note art follows the left hand.
  The world keeps stereo and render-only head tracking behind these panels.
- Menus appear upright about a metre in front of you and stay put while you read; turn away and they glide back.
  Squeeze the left grip to move the menu to where you are looking.
- Menus with tabs (Options > Controls, Visuals, Gameplay): each grip squeeze changes the tab once. The right grip goes
  to the next tab, the left grip to the previous one (and also re-places the menu).
- Cutscenes play on a large screen in front of you.
- Hands and equipped items remain tracked during ordinary sword attacks and
  blocking. Arm and forearm geometry is hidden; full arm tracking and its
  calibration are deferred. Verified authored full-body actions retain native
  ownership of the hands while the action plays.

The v199 build passed automated and actual-game simulated-controller checks.
Headset appearance, combat feel, note readability and complete gameplay have not
been accepted. See [the verification report](feedback-2026-10-10.md).

## Experiments (files in the build folder)
- `hud-scale` containing e.g. `0.75`: how far the HUD is pulled towards the centre (1 = the game's own layout, default 0.65).
- `no-physical-crouch`: turn off crouching by sitting down. Full arms are currently disabled regardless of `no-full-arms`.
- `power-gestures`: cast the equipped left-hand power with gestures (open-palm push, clap, palm at the temple; while aiming Blink, flick the hand toward you to blink). Off by default until tested.
- `fsr` containing `sharpness mvx mvy jitterx jittery` (default `0.3 -1 -1 1 -1`): FSR 3 tuning. `no-fsr-reactive`: turn off the hand/weapon ghosting guard.
- `no-hand-crosshair`, `no-hand-shots`: switch off the crosshair move or hand-aimed shots.
- `no-vr-glyphs`: keep the Xbox button pictures in prompts instead of the Quest control labels.
- `slash-speed` containing 1-2 (default 1.7): play rate of the game's own sword attack. `no-slash-speed`: the game's speed.
- `no-takedown-range`: the game's choke takedown range (VR default: distance, height and angle x1.5).
- `gun-shape` containing `up back length radius` in metres (pistol default `0.06 0.06 0.36 0.035`): collision shape of the left-hand weapon.
- `no-contact-damage`: sword damage only from the game's own attack animation. `no-impact-fx`: no surface sparks/sound from the weapons. `no-weapon-collision`: weapons pass through walls.
- `hud-census`, `marker-probe`: diagnostics only, they write CSV logs to the output folder.

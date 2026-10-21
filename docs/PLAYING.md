# Playing Dishonored 2 in VR

The next combined feedback/physical-hand test is v553; see
[its plan](HEADSET-REPAIR-v553-PLAN.md) and [ordered headset checklist](HEADSET-REPAIR-v553-CHECKLIST.md). It is not a release-ready build.
The default launcher manifest is an older development candidate, not this repair.

## Start
1. Connect the Quest through Virtual Desktop (VDXR runtime).
   In Virtual Desktop Settings > Streaming, set Synchronous Spacewarp to Disabled and the frame rate to 72 or 90.
   With SSW on, the game runs at half rate and walking looks steppy.
2. Only when the repair manifest exists and permits testing, run `.\tools\run_demo.ps1 -CandidateManifest artifacts/repair-v553-test-candidate.json -Launch`. A blocked manifest stops before launching the game. v553 requires real tracked arms and the existing full-body configuration; its temporary full-hand query switch is scoped to the session.
3. Load a save. The game must be windowed or borderless.
   Enter gameplay for initial startup; you can pause once VR is running.
   Restarting while paused can fail if the native renderer has no prepared scene camera.
   Ctrl+C requests return to desktop. Require verified restoration and hook drains before calling it clean exit. v546 removes the shared-takedown restoration dependency on a later Stage0 callback; other subsystem and real pause/transition exit checks remain acceptance gates.

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

## Sound
VR audio now follows the centre of the visible head, including head turns and
the admitted lean/crouch offset. Scripted camera scenes retain their native
audio orientation. Headphone panning is selected for the VR session; the
previous mode is restored on exit. This uses the game's existing spatial mix;
headset listening validation is still required. See [audio evidence and checks](native-vr-audio.md).

## Controls (Quest Touch)

| Input | Action |
|---|---|
| Left stick | Move in the direction you look; click to sprint |
| Right stick left/right | Snap turn (45°) |
| Right stick click | Zoom (spyglass) |
| X held | Quick Access Wheel: pick with the right stick or move your left hand toward the item (a light tick per item), release X to equip |
| A held (about 0.35 s) | Same Quick Access Wheel for the right hand: pick with the left stick or move your right hand toward the item, release A to equip. A quick tap is still a jump. Whichever hand opens the wheel owns it until release |
| A | Jump and mantle |
| Both hands raised fast above your head | Jump too (both triggers released, nothing held); keep them up to mantle |
| Open hand (or the sword or pistol, held still) pressed into a closed door | Opens it, away from you, as far as you push it: push slowly to open it a little and peek, push fast to swing it fully open. A locked door rattles |
| B held | Holster weapons (the game's own sheathe) |
| Right stick down (flick) | Crouch / stand |
| Sit or crouch down in real life | The character crouches; stand up to stand. The view keeps your real head height |
| Grip alone (about 45% squeeze, v620 candidate) | Use offered doors, keyholes, ammo, loot and levers, or the offered body carry; it fires about 55 ms sooner when the trigger finger stays off the trigger and the game is asked for the target while you squeeze. In the v548 test candidate, an actual native choke hint gives the right grip a nonlethal choke hold instead; being behind an NPC alone does not |
| Full fist (grip + trigger, grip about 45%) | Grab physically: a body by a limb, a bottle, a glass, an ashtray, any loose prop. The fingers wrap whatever the hand holds. Once held, the grip alone keeps it |
| Right grip over your right shoulder | Draw the sword. It stays in your hand only while you keep squeezing (see *Weapons in your hands*) |
| Left grip at your left hip | Draw the pistol or crossbow. It stays in your hand only while you keep squeezing |
| Y on an offered Talk prompt (v553 test candidate) | Talk to that exact NPC or open the shop; the prompt shows Y. Grip no longer commits that Talk offer. In gameplay Y has no other action |
| Move your head sideways | Lean physically. The v553 test candidate removes button-driven lean; Y does not modify stick movement or turning |
| A fast right-hand swing | Sword attack |
| Sword held in front of your chest or face, any angle | Block. Small parry moves keep blocking; only a real swing attacks. A power or shot from the left hand briefly lowers the block, then it comes back |
| Right trigger with the sword in hand | Block too |
| Behind or beside an unaware enemy: a calm, deliberate stab or slice (about walking-pace hand speed, at least 0.6 m/s; leg, torso or head, no aiming at the neck needed) | Silent lethal takedown: the contact itself is the hit and replaces the alerting sword hit. A thrust at a guard the game offers a takedown on works too. Facing a guard, or one who already noticed you, is an ordinary hit. Ordinary hits still need a real swing (1.5 m/s); a calm stroke on a guard who has noticed you does nothing. A resting or drifting hand never kills. `build\stealth-speed` sets the calm threshold (default 0.6 m/s, range 0.3 to 1.5). Set the knob `no-stealth-contact` to turn the contact takedown off. This is separate from the nonlethal grip hold and physical full-fist grab |
| Left trigger | Use the power or weapon in your left hand, aimed by your left hand. A gripped gun fires; a full fist never casts |
| Hold right grip while the game offers its Choke hint (v548 test candidate) | Start a nonlethal choke, then keep holding to finish. No artificial gaze wait or neck-pose requirement. Body carry and full-fist physical grab stay separate |
| Menu button | Pause. Hold for the journal; hold with the right stick for the quick slots |

In menus, point with the right controller and pull the right trigger to click.

Raise your left hand in front of your face and turn it like checking a watch to see a controls card.

## VR settings
Open the game's pause menu and choose **VR Settings**. Its controls appear in
the native menu image, beside the game's navigation. Point with the right
controller and use its trigger to select. Y while paused remains a shortcut.
During play, hold left Y for about half a second to open the settings; a short
Y still talks to an NPC. The game keeps running behind the panel (it does not pause). Press Y again or B to close. The `no-hold-y-menu` knob
(or the launcher's "Hold Y for VR settings" switch) turns the hold off.
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
- Attacks need a real arm swing; turning only the wrist never attacks. A fast blade that touches an enemy hits at that moment (the game's own damage, reactions and alerts); a takedown from behind stays the game's assassination.
- The sword, pistol and crossbow stop at walls and push loose props; touching a surface plays the game's own sparks, dust and impact sound, quieter than a hit.
- Either hand can use or grab things. When a hand points at something the game highlights, a thin blue ink line connects the
  hand to it; squeeze that hand's grip alone to use it (open a door, peek through a keyhole, take ammo, pull a lever). The
  interaction keeps that hand until the grip is released. Pointing at nothing, the grip uses what you look at (including
  carrying a body, but excluding Talk in the v548 test candidate). A hand too far from you to reach anything, such as one held at the floor while you stand, also uses
  what you look at.
- Carrying a body over your shoulder: the view does not follow your head into the body when you lean toward that shoulder.
- To hold something physically, make a full fist (grip + trigger): a loose prop, a bottle, a body by a limb. Release the grip to
  drop it, or release while swinging to throw. A fist on a bottle, glass or loot the hand points at takes it the game's own way
  (the bottle lands in that hand). A hand holding a weapon does not grab anything else. A trigger squeezed a little after the
  grip (within about half a second) also makes a fist when that hand has nothing to use.
- Grab a living guard by the chest, collar or arm with a full fist and yank hard: he falls into the game's ragdoll toward your
  pull and gets back up a few seconds later (alerted). In the v548 test candidate, a right-grip-only choke requires the game's actual Choke hint; proximity alone never authorizes it.
- Fingers close around whatever a hand holds, the same way for every object. A bottle held by its neck gets a closed fist,
  a wide object such as a bottle's body gets an open hand, and a hand resting on a table lies flat on it.
- An open, empty hand pushes loose light things at whatever speed the hand moves: slide a bottle along a table, or swat a
  loose prop and it flies off. Heavy objects, walls and people still stop the hand, and the hand you see touches what it
  pushes. To pick something up, make a full fist.
- A prop thrown hard (faster than 3 m/s) into a guard hits him with the game's own physics damage: a big stagger and 2-8 damage
  depending on the speed. Bottles and other carryables keep the game's own throw.
- The cue also marks eligible nearby physics props. It disappears while holding
  the prop and uses the same target selection as pickup. Native collision still
  applies; a closed cabinet can prevent a grabbed object moving freely.

## Weapons in your hands
Weapons are held like real objects: a weapon stays in your hand only while you squeeze its grip.
- **Sword (on your back).** Reach your right hand over your right shoulder and squeeze the grip to draw it. Keep squeezing to
  hold it. Let go over the shoulder to sheathe it. Let go anywhere else and the sword falls out of your hand as a real sword in
  the world; let go during a swing to throw it. A thrown sword tumbles, glances off walls and lands flat; if it hits a guard
  it wounds him like a sword hit (the game's own damage, so he reacts and calls out); a guard who has not noticed you dies
  from one throw (the game's own stealth test, the one its takedowns use). Squeeze the grip next to a dropped sword
  to pick it up, or squeeze over your shoulder to call it straight back. A dropped sword also comes back by itself after 30 s
  or when you are 12 m away.
- **How you hold the hilt decides the grip.** At the shoulder, a fist pointing up draws the blade above the fist (normal grip),
  a fist pointing down draws it below the fist (reverse grip). Picking a sword up from the floor, a hand pointing along the
  blade gives a normal grip, against the blade a reverse grip. There is no button for it.
- **Pistol and crossbow (on your left hip).** The Quick Access Wheel puts the gun in your left hand; it stays there until you
  grip it. From then on it is held while you squeeze the left grip, and goes back to the hip when you let go. Squeeze the left
  grip at your left hip to draw it again. The trigger fires only a gun in the hand: an empty left hand's trigger does not pull
  the gun out of the holster.
- A sword you are not squeezing never attacks or blocks; the right trigger blocks only with the sword gripped.
- If the game takes the sword away (carrying a body, climbing, holding a bottle) and gives it back while you are not
  squeezing, it goes back on your back.
- B held still sheathes everything the game's own way.

## Menus and cutscenes
- The Quick Access Wheel is a world panel at your left hand's opening position;
  it stays in place while you select. Journal and note art follows the left hand.
  The world keeps stereo and render-only head tracking behind these panels.
- The Black Market is held in your left hand like a note; point at it with the right hand. If the left hand
  loses tracking it stands a metre ahead instead.
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
These are on by default in the launcher (except where a line says otherwise). Switch them in the launcher (Windows) or with `d2vr-linux feature` (Linux); a knob file you create by hand for a feature the launcher has switched off is set aside at the next start. The file names below are what the mod reads.
- `hud-scale` containing e.g. `0.75`: how far the HUD is pulled towards the centre (1 = the game's own layout, default 0.65).
- `no-physical-crouch`: turn off crouching by sitting down. Full arms are currently disabled regardless of `no-full-arms`.
- `power-gestures`: cast the equipped left-hand power with gestures (open-palm push, clap, palm at the temple; while aiming Blink, flick the hand toward you to blink). Off by default until tested; the launcher lists it as "new, untested" and does not enable it for you.
- `fsr` containing `sharpness mvx mvy jitterx jittery` (default `0.3 -1 -1 1 -1`): FSR 3 tuning. `no-fsr-reactive`: turn off the hand/weapon ghosting guard.
- `no-hand-crosshair`, `no-hand-shots`: switch off the crosshair move or hand-aimed shots.
- `no-hand-blink`: Blink (the teleport power) aims along the left hand's pointing direction while the left trigger is held, like the other left-hand powers; this switches it back to the head. Status `blink_aim` shows the hook (`routed`, `last_reason`, `last_dest`), and `native_left_aim` the left-aim counters.
- `no-vr-glyphs`: keep the Xbox button pictures in prompts instead of the Quest control labels.
- `slash-speed` containing 1-2 (default 1.7): play rate of the game's own sword attack. `no-slash-speed`: the game's speed.
- `no-takedown-range`: the game's choke takedown range (VR default: distance, height and angle x1.5).
- `no-stealth-contact`: a sword contact on a guard the engine calls unaware is no longer held for the native Assassination / stealth kill (it is the ordinary hit again).
- `gun-shape` containing `up back length radius` in metres (pistol default `0.06 0.06 0.36 0.035`): collision shape of the left-hand weapon.
- `no-contact-damage`: sword damage only from the game's own attack animation. `no-impact-fx`: no surface sparks/sound from the weapons. `no-weapon-collision`: weapons pass through walls.
- `no-hand-clamp`: the hands are drawn as before the physical-hands clamp (the chest-anchored palm hold), and shots leave from the controller again. Only the drawn hand and the shot origin change: throwing speed, grab and pickup selection and swing speed always follow the controller. For comparing in the headset; the status `hand_clamp.enabled` shows it. `hand-clamp-exclude-info-zero`: diagnostic for the hand clamp's sweep past a pushed prop (status `hand_clamp.exclusion`).
- `hud-census`, `marker-probe`: diagnostics only, they write CSV logs to the output folder.

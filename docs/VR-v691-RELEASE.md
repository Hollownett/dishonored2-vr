# v691 test release: the cure message where it can be seen, no sword on a grip, instant grenade

From the v690 headset session (2026-10-30 ~10:50-11:00) and the user's flat reference (the game shows
"Press RB to inject counter-serum." bottom-centre while sneaking up behind Hypatia, beside the choke/kill icons).

## Script messages are not HUD-panel content

The kill/choke hints are excluded from the HUD screen and drawn as their own world art. hud.iggy's script messages
(`_tutorialMessage` / `_informationMessage`, published since v677 to be shown ahead of the player) were still converted to
HUD-screen panel content in window mode, below the gaze, so the cure message was not seen; during the cure animation the
window is off and it appeared then. Only the tutorial movie's cards are panel content now; script messages keep their own
head-locked art (slot 11). General rule, no Hypatia-specific code. The kill/choke hint size is unchanged (v689/v690).

## A grip's Use is a press, not a hold

A held gamepad X is the game's holster/draw toggle. The grip held Use for as long as it was squeezed, so a grip on a
newspaper or a bottle could draw the sword instead of reading or taking it. Use was held only for the native shoulder carry
(v468), which is now hands-only, so a grip's Use is a 200 ms press (`build/legacy-corpse-carry` keeps the hold).

## Grenade leaves at the release

The launch is the ARK_MSG_WEAPON_FIRE event at 293 ms in the throw scene (gadget_grenade_throw_high.midnightscene), not a
delay value. Both throw states take that scene and its play rate from arkCpntWeaponThrowable slot 0x120 (0x633810,
grenades only); a hook there plays the throw scene 8x faster, so the grenade leaves ~40 ms after the hand opens (sound,
ammo and the switch to reload unchanged; the reload scene is untouched). Knob `build/grenade-throw-rate` (1 = native).
The v690 early release on the throwing motion is removed. Status `grenade_throw.scene_rate`, `scenes_sped`, `last_delay_ms`.
Research: artifacts/grenade-throw-re/.

## Verification

Sim `aim` (v691): hook installed (scene_rate 8), game crosshair art 3144/3346 draws, HUD screen and captures pass, no eye-art
failures. Not tested: the cure message in the headset, the newspaper/bottle grip, a grenade throw (the test save has no
grenades).

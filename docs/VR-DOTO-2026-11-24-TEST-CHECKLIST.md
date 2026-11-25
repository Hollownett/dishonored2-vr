# Headset checklist: Billie's toolkit, dual Voltaic Guns, grip cancel (#6), mines, D2 Possession (2026-11-24)

Branch `feat/doto-vr`. Everything below was built and checked on Linux (host checks + simulated sessions of the real games under
Proton, `tools/linux/scenarios/*`); none of it has been in a headset yet. Each line: what to do, what you should see, the knob that
turns the change off if it feels wrong (`%LOCALAPPDATA%\DishonoredVR-DOTO\build\<knob>`, or the launcher's feature list).

## Both games

| # | Do | Expect | Off switch |
|---|---|---|---|
| 1 | Aim a power with the trigger (D2: Blink, Far Reach, Domino...; DOTO: Displace), then squeeze the grip of that hand | The aim ends without casting; a short tick in that hand. Let go of the trigger to aim again. A plain press-release still casts. | `no-power-grip-cancel` |

## Death of the Outsider

| # | Do | Expect | Off switch |
|---|---|---|---|
| 2 | Displace: hold the trigger and point the hand (not the head) | The marker follows the hand; release places it; a quick tap swaps you to it with a short dark fade | `no-teleport-fade` (fade only) |
| 3 | Foresight: trigger to leave the body, walk, trigger to return | Spirit moves with your locomotion; a short fade when you snap back | `no-teleport-fade` |
| 4 | Semblance in the power hand: come up behind an unaware NPC (the choke prompt shows) and pull the power trigger (do not squeeze the grip) | Billie takes his face (the game's own face-taking choke), a purple swirl on the hand. As in the flat game, the face is taken INSTEAD of a choke: a grip choke that is already running cannot be turned into Semblance. Also: choke someone out with the grip, then point the power hand at the body and pull its trigger | — |
| 4b | With Semblance in the power hand, try to talk / pickpocket / loot with that hand's trigger | Those go to the OTHER hand's trigger while Semblance is held (that hand's trigger is the power's, as a gun hand's is its gun's) | — |
| 5 | Void Strike: sword charge (trigger held still, or the fist-by-the-ear pose), release | The strike commits as a Void Strike (the game needs a 0.5 s hold; the VR charge holds the attack 0.56 s, from the very first strike of a session) | — |
| 6 | Hook Mine in the hand: hold the trigger 0.3 s or longer | The mine switches lethal / non-lethal: a tick when it switches, a double tick when you let go, nothing is thrown. The mine in your hand does not change colour; check the mode where the game shows it (HUD / a placed mine). | `no-mine-mode-hold` |
| 7 | Hook Mine: a short trigger press | It is thrown/placed as before | — |
| 8 | Hook Mine: throw it with the hand (open the grip while swinging) | It flies along the throw and sticks where it lands (engine placement); a slow open still just drops it | `no-mine-throw-stick` |
| 9 | Voltaic Gun equipped, look at the left forearm, then fire it (grip + trigger) | The gun is on the left forearm (it was not drawn in the default state before) and stays there while it fires (it used to jump up near the ceiling for a moment) | `no-voltaic-native-place` (the firing fix only) |
| 10 | Both forearms with a gun (wheel each hand to the gun); pick a different bolt with the right hand's wheel | Each forearm fires its own bolt; the left keeps its own | — |
| 11 | Sword in the right hand with a right forearm gun | No ring/crosshair for the right gun while the sword is held; block with the sword works | `no-sword-hand-reticle` (crosshair rule only) |
| 11b | Charged shot (the Black Market's Charged Shot upgrade): hold grip + trigger on either forearm gun | At 2 s a light tick in that hand: release for the charged bolt (brighter, with a trail). Held to 4 s it fires by itself, overcharged. The left gun also shows its red charge lights while held; the right gun has no lights (the tick is its cue) | — |

## Dishonored 2

| # | Do | Expect | Off switch |
|---|---|---|---|
| 12 | Possess a creature or a person; turn your head and push the stick, for the WHOLE possession (not only the first second) | The host moves where you look; the right stick turns the host. 2026-11-25: the first version only acted during the 0.5 s fly-in (player state 26); it now follows the possessed host's camera (mode 7) until you leave. Not yet seen in a simulated possession (scripted aims at a patrolling guard cast without a host): please test this one first | `no-possession-view-move` |

## Known limits (not fixed in this branch)

- Shots from a right-forearm copy with a bolt the save has no charges of are refused (`no_ammo`), as the game does.
- The copy gun on the right forearm has no game component of its own: its shots use its own bolt, but whether its model shows a
  bolt-specific look (if the game has one) is not checked; the lab save had no charges to fire and compare.
- Simulated sessions do not show comfort or feel; please report anything that feels wrong per line number.

# VR v1127 (release, 2026-11-10)

Parrying works. Headset-tested by the user on v1127 (parry confirmed).

## Combat

| change | what you see |
|---|---|
| A parry is a fresh Block press as the enemy swings, but the sword in front holds Block the whole time. A quick push (1.5 m/s or more) of the already-blocking sword hand now lets Block go for 50 ms and presses it again; one parry per push, re-armed once the hand slows | push the sword into the enemy's swing to parry and knock him off balance; holding still is still a plain block |
| An attack press waits until Block has been off for 45 ms, also when a parry push let it go just before | a swing straight out of a parry still lands |

Includes v1126 (tutorial sword draw, no vanishing sword, hold the stick down to dive).

## Known issues

- **Crossbow aim in the tutorial's firing range looks wrong:** the tutorial puts an invisible wall in front of the range. Outside the
  tutorial the crossbow aims normally.
- As v1123: exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).

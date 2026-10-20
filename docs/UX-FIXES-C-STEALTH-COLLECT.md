# Stealth kill evidence: what to collect and how to read it

Goal: find out why sword stealth kills from behind are missed, so Task 10 fixes the right cause.

## 1. What to do in the headset (5 minutes)

1. Start VR and go to a level with unaware guards (standing or patrolling, not alerted).
2. Make a manual save before each guard.
3. For each of 10 attempts: approach from behind, swing the sword fast at his back, neck or head once. Note on paper "kill / hit / nothing" and where you hit (back, head, legs).
4. Do not restart the game between attempts. Quit normally. Quit the game fully between collection sessions: the contact and stealth counters are only reset when the game starts, so a second session in the same process mixes the two.

## 2. Where the files are

- Windows: `%LOCALAPPDATA%\DishonoredVR\artifacts\xr-live-status.json` and the newest `%LOCALAPPDATA%\DishonoredVR\artifacts\runs\*\` folder (`native-combat.csv`, a copy of `xr-live-status.json`, `*.log`).
- Linux (Proton): `~/.local/share/Steam/steamapps/compatdata/403640/pfx/drive_c/users/steamuser/AppData/Local/DishonoredVR/artifacts/xr-live-status.json` and `.../artifacts/runs/*/`. The proxy log is `.../artifacts/linux-proxy.log`.

## 3. Status fields to copy

Confirmed against `src/native_interaction_aim.hpp` (`sword_contact_status_json`, `stealth_counts_`, `stealth_reject_`, `hit_height_`) and `src/dxgi_probe.cpp` (`takedown` block):

| Field | Meaning |
| --- | --- |
| `sword_contact.enqueued` | Blade contacts handed to the game-thread consumer (the "contacts" total). |
| `sword_contact.capture_rejected`, `.consumer_rejected` | Contacts dropped before / by the consumer. |
| `sword_contact.stealth[0]` | held: stealth strike accepted into the hold window |
| `sword_contact.stealth[1]` | native took it: the engine's own Assassination took over (not a miss) |
| `sword_contact.stealth[2]` | killed: stealth kill delivered |
| `sword_contact.stealth[3]` | noticed: fell to an ordinary hit after unaware was lost |
| `sword_contact.stealth[4]` | dropped: dead, choke, or owner lost |
| `sword_contact.stealth_reject[0]` | not behind/side |
| `sword_contact.stealth_reject[1]` | slow (below the stealth-strike speed gate) |
| `sword_contact.stealth_reject[2]` | not unaware (engine says he noticed you) |
| `sword_contact.stealth_reject[3]` | not damageable |
| `sword_contact.stealth_reject[4]` | other (hold slot taken by another victim) |
| `sword_contact.hit_height` | `[leg < 0.9 m, torso 0.9-1.5 m, head > 1.5 m]` above the victim's feet |
| `takedown.swings`, `.swings_with_kill` | physical swings, and swings during a native kill offer |
| `takedown.ga_offered_frames`, `.ga_offered_swings` | frames / swings where the native Assassination was offered |

`native-combat.csv` columns are `tick,what,entity,value,extra`. Look at rows named `contact_hit`, `slash`, `thrust`, `overhead` (the `value` column is the tip speed).

## 4. Run the report

```
python3 tools/stealth_session_report.py <path-to-xr-live-status.json> [native-combat.csv]
```

## 5. Decision table

| Cause | Evidence | Fix | Task |
| --- | --- | --- | --- |
| **S, swing too slow at contact** | `stealth_reject[1]` dominant; `contact_hit` speeds under 1.5 m/s | The gate `SwordHitGate::hit_speed` is a head-relative tip speed and the contact is measured at the end of a stroke; measure the peak speed over the last 120 ms. | Task 10 cause S |
| **B, not behind** | `stealth_reject[0]` | The rear/side cos < +0.2 test fails for guards that turn while walking; use the guard's velocity-forward when moving, and widen to cos < +0.35 for the 200 ms before contact. | Task 10 cause B |
| **U, engine says aware** | `stealth_reject[2]` | `NativeMelee::unaware` is false when he is close behind (proximity notice) before the blade lands; do not require `unaware` at contact time when it was true within the previous 400 ms (latch it on approach). | Task 10 cause U |
| **D, not damageable** | `stealth_reject[3]` | Victim flags (+0xd1/+0xd7) read as invulnerable mid-animation; retry for 150 ms. | Task 10 cause D |
| **W, hold expires before native accepts** | `stealth[3]` high, `ga_offered_frames == 0` or tiny | The native Assassination never starts, so the 120 ms (no offer) window ends in a hit; when the contact is a stealth strike and `ga_offered_frames` is zero, keep the 700 ms window and press the attack once more at 250 ms. | Task 10 cause W |
| **N, no contact** | `enqueued == 0` while `swings > 0` | The blade segment passes beside the neck; the near-pass assist (ships regardless). | Task 8 |
| **K, engine rule rejects** | `stealth[2]` (killed) is zero while `stealth[0]` held and `ga_offered_frames > 0` | The native kill rule rejects the player state (crouched, leaning); probe `native_player_state_active` and `choking` reads. | Task 10 cause K |

The report prints `native_took_over` when the engine's own Assassination killed the guard: that is a success, not a miss. A `head-height contacts only` note means the near-pass assist must cover the head volume.

## 6. What to send back

The report output, the paper tally, and the `runs\` folder zipped.

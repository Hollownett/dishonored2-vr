# v553 combined physical-hand headset test

Use only `artifacts/repair-v553-test-candidate.json` after its complete build and provenance gates pass. v552 is an unlaunched, blocked baseline. A previous disabled DLL is not a reusable process: require a clean Steam game, exact executable/DLL hashes and a fresh verified save backup. Preserve Steam Cloud, audio settings, accepted movement, turning and weapon alignment.

## Preparation

- Verify `full_hand_response_enabled=true`, real tracked arms, the existing full-body configuration and no reduced/simulated hand mode. The launcher owns the temporary hand-query marker and restores its previous bytes on exit.
- Verify live `full_hand_collision.requested` and `enabled`, then distinguish prepared uploads, clear/missing/rejected receipts, ready transactions, actual consumed contact palettes and fallbacks. A flag or query count alone does not prove visible collision.
- Record passive timing and boundary telemetry with an explicit3600-second budget until the actual stop. Capture bounded gameplay mirror segments separately. The current recorder is a low-resolution silent desktop mirror, not the headset compositor.

## Ordered tests

1. **Full hands first:** empty left, then right hand; palm, back and each digit against a wall, door and loose prop. Open/close the fingers and rotate the wrist while maintaining gentle contact. Confirm procedural fingers, visible contact, comfortable blocking and immediate free-air tracking. Repeat with the other hand holding a weapon. Mark the exact hand and Tick for penetration, sticking, missing response or a delayed pose.
2. **Contact limits:** slow press, faster approach, deep initial overlap, moving/rotating props, release/regrip and brief tracking loss. Stop on uncomfortable displacement. Local probes do not establish swept thin-wall protection; target witnesses do not pin a body or detect every in-place mesh change. New native rigid-body hands, solver constraints and corpse-part grips are not enabled by this query lane. Preserve existing grab and door behavior; they are not certified as full-digit physical interaction by this test.
3. **Native Choke:** release both right controls. With the game's exact Choke hint displayed, hold only right grip until the guard is unconscious, not dead. No hint means no choke. Separately test offered body carry and full-fist physical pickup.
4. **Y Talk:** fresh Y on an offered NPC/shop prompt addresses that exact NPC. Grip must not talk; Y must not lean or change movement/turning. Check physical lean, crouch, menus and wheel quick actions.
5. **Grip/weapon ownership:** alternate fresh left/right grips on doors, loot and loose objects. Test full-fist pickup separately. Draw a gun only from hip or an accepted wheel selection; ordinary object grip must not draw it. Fire while the other hand uses an object, holster, regrip and check gun/prop overlap. Repeat fast holster-to-wheel and holster-to-hip sequences, cancel and focus interruption.
6. **Shadow Walk:** activate, turn by head and stick, move, swing for native attack animation and exit. Check restored ordinary controls. Rat tunnels and authored finishers retain native camera ownership. Stop on discomfort.
7. **Performance/regression:** matched gameplay without video/readback, then a separate video window. Inspect worst contiguous stalls and query-budget rejections, not just medians or CPU submission rate. Reload twice and check audio, movement, turning and hand restoration.
8. **Stop:** require every hook/input/body restoration, full-hand quiescence and no unexplained native faults. Manual-equip pending/poisoned queued requests require a fresh game process; hook disablement is not cancellation of the native request. Archive exact loaded identity, telemetry, video and user verdict before another candidate.

This is an experimental engineering candidate. Offline tests are not headset quality, comfort, complete collision, performance or bug-free acceptance. Tiny weapon smoothing remains disabled until its shared attachment/shot/crosshair ownership gate is proved.

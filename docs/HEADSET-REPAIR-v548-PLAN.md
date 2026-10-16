# Native-offer interaction repair

This is a work-in-progress repair plan, not a release or headset acceptance claim. Preserve v547, accepted movement/turning, gun holding/hip placement, and saves. Do not relaunch VR without a reviewed candidate and the user's test approval.

## Confirmed baseline and priorities

The user reports one successful VR checkpoint reload. Marketplace and most behavior are qualitatively good. Physical door closing works, but contact remains difficult and opening sometimes delayed. Highest priorities are native-hint-only nonlethal grip choke, Talk on Y, and the left gun remaining visible while holstered or carrying a prop.

## Implementation and dependencies

1. Stop widening choke distance/angles. The existing private invocation scope expands the sensor that was previously treated as offered eligibility. Its non-mutating implementation does not make its widened eligibility acceptable for this request.
2. Observe the native contextual CHOKE offer, not a generic takedown cache, NPC position, neck geometry, kill/drop sensor, or generic interaction-window visibility. The pinned native HUD producer also checks state eligibility and HUD suppression after reading the choke sensor. A same-victim active native Choke state may continue an already-owned hold, but may never start one.
3. Give a settled right grip one exclusive owner per press. An offered choke consumes Use; a body grab/carry cannot turn into choke mid-press. Release, focus/pose-epoch loss, occupied hands, stale identity, or changed victim cancels safely. Remove the artificial gaze-search prerequisite from the choke path only after native offer evidence is available.
4. Classify the selected native Talk usage exactly. NPC identity or the presence of a Talk declaration is insufficient: carry, loot, busy Talk and other offers share NPCs. A Talk-owned Y hold must not leak to native lean, menus or wheel quick actions; retain non-Talk lean behavior.
5. Capture terminal weapon ownership before a visibility correction. Enqueued holster counters and stale gun-model caches do not prove which mesh renders. Do not hide all weapons, change accepted alignment, or claim instrumentation alone fixes the ghost.

## Validation gates

- Focused native-offer fixtures reject no hint, kill/drop-only hints, stale/changed player or victim, wrong callsite/type, and disabled hooks.
- Grip fixtures cover tap versus hold, body Use ownership, focus/epoch cancellation, same-victim active continuation, release and no trigger leakage.
- Talk fixtures distinguish actual Talk from busy Talk, carry, loot, door and unknown usages; held Y cannot become lean or late-Talk after cancellation.
- Build all required checks against unchanged archived source inputs before producing a new numbered DLL.
- In headset: Y talks and opens shops; hold grip chokes only with the game-offered hint, without killing or stealing body carry; holster and bottle/glass produce no visible gun duplicate. Recheck normal movement/turning, wheel, lean and reload. Offline fixtures cannot pass these gates.

## Primary public reference

The publisher's [Dishonored 2 Xbox controller manual](https://assets.ctfassets.net/rporu91m20dc/4wyjsRJV32cmKEcymOquUI/43ec13c9e82351efbc591dd33832f262/manuals_dishonored2_xone_en_pl.pdf) identifies RB as block/nonlethal takedown held. Native pinned-executable receipts, not this control description, must establish current action eligibility and ownership.

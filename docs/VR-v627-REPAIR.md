# v627 world interaction prompt candidate

Headset feedback accepted v626 sprint synchronization and pre-grip coin, book and bottle prompts. This candidate preserves those movement and pickup paths. It addresses the remaining world-control and corpse presentation regressions; fresh headset acceptance is still required.

## World controls

Fresh v626 door approaches pass native permission checks before grip, but the hand LOS wrapper rejects every noncharacter owner outside its model-bounds sphere, including Openable components. The first native Add then occurs after the grip-triggered gaze path. RTTI from the pinned installed executable identifies Openable (1c569c8), NPCInteraction (1c561f8), UsableWithStateList (1c5bfc8) and Pickable_Simple (1c5a3b8).

Inside-sphere noncharacter behavior remains unchanged, including generic readables. Verified takeables still use the strict pickup sphere outside it. Other world usables outside that sphere, or without usable model bounds, preserve the original native LOS result and output byte through the full callback. The existing native permission, selection, ordering and ownership checks remain authoritative. No Use event is generated for preview, and no native interaction text is fabricated.

## Corpse presentation

The v626 corpse owner 0x8f5d61f0 received native Add and Gauge updates with open grips approximately 3.4 seconds before its physical grab. This demonstrates available pre-action native artwork for that encounter. The retained-entry placement branch used a uniform 5 cm offset from the entity origin; the older NPC branch used body-relative presentation heights.

Retained NPC entries now use that exact owner's finite entity origin and restore the existing downed-state policy (+d1 mask 0x28): 35 cm above a downed body, 1.95 m above a standing NPC. Static object bounds are not used as an NPC origin. The entry, spawn and downed state are revalidated. Dead bit 0x08 is verified; bit 0x20 follows the existing downed-state policy. Native +Z maps to XR +Y. Limb dragging, torso hold-to-carry timing and native gauge content are unchanged. A bounded successful-submission trace records the entry owner/key, actual XR anchor, texture dimensions and grip state.

## Verification and test conditions

Focused tests cover native LOS success/failure and exact out-byte preservation for outside or missing-bounds world controls, existing inside-sphere readable behavior, takeable range rejection, retained NPC offsets, unreadable/changing NPC state and stale entry ownership. Candidate compilation uses clang-cl/xwin /W4 /WX. The complete native Windows MSVC gate is unavailable on this host.

The candidate uses installed stable Proton 11.0 and preserves the user's saved AA setting (last observed DLAA), alternate-eye configuration and requested gameplay features. Power gestures remain disabled; trigger buttons activate powers. Accepted sprint and hand collision source files are checked unchanged against the v626 build snapshot.

Doors, typewriters, safe dials and corpse prompts still require a fresh headset check before claiming the repair visually accepted. Prior centre/right-eye and alternate-eye continuity reports remain unresolved by this focused change.

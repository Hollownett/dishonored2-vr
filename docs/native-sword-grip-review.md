# Native occupied Sword grip review

The parent obtained the Sword through normal story gameplay and ran v85 with
arm observer only. `20261009T013449.955942Z-pid44016-arm-v85` reports tracked arms
false, native pose/copy calls bothzero, no animation writes or callback errors.
The independent three-second read-only capture contains10 pose reports and no
reader errors. Eight reports have unchanged native source/output and palette
agreement within1.46e-7; reports0024/0028 race-invalidated the right palette and
are excluded. No raw “zero errors” count certifies those two invalid poses.

Selected source0036 is exact owned right `Sword`, index2, non-null equipment slot;
left is `Empty`, index0, null slot. It matches the Empty baseline's exact69
names/hierarchy/inverse binds. Private mesh replay agrees with native output to
8.02e-8 right and9.28e-8 left. This is an occupied Sword source, not an Empty fist.

All15 right main bones change. Fourteen exceed the existing flexion threshold
trace2.98 (roughly8.1degrees); thumb02 changes7.3degrees. Right MCP changes are
index58.05/medium70.91/ring84.49/pinky73.48degrees, PIP59.43–78.31 and
DIP27.60–33.37. Thumb01 changes19.92, thumb03 changes51.57. Main offsets remain
unchanged to read precision. Left changes below0.12degrees and supplies no
closed left reference.

The right mesh visibly forms a compact shaft grasp with opposed thumb and a
weapon-shaped cavity. It is a much better native starting point than arbitrary
angle closure, but does not establish a fully closed Empty fist. The captured
locals and helpers may serve as a deliberately derived reference only with
occupied provenance retained and new acceptance checks.

Real helper offsets reach0.021428 native units at ring0bottom. Relationships
vary by knuckle: MCP top helpers roughly0.7 of MCP rotation, MCP bottom roughly
0.43–0.46; PIP top varies roughly0.5–0.7 while bottom is around0.1; DIP top
roughly0.6 and bottom nearlyneutral. Thumb02/03, palm skin and dorsal muscles
have distinct measured transforms. Reusing the complete captured local helper
pose avoids treating a generic half-angle rule as native behavior.

`tools/analyse_native_occupied_hand.py` reconstructs the captured finger/palm
helper locals on the Empty baseline while leaving non-hand locals unchanged.
This is explicitly an offline occupied-source transfer, not a capture of Empty.
Right flesh contact diagnostics include108 index/thumb triangle pairs
(maximum plane-overlap proxy0.004429 native units),31medium/ring (0.004397),
13pinky/ring (0.003092) and self contacts. These are native grasp/contact
features in the transferred pose, not a no-clipping certification. The checker
does not establish physical penetration volume or final textured appearance.

The transferred right alternative `emily_body.material` surface moves0.374505
native units but preserves every edge length to6.16e-9 (maximum edge ratio
1.00000035). Native thumb/end locals remain neutral relative to their parents;
the shared weighted patch follows a rigid subtree rather than tearing.
Preserving the end global separately would break this relationship. The
surface's meaning and pixel visibility in occupied Sword gameplay still require
current native draw/appearance evidence. Left ring remains unchanged because
the captured left hand remains open; no left ring closure solution follows.

Private comparison images and source statistics are in
`artifacts/native-sword-idle-source-v85.json/comparison.json` and the adjacent
left/right-idle-action.png. Detailed transferred locals, helper/contact/strain
measurements and comparison against the earlier authored bank are in
`native-arm-assets/native-sword-grip-v85-review/occupied-native-sword-analysis.json`
and left/right-native-sword-transfer-review.png.

Old PID23072 mesh/fullbody reports were also checked for a left native closed
source: they match native output but have zero left flexed bones against this
Empty baseline, so they cannot substitute for a captured Gun grip. The smallest
next left-source route is a legitimate left-hand Gun or other grasp acquisition
and observer-only idle capture. Keep occupied source provenance; do not mirror
right helpers or re-label Sword/Gun poses as captured Empty fists. Current
production hand references and captured exporter acceptance remain unchanged.

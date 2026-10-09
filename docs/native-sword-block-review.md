# Native Sword block source review (v89)

Source: parent-operated observer-only run
`artifacts/runs/20261009T015756.125857Z-pid44016-arm-v89` and
`artifacts/native-sword-block-source-v89/capture.json` (PID44016,
60 samples,10 pose files,0 capture errors). Requested action is native Sword
RB block. No tracked-arm activation or game control was performed by this
analysis. Exact executable SHA256 is
`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.

The left graph is Empty with null equipment slot; the right graph is Sword
with a non-null slot. Those facts do not identify the animation action.
All10 left sources have0/15 flexed principal finger joints versus verified
Empty v72 pose0044. Pose0036 differs by at most approximately0.05 degrees;
helper offsets change at most4.39e-8 native units. Its private wrist-aligned
mesh remains open. The ring surface changes at most5.07e-8 native units after
transferring the complete finger/helper subtree onto the open pose. There
is no useful closed left endpoint in this folder.

Right sources retain14/15 flexed principal finger joints. Most right local
rotations match v85 Sword idle, but pinky MCP differs20.64 degrees and pinky
DIP0.43 degrees. Principal local segment offsets remain effectively unchanged.
Native helper offsets reach0.0214287 units; all five end-joint local offsets
remain within4.05e-8 units of the Empty baseline. Preserving their native
parent-local relationship keeps the right alternative-body surface rigid:
maximum edge absolute change6.10e-9 units despite0.374655-unit displacement.
This is evidence against preserving the accessory leaf's open global position.
It does not certify material visibility or a natural empty fist.

Nine paired reports have unchanged source/output agreement, errors at most
1.01e-7. Pose0052 right source is invalid (error null, source_unchanged false)
and is excluded from compatible source evidence despite capture errors0.
The private source-mesh replay for pose0036 agrees with the uploaded palettes
within1.12e-7 units. Native-contact diagnostics still report right thumb/index
and other intersections; these are not polished fist acceptance.

Both arms are pose_state3 / renderer_upload_source. The report explicitly marks
snapshot_frame_synchronised=false, final_blended_pose_verified=false,
tracked_arm_writes_absent_verified=false, authored_pose_confirmed=false.
The run configuration reports observer-only, but the source report itself
cannot establish which displayed animation frame was sampled. In particular,
the parent's visually observed two-hand block and these open left sources
must not be equated. A synchronized final-palette/action trace is needed to
resolve that mismatch before further closed-reference capture claims.

Private outputs:
- capture folder comparison.json and left/right-idle-action.png;
- native-arm-assets/native-sword-block-v89-review/occupied-native-sword-analysis.json;
- full-local-relationships.json: all69 parent-local open/block matrices per
  side, exact source hashes, names, parents, principal/helper/end relationships;
- left/right-native-sword-transfer-review.png (research transfer only).

No captured-reference export, bank installation, production activation, Empty
fist certification, or headset acceptance resulted from this review.

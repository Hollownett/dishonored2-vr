# Private authored native-rig grip trial

The v72 native empty idle/sprint and v76 native block trials supplied open hands,
not fists. The authored route therefore operates on the exact installed Emily
69-joint rig and private mesh. It does not label artist/solver matrices as
captured native animation. No production reference archive was exported and no
runtime pose activation changed.

## Reproducible candidate

The current private review candidate is
`artifacts/research/native-arm-assets/authored-grip-spread-trial/`:

- `left-authored-candidate.png` and `right-authored-candidate.png`: native idle,
  authored closed grip, and independent index release from three views.
- `authored-candidate.json`: exact matrices, source SHA, main-axis geometry,
  thumb artist parameters, helper drivers, per-material displacement/strain.
- `hand-contact-diagnostic.json`: exact introduced triangle crossings versus
  the source, region pairs and absolute triangle-plane overlap proxies.

The source is v72 `pose-0044.json`, which matches the native renderer/inverse
bind at approximately 1e-7 native units. Additional flex axes come from each
native bone segment and signed palm curvature. Main local offsets/lengths
remain unchanged. The artist distributes flexion across MCP/PIP/DIP rather
than applying one generic angle to every bone. Thumb opposition is bounded and
searched against actual flesh/glove triangles in both index states. Thumb
parameters are held constant as the index releases; other digits and helpers
remain independent of the index input.

Native partial-bend captures showed substantial helper drivers: non-thumb top
MCP helpers rotate approximately 70% of the main bend, bottom helpers around
50%; thumb top helpers have nearly neutral local rotations while lower/palm
helpers respond differently. The trial uses these bounded relationships and
native geometry pivots, retaining neutral end-joint locals. Ring/pinky MCP
abduction of four/eight degrees separates neighboring fingers. Helpers move
less than 0.025 native units; this is a deformation allowance, not a measured
human displacement.

```text
python tools/author_native_hand_candidate.py --source artifacts/research/native-arm-assets/hand-capture-unmodified-v72-14304/pose-0044.json --output artifacts/research/native-arm-assets/authored-grip-spread-trial --driven-helpers --relaxed-knuckles --thumb-clearance .025 --thumb-parameters artifacts/research/native-arm-assets/authored-thumb-contact-search.json --fan-fingers
python tools/check_authored_hand_contacts.py --candidate artifacts/research/native-arm-assets/authored-grip-spread-trial/authored-candidate.json
python tests/native_authored_pose_check.py --source artifacts/research/native-arm-assets/hand-capture-unmodified-v72-14304/pose-0044.json --candidate artifacts/research/native-arm-assets/authored-grip-spread-trial/authored-candidate.json
```

The parameter search used 326 evaluations per side and a twelve-iteration cap.
It improved contact quality but did not converge within that cap. Search
termination is not a pose acceptance result.

## Quality findings and limits

The chosen thumb has no introduced thumb/index triangle crossings in either
control state. The right thumb has zero introduced crossings with any region;
the left has nine thumb self-fold pairs with overlap proxy at most 0.00119
native units. Main lengths, rigid rotations, bounded helpers, independent index,
and unchanged zero-hand-weight vertices pass private fixture checks.

The full hand still has introduced fold/contact pairs: 142 left and 51 right
in the full grip. Most are same-finger joint folds. The left has no remaining
cross-digit pairs; the right has four medium/ring pairs. Joint-fold overlap
proxies reach about 0.0047 native units; the remaining right medium/ring proxy
is about 0.005. These are real diagnostics, so the candidate is not certified
as clipping-free. The checker excludes shared native seams, distinguishes
existing crossings from introduced ones, and does not evaluate coplanar
contacts or certify an enclosed physical volume.

The largest original left edge ratio belonged to
`player_emily_ring_disappear.material`, not flesh: an 0.000085-native-unit edge
became 0.00276 in the first trial. Ring geometry mixes thumb-helper and index-end
weights and needs independent draw/visibility protection. The right
`emily_body.material` alternative surface follows thumb/end joints and can move
roughly 0.58 native units. Its gameplay visibility has not been established.
Neither surface may be silently counted as safe hand flesh. Sleeve vertices
with zero hand influence remain unchanged to floating-point precision.

All measurements are native model units. The default one-unit-per-metre
conversion remains independently unverified. No headset comfort, physical palm
offset, final texture rendering, or user acceptance claim follows from these
offline plots.

## Proposed integration boundary

After mesh/material review, an authored reference should use a separate
`D2VRAH01` archive/loader with explicit authored origin, pinned executable/source
SHA, exact side/hierarchy/chains/helpers/inverse identity, and candidate/mesh
review provenance. Keep the captured `D2VRHP01` exporter unchanged. Reuse the
same rigid/length/ownership validation; provenance must never imply a captured
fist.

Production remains opt-in until reviewed. It requires a recent sustained
Empty/null occupancy publication, callback-time `native_empty_side` revocation,
same epoch/owned rig and exact draw-surface safety. Empty equip state alone does
not distinguish idle from mantle, takedown, scripted action or an active native
grasp. Preserve these authored native actions and occupied weapon hands. The
closed endpoint must also be checked through the actual parent-local runtime
interpolation at partial grip amounts, with both trigger states, before use.

## Actual runtime sweep and visible draw evidence

The standalone `authored_hand_sweep.cpp` uses the production `curl_fingers`
parent-local screw blend. Its D2VRTS01/D2VRSO02 fixtures are incompatible with
installable references. Seven grip amounts and both index states are checked
for each side, with and without accessory-only leaf preservation, and with
the thumb amount `max(grip, 0.5 * index)`: 112 output poses, 56 distinct skin
contact checks. This thumb envelope eliminates thumb/index crossings in the
original spread candidate across the sampled range. Left thumb self contacts
remain, with maximum plane-overlap proxy 0.001292 native units. Right thumb
contacts are absent. Other digit contacts remain: right medium/ring up to
0.005096; released-index/full-grip index/medium contacts on both sides.
No clipping-free acceptance follows from the sweep.

Keeping the left index-end/right thumb-end leaf global unchanged is bit-exact
on flesh, glove and cuff vertices throughout matched sweep controls. It only
partly reduces accessory deformation; shared accessory/flesh helper influences
prevent claiming a full accessory fix. A broad spread trial that changed index
splay introduced thumb/index contacts and is rejected.

The v78 GPU trace contains count-only candidates for right flesh (12345, three
deduplicated draws), sleeve (8700, two) and alternative body (531, two). No left
surface fingerprint matches appear in its bounded sample window. These counts
are neither model identity nor visibility proof. In particular, the alternative
body cannot be assumed hidden. Current trace data omit IA buffer identities,
offsets, stride, index format, startIndex/baseVertex and material identity.

Parent-owned collector extension: retain indexed hook start/base and instance
arguments; query bounded IA vertex/index buffer identities and descriptors,
primitive topology and exact ranges alongside context/phase/draw identity.
Then correlate to independently verified native surface upload ownership or
index/vertex readback fingerprints. Counts alone, guessed native GPU wrapper
offsets, or unverified material flags must not permit a pose bank. An unknown
visible accessory primitive keeps the authored bank disabled.

The corrected right-only spread trial retains the original thumb clearance
0.025 and thumb parameters, with MCP splay medium +2, ring -8, pinky -16
degrees; left remains at ring -4/pinky -8. Runtime full-grip medium/ring
crossings reduce from four to one, overlap proxy 0.005096 to 0.003819. Thumb/index
crossings remain absent across all sampled control states. Low-grip pinky/ring
and medium/ring contacts and released-index index/medium contacts remain. See
`authored-grip-right-spread-trial/actual-runtime-sweep.json` and its actual-screw
review images. Its rigid matrices, main lengths, bounded helper translations,
independent index and unaffected zero-hand-weight vertices pass isolated tests.

An earlier broad spread trial inadvertently used the helper's default thumb
clearance 0.012 instead of the baseline 0.025. Its contact changes are confounded
and must not be used to infer a causal index-splay result. The right-only trial
was corrected and rerun before reporting the measurements above.

The standalone candidate collector is now implemented in the parent probe:
`dxgi-native-hand-draw-candidates.csv`, count fingerprints only, at most 64
unique draw/buffer/range/shader tuples per phase. It queries all 32 IA slots,
index buffer/format/offset, buffer ByteWidth, topology and VS/PS COM identities;
no staging or readback occurs. Syntax-only MSVC /W4 /WX validation passed.
Native surface correlation and live evidence are still outstanding.

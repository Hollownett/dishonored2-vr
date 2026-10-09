# Native hand GPU ownership evidence

Read-only pinned executable evidence now traces owned arm descriptors to actual
stored ID3D11Buffer pointers. No native function calls, GPU staging/readback,
pose capture or input were performed by this lane.

Verified static paths are under `artifacts/re-toolchain/direct-ghidra/`:
`hands-gpu-buffer-ownership`, `hands-gpu-buffer-wrappers`,
`hands-gpu-com-ownership`, `hands-native-decl-base` and
`hands-surface-resource-names`. Every export used `-readOnly -noanalysis` and
the pinned executable SHA256. The shared project was released afterward.

MD6 surface+0x90 is the geometry loaded at 0x259960. Geometry+0xa0 is the
vertex wrapper list; geometry+0x298 is the index wrapper list. Constructor
0x2ae210 creates lists with 0x20-byte entries and 0x38-byte wrappers via
0x2d3420. Upload0x2371e0 uses index count+0x34/CPU indices+0x88; upload0x237460
uses vertex count+0x38/CPU vertices+0x80. Vertex upload splitting uses actual
GPU format slots, not a guessed common interleaved stride.

Wrapper+0 holds ID3D11Buffer: 0x2d3aa0 releases its existing IUnknown through
vtable+0x10 and passes the wrapper address as CreateBuffer output to the native
ID3D11Device vtable+0x18. Wrapper+0xc is ByteWidth, +0x18 usage, +0x1c native
bind flags (vertex1/index2). List entry+0 is wrapper; +0x10 base offset;
+0x18 vertex stride, as populated by 0x2ae400 and consumed by 0x2ae7b0.

Resource base constructor0x05e740 initializes the name pointer at+8. Pathname
consumer0x05e890 reads that exact pointer into its string helper. Material
constructor0x2d4040 derives from that base, exact vtable0x1b17928. Surface
material+0x28 is assigned by the pinned MD6 loader. These names are read
boundedly and rechecked; no getter is called.

`tools/inspect_native_surface_buffers.py` validates pinned PID/image, exact
native owned player/body/arms, model source/skeleton/descriptor, geometry type,
surface lists, wrapper fields and before/after resource/list identity. It reads
the names of actual left/right player arm resources and all material surfaces.
`tools/compare_native_surface_draws.py` compares the v82 D3D getter CSV against
every owned index pointer and all five vertex slots, strides, offsets, widths,
index format, topology, instance controls and exact zero-offset full draw range.
Same counts with different pointers/ranges are rejected.

Private evidence is in `native-surface-buffers-v82-44016.json` and
`native-surface-draw-agreement-v82.json`. Eight submitted draw identities match:
left sleeve2/flesh1/alternative body1, right sleeve1/flesh2/alternative body1.
Both alternatives have actual native material
`models/characters/female/emily/textures/emily_body.material`; source resources
name the exact Emily left/right player arm meshes. Left ring and glove have
no match in the bounded v82 window. Absence does not establish invisibility.

The native snapshot was taken after the trace, so equality is not atomic with
the render frame. Actual draw submission does not prove surviving pixels or
occlusion. CPU indices have been released after upload; no GPU index-byte
readback fingerprint is claimed. This is nevertheless verified native surface
ownership and exact IA pointer/range agreement, rather than count coincidence.
Whole-palette authored grip remains unsafe until accessory geometry is handled.

`tests/native_surface_buffers_check.py` passes synthetic wrapper/list/name race,
format/range/slot and same-count pointer collision cases. No production bank
activation follows from these tools.

## Accessory-only rigid leaf trial

`tools/fit_authored_accessory_leaf.py` tests the parent hypothesis offline on
28 actual production screw-interpolated poses. Weighted Kabsch fits a rigid
6DOF transform to the leaf's inverse-bind vertex positions, accounting for
other bones' already posed weighted contributions. Weights squared minimize
the resulting skinned vertex error. Only the accessory-only leaf changes;
flesh, glove and cuff skin are bit-exact throughout. No mesh or production
palette is modified.

Left target is the native ring rigidly attached to current index02. Its maximum
residual is 0.033711 native units, RMS 0.008281; active3356 vertices retain up to
0.030287 error and the197 zero-leaf-weight vertices impose up to0.033711 error
that no leaf transform can correct. Right target retains native alternative
body positions. Four leaf-influenced vertices retain maximum0.003838 error;
whole127-vertex surface RMS0.000496. Its fitted leaf global translation changes
by up to0.628261 native units; this is not an anatomical endpoint or accepted
runtime deformation. These results fail the proposed1–2mm-equivalent bound,
with model-unit conversion separately unverified.

Evidence: `authored-grip-right-spread-trial/accessory-rigid-leaf-fit.json` and
left/right `accessory-rigid-leaf-fit.png`. `tests/accessory_leaf_fit_check.py`
passes known rigid recovery, incompatible targets and invalid influence checks.
The right fit offers a useful research bound, while the left requires another
render/geometry policy because shared flesh helpers cannot safely be frozen.

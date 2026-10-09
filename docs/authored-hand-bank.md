# Private authored hand resource

`src/authored_hand_bank.hpp` and `tools/export_authored_hand_bank.py` implement
an inactive research resource for the exact installed Emily 69-joint arm rig.
This is deliberately authored geometry, not a captured native fist. No loader,
configuration or bank activation was added to the production probe. The
captured D2VRHP01 workflow remains unchanged.

## D2VRAH01 layout

All scalars are little endian, matching the existing Windows native-palette
format. Header: eight-byte magic, uint32 flags exactly 3 (authored origin and
acknowledged limits; acceptance bit is absent), six 64-byte lowercase SHA256
strings in order executable/source report/candidate/left mesh/right mesh/mesh
review, then uint64 FNV1a payload checksum. Header size is 404 bytes. FNV is
accidental corruption detection, not a cryptographic authenticity mechanism.

Two fixed side records follow, left then right. Each contains uint32 side,
69 zero-padded 96-byte exact joint names, 69 int16 parents, 15 uint32 named chain
indices, two uint32 palm helpers, uint32 accessory-only leaf, 69 role bytes
(bit0 finger subtree, bit1 main bone, bit2 preserved accessory leaf), 69×12
inverse-bind floats, and open/closed 69×16 column-major transform floats.
No variable counts, dynamic strings, arbitrary paths or compressed blobs are
parsed. Trailing data are rejected. The file is private ignored source geometry.

The exporter pins the read-only source hash, explicit authored origin and
actual 112-state C++ screw-interpolation review. Review candidate/output hashes
must match; the exporter independently confirms the left index-end/right
thumb-end leaf has no flesh, glove or cuff skin influence. The accompanying
manifest includes the archive SHA256, payload checksum, all six provenance
hashes, mandatory gates, remaining limits and `metadata_acceptance=false`.

## Runtime factory contract

Before reading, independently verify the archive SHA256 against the reviewed
private manifest. Construct `AuthoredBankProvenance` from that verified manifest,
including `payload_checksum`; do not copy expectations from the untrusted
archive. Construct both `AuthoredRigIdentity` entries from the current verified
native rig: exact names, parents, chains, helpers, inverse binds and reviewed
accessory leaf. Call `read_authored_hand_bank(stream, provenance, rigs)`.

The reader rejects provenance, side, name, hierarchy, inverse, role, rigid-pose,
flexion, segment and helper mismatches, corruption, truncation and trailing data.
Main/end local offsets may differ by at most 0.0001 native units; helper offsets
are limited to 0.025. The common captured reference validation remains intact.

Call `authored_hand_pose(resource, side, current_palette, current_rig, grip,
index, eligibility)`. A fresh identity mismatch or invalid controls throws.
Eligibility requires explicit opt-in, exact visible primitive validation,
verified native idle action and the current side's fresh owned Empty/null bit.
Any absent gate returns the current native palette unchanged. Eligible poses
use thumb `max(grip,0.5*index)`, independent index, other fingers at grip, and
retain the accessory-only leaf's current global transform. Empty equip state
alone does not establish idle action or safe material visibility.

The eligibility booleans are a caller contract, not newly discovered native
fields. Current exact-visible-primitive correlation and idle-action proof remain
outstanding. Do not turn them on from candidate count matches or user opt-in
alone. Production and headset acceptance remain false.

## Evidence and tests

The private bank is at
`artifacts/research/native-arm-assets/authored-grip-right-spread-trial/authored-hand-bank.bin`
with its adjacent manifest. The corrected right spread reduces endpoint
medium/ring crossings four to one, overlap proxy 0.005096 to 0.003819 native
units. Thumb/index crossings are absent in the sampled sweep. Low-grip
pinky/ring and medium/ring contacts, released-index contacts and self contacts
remain. Accessory-only leaf preservation does not fully fix ring/alternative
body deformation. Unit conversion, material visibility and headset acceptance
are unverified. No clipping-free certification is claimed.

`tests/authored_hand_bank_check.cpp` exercises the actual private bank, bad
headers/hashes/sides/names/hierarchy/roles/geometry/inverse/truncation/checksum,
runtime gate revocation, thumb envelope, all-zero preservation and accessory
leaf preservation through 28 control states. `tests/authored_hand_export_check.py`
checks authored/captured separation and source/stale/incomplete review refusal.
MSVC /W4 /WX and both checks passed; the existing captured exporter check passed.

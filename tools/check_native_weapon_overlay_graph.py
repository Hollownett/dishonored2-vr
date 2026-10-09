"""Pinned active AnimEvolved graph assertions; design evidence, no game access."""
import hashlib
import json
from pathlib import Path
import struct
from probe_game import PE

ROOT = Path(__file__).resolve().parents[1]
data = (ROOT/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
pe = PE(data)

def code(rva, size):
    offset = pe.offset(rva)
    return data[offset:offset+size]

def call(rva, target):
    raw = code(rva, 5)
    assert raw[0] == 0xe8 and rva+5+struct.unpack('<i', raw[1:])[0] == target

table = 0x1d05298
assert any(entry['vtable_rva'] == table for entry in pe.rtti_vtables('arkCpntAnimEvolvedFactory'))
callbacks = {4: 0x951030, 5: 0x952670, 6: 0x951570, 7: 0x952ee0}
for phase, target in callbacks.items():
    assert struct.unpack('<Q', code(table+0x10+phase*8, 8))[0] == pe.image_base+target
for source, target in [(0x95106f,0x54f500),(0x9526af,0x550970),(0x9515af,0x550830),
                       (0x54f598,0x540a90),(0x5509d7,0x540a90),(0x5509f1,0x540a90),
                       (0x5508f3,0x540a90),(0x5508ff,0x7521f0),(0x952f1f,0x550a00),
                       (0x54130b,0x25efe0),(0x942a63,0x543b00),(0x8af5b5,0x7ec5b0),
                       (0x8af5cb,0x8af6a0),(0x8af64c,0x7ec5b0),
                       (0x753d04,0x7506d0),(0x75422f,0x53d6a0),(0x53d700,0x9b900)]:
    call(source, target)
raw = code(0x550a00,5)
assert raw[0] == 0xe9 and 0x550a05+struct.unpack('<i',raw[1:])[0] == 0x541270
assert code(0x5508eb,5).hex() == 'ba03000000'
assert code(0x540ad3,10).hex() == '81a1e0020000000000ff' # Stage0 resets the packed transform count.
assert code(0x8af5a7,7).hex() == '4533c0418d5006'
assert code(0x8af640,5).hex() == 'ba07000000'
result = dict(sha256=sha, active_factory_rva=table, phase_callbacks=callbacks,
              inactive_541110_used_as_evidence=False, live_phase_dispatch_verified=False,
              shared_pose_overlay_safe=False, engine_calls=False, game_memory_writes=False,
              limits=['Indirect/event readers and save/load paths are not excluded',
                      'Append buffer lifetime and native no-op stage3 require owned restoration before invalidation',
                      'Scheduler graph order does not prove the whole native model/XR generation is stable'])
print(json.dumps(result,indent=2))

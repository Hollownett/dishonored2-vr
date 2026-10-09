"""Exact installed PE prolog proof; never opens the live process or calls native code."""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
probe = json.loads((ROOT / 'artifacts/game-probe-rig.json').read_text(encoding='utf-8-sig'))
exe = Path(probe['executable']['path'])
image = exe.read_bytes()
assert hashlib.sha256(image).hexdigest() == 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
pe = struct.unpack_from('<I', image, 0x3c)[0]
assert image[pe:pe + 4] == b'PE\0\0'
sections, optional = struct.unpack_from('<H', image, pe + 6)[0], struct.unpack_from('<H', image, pe + 20)[0]
start = pe + 24 + optional

def at_rva(rva, size):
    for n in range(sections):
        vsize, va, raw_size, raw = struct.unpack_from('<IIII', image, start + n * 40 + 8)
        if va <= rva and rva + size <= va + min(vsize, raw_size):
            return image[raw + rva - va:raw + rva - va + size]
    raise AssertionError('RVA outside initialized PE sections')

assert at_rva(0x8a4830, 32).hex() == '48894c240855535657415441564157488bec4883ec4048c745f0feffffff488b'
assert struct.unpack('<Q', at_rva(0x1c940a8, 8))[0] == 0x1408a5f60
assert at_rva(0x96240, 32).hex() == '488bc455488da8a8feffff4881ec5002000048c7442440feffffff4889581848'
assert at_rva(0x8ab0f0, 32).hex() == '40534883ec3048c7442420feffffff488bd941b001488bd1488d4c2440e8ae7c'
assert struct.unpack('<Q', at_rva(0x1c80090 + 0x10, 8))[0] == 0x1408533e0
assert struct.unpack('<Q', at_rva(0x1c940a8 + 0x30, 8))[0] == 0x1408ab0f0
print('PASS: pinned PE join/shutdown signatures and GameLocal/PhysicsWorldLocal virtual shutdown targets')

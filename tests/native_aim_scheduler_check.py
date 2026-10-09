"""Verify the pinned producer ABI and component scheduler phase mapping offline."""
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_game import PE

root=Path(__file__).resolve().parents[1]
data=(root/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
assert hashlib.sha256(data).hexdigest()=='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
pe=PE(data)
def chunk(rva,size):
    at=pe.offset(rva)
    return data[at:at+size]
def pointer(rva):
    return struct.unpack('<Q',chunk(rva,8))[0]-pe.image_base
def call_target(rva):
    code=chunk(rva,5)
    assert code[0]==0xe8
    return rva+5+struct.unpack('<i',code[1:])[0]

assert chunk(0x7f4b30,32).hex()=='4c8bdc574881ec80000000488bf984d20f845e010000440fb7819a510200488b'
assert chunk(0x7f6a80,32).hex()=='48897c242055488d6c24d04881ec30010000488bf9488b4910488d55400f29bc'
assert call_target(0x7f7275)==0x7f4b30
assert call_target(0x9533ee)==0x7f6a80
for phase in range(8):
    thunk=pointer(0x1c71058+phase*16)
    assert chunk(thunk,6)==bytes([0x48,0x8b,0x01,0xff,0x60,0x10+phase*8])

stores=[
    (0x1d04698,2,0x94f9a0), # Early camera animation update.
    (0x1d04698,7,0x9533c0), # Finalized view/aim producer.
    (0x1d0b658,1,0x954d00), # Equipped aim consumer.
    (0x1d0bbf8,1,0x9548f0), # Queued interaction sensor.
    (0x1d0bbf8,2,0x94ffe0), # Primary interaction sensor.
    (0x1d0b418,1,0x954940), # Queued character sensor.
]
for store,phase,callback in stores:
    assert pointer(store)==0x940c40
    assert pointer(store+0x10+phase*8)==callback
label=b'TtarkSubmitAndWaitGraph\0'
assert label==chunk(0x1ad0aa0,len(label))
print('PASS: pinned producer bytes/callers, eight phase thunks, camera/action/sensor phases, SubmitAndWaitGraph profiler identity')
print('LIMIT: offline check does not establish indirect/event-reader scheduling or live weapon/power effects')

"""Pinned offline native counter, producer and scheduling ABI checks; no game access."""
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_game import PE
root=Path(__file__).resolve().parents[1]
data=(root/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
assert hashlib.sha256(data).hexdigest()=='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
p=PE(data)
def code(rva,n):
    at=p.offset(rva);return data[at:at+n]
def pointer(rva):return struct.unpack('<Q',code(rva,8))[0]-p.image_base
def target(rva):
    b=code(rva,5);assert b[0]==0xe8;return rva+5+struct.unpack('<i',b[1:])[0]
assert pointer(0x1c940a8+0xb8)==0x8a8a20
assert pointer(0x1c940a8+0xc0)==0x8a89a0
assert code(0x8a8a20,8).hex()=='488d8128742e00c3'
assert code(0x8a89a0,12).hex()=='4881c128742e00e914787c00'
assert code(0x10701c0,3).hex()=='8b01c3'
assert code(0x59980,3).hex()=='ff01c3' and target(0x8af33c)==0x59980
assert code(0x541110,32).hex()=='48895c24184889742420574883ec20488b4110488bf9488bb0580300004885f6'
assert code(0x540a90,32).hex()=='48895c2420555657415441554883ec20488b413885d24c63ea488b80e81f0000'
for caller in (0x540e8a,0x5411e2,0x54ebc5,0x54f598,0x5508f3,0x5509d7,0x5509f1):assert target(caller)==0x540a90
tail=code(0x53eedd,5);assert tail[0]==0xe9 and 0x53eee2+struct.unpack('<i',tail[1:])[0]==0x540a90
assert target(0x95154e)==0x541110
assert target(0x5411e2)==0x540a90 and code(0x5411da,5).hex()=='ba03000000'
for table,upload in [(0x1d046f8,0x952e70),(0x1d05658,0x952f50),(0x1d056b8,0x952f50),(0x1d05718,0x952f50)]:
    assert pointer(table+0x10+6*8)==0x951520 and pointer(table+0x10+7*8)==upload
assert code(0x8af5a7,7).hex()=='4533c0418d5006' and target(0x8af5b5)==0x7ec5b0
assert target(0x8af5cb)==0x8af6a0
assert code(0x8af640,5).hex()=='ba07000000' and target(0x8af64c)==0x7ec5b0
assert target(0x8af762)==0xaa96f0 and target(0x8af779)==0x7ad60
assert target(0x753d04)==0x7506d0
assert code(0x18e0d2,4).hex()=='41894254' # destination(binding+30)+54=cache84, after pose stores.
assert target(0x75422f)==0x53d6a0 and target(0x53d700)==0x9b900
print('PASS: pinned game counter owner/read/increment, canonical phase6 materializer ABI, phase6 to attachment wait to phase7, secondary source leaf')
print('LIMIT: canonical scheduling and exact observed content identity do not cover every indirect/event writer or certify a weapon retarget outcome')

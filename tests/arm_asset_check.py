"""Guard checks for the independent read-only arm resource extractor."""
import io
from pathlib import Path
import struct
import sys
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from extract_arm_assets import index_entries,unpack_asset,resource_path
def reject(action):
    try: action()
    except (ValueError,UnicodeError,zlib.error): return
    raise AssertionError('Invalid asset input was accepted')
names=('baseModel','models/player_leftarm_emily.md6mesh','generated/player_leftarm_emily.bmd6model')
entry=struct.pack('>I',7)+b''.join(struct.pack('<I',len(n))+n.encode() for n in names)
entry+=struct.pack('>QII6sHH',4096,1000,30,b'\0'*6,0,20)
index=b'\x05SER'+b'\0'*28+struct.pack('>I',1)+entry
decoded=list(index_entries(io.BytesIO(index)))[0]
assert decoded['resource_id']==5 and decoded['offset']==4096 and decoded['destination']==names[2]
assert resource_path(Path('base'),'game1',5)==Path('base/game1_005.resources')
assert resource_path(Path('base'),'game1',8192)==Path('base/shared_2_3.sharedrsc')
reject(lambda:resource_path(Path('base'),'../elsewhere',0))
reject(lambda:resource_path(Path('base'),'game1',999))
reject(lambda:list(index_entries(io.BytesIO(index[:-1]))))
bad=index[:40]+struct.pack('<I',5000)
reject(lambda:list(index_entries(io.BytesIO(bad))))
data=b'MM6B'+b'a'*10000+b'MM6B';packed=zlib.compress(data)
assert unpack_asset(packed,len(data))==data
assert unpack_asset(data,len(data))==data
reject(lambda:unpack_asset(packed,10))
reject(lambda:unpack_asset(packed[:-2],len(data)))
reject(lambda:unpack_asset(packed+b'extra',len(data)))
print('PASS: native resource index fields, bounded names, exact decompression, truncated data, container selector and output path isolation')

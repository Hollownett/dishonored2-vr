"""Synthetic packed planar geometry checks; no game memory or resources."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyse_arm_mesh import decode_mesh
def name(value): return struct.pack('<I',len(value))+value.encode()
def mesh(weight=255,joint=2,indices=(0,1,2),position=0.):
    head=b'MM6B'+name('synthetic.md6skl')+b'\0'*2+struct.pack('<6f',0,0,0,1,1,1)+struct.pack('<I',1)
    geometry=b'BTRI'+struct.pack('<3I',0x10002,10,10)+struct.pack('<10f',1,1,1,0,0,0,1,1,0,0)+struct.pack('<IQ',3,0)
    raw=struct.pack('<9f',position,0,0,1,0,0,0,1,0)+b'\0'*(3*16)
    raw+=bytes((weight,0,0,0,0,0,0,0,joint,0,0,0,0,0,0,0))*3
    geometry+=raw+struct.pack('<I3H',3,*indices)+struct.pack('<6f',0,0,0,1,1,1)+struct.pack('<II',0,1)+struct.pack('<3I',0,0,3)+b'\0'*48
    return head+name('mesh')+name('material')+b'\0'*9+geometry+b'MM6B'
def reject(data):
    try: decode_mesh(data)
    except (ValueError,UnicodeError): return
    raise AssertionError('Malformed compiled arm geometry accepted')
p=decode_mesh(mesh());assert p['surfaces'][0]['vertices_count']==3
assert decode_mesh(mesh(joint=19),20)['surfaces'][0]['vertices_count']==3
try:decode_mesh(mesh(joint=20),20)
except ValueError:pass
else:raise AssertionError('Body vertex escaped its 20-joint palette')
assert p['surfaces'][0]['vertices'][1][0]==(1.,0.,0.)
assert p['undecoded_footer_bytes']==4
reject(mesh(weight=254));reject(mesh(joint=69));reject(mesh(indices=(0,1,3)));reject(mesh(position=float('nan')))
reject(mesh(position=10));reject(mesh()[:-4])
print('PASS: planar positions, eight weight/index records, native bounds, weight normalisation, palette/triangle limits, finite data and truncation')

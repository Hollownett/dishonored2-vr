"""Synthetic read-only wrapper ownership/race and draw identity checks."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inspect_native_surface_buffers import buffer_list,resource_name
from compare_native_surface_draws import matches

class Memory:
    def __init__(self):self.data={};self.recycle=0
    def put(self,at,data):
        for i,b in enumerate(data):self.data[at+i]=b
    def read(self,at,size):
        result=bytes(self.data[at+i]for i in range(size))
        if at==self.recycle:self.recycle=0;self.put(at,bytes(size))
        return result

m=Memory();geometry=0x10000;data=0x20000;wrapper=0x30000;com=0x40000
m.put(geometry+0xa0,struct.pack('<QII',data,1,15<<8));m.put(data,struct.pack('<QB7xQQ',wrapper,1,0,12))
m.put(wrapper,struct.pack('<QB3x5I',com,0,36,0,0,0,1));m.put(com,struct.pack('<Q',0x50000))
good=buffer_list(m.read,geometry,0xa0,'vertex');assert good[0]['com_buffer']==com and good[0]['entry_stride']==12
def rejected():
    try:buffer_list(m.read,geometry,0xa0,'vertex')
    except ValueError:return
    raise AssertionError('Invalid wrapper/list race accepted')
m.put(wrapper+28,struct.pack('<I',2));rejected();m.put(wrapper+28,struct.pack('<I',1))
m.recycle=wrapper;rejected();m.put(wrapper,struct.pack('<QB3x5I',com,0,36,0,0,0,1))
m.recycle=geometry+0xa0;rejected()
surface=dict(native_index_buffers=[dict(com_buffer=900,entry_base_offset=0,byte_width=6)],native_vertex_buffers=good,
    native_index_element_size=2,indices=3,draw_range_records=[[0,0,3]])
row=dict(vertex_slot='0',vertex_buffer=str(com),vertex_stride='12',vertex_offset='0',vertex_byte_width='36',index_buffer='900',
    index_offset='0',index_byte_width='6',index_format='57',topology='4',instances='1',first_instance='0',start_index='0',base_vertex='0',index_count='3')
assert matches(surface,[row])
for key in ('vertex_buffer','index_buffer','vertex_stride','vertex_offset','index_offset','vertex_byte_width','index_byte_width','start_index','base_vertex','topology','index_format'):
    changed=dict(row);changed[key]=str(int(changed[key])+1);assert not matches(surface,[changed]),key
assert not matches(surface,[row,row]);partial=dict(surface);partial['draw_range_records']=[[0,0,2]];assert not matches(partial,[row])
m.put(0x60008,struct.pack('<Q',0x70000));m.put(0x70000,b'models/test.material\0'+bytes(491));assert resource_name(m.read,0x60000)=='models/test.material'
m.recycle=0x60008
try:resource_name(m.read,0x60000)
except ValueError:pass
else:raise AssertionError('Recycled resource name accepted')
print('PASS: verified wrapper fields, list/wrapper races, full IA identity/range/format and same-count pointer collision rejection')

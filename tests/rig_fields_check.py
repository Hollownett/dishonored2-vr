"""Synthetic guard checks. They do not establish a live player rig or write ABI."""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from rig_fields import animation_snapshot, body_snapshot, player_components, checked_component, component_ready, decode_transform, joint_name, native_list, skeleton_parents

base = 0x7ff712340000
game, player, model, animation, skeleton, descriptor = [0x100000+i*0x400000 for i in range(6)]
names, transforms, mapping, handle = [0x2100000+i*0x10000 for i in range(4)]
tables = {name: [{'subobject_offset': 0, 'vtable_rva': rva}] for name, rva in
          [('arkPlayer', 0x1c5e428), ('arkSkinnedRenderModel', 0x1b0d628), ('arkCpntAnimEvolved', 0x1b80408)]}
tables['arkCpntAnimEvolved'].append({'subobject_offset': 0x20, 'vtable_rva': 0x1b804f0})
memory = {}


def put(address, data):
    memory.update({address+i: byte for i, byte in enumerate(data)})


def q(address, value):
    put(address, struct.pack('<Q', value))


def read(address, size):
    try:
        return bytes(memory[address+i] for i in range(size))
    except KeyError as error:
        raise OSError('Unreadable synthetic memory') from error


def reject(action):
    try:
        action()
    except (ValueError, OSError):
        return
    raise AssertionError('Unsafe rig snapshot was accepted')


q(base+0x39a5290, game)
q(game, base+0x1c940a8)
q(game+0x3130b0, player)
q(player, base+tables['arkPlayer'][0]['vtable_rva'])
q(player+0xf0, model)
q(player+0x360, model)
q(model, base+tables['arkSkinnedRenderModel'][0]['vtable_rva'])
put(model+0x1860, struct.pack('<15f', 10, 20, 30, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1))
q(model+0x1fe8, descriptor)
q(descriptor+0x50, skeleton)
q(skeleton+0x70, names)
put(skeleton+0x78, struct.pack('<H', 2))
put(names, struct.pack('<HH', 10, 11))
name_manager = base+0x22995d0
name_entries, left_name, right_name = 0x2500000, 0x2600003, 0x2610005
q(base+0x22995c0, name_manager)
q(name_manager, base+0x1b093c8)
put(name_manager+8, struct.pack('<QII', name_entries, 12, 12 << 8))
q(name_entries+10*0x48+0x10, left_name)
q(name_entries+11*0x48+0x10, right_name)
put(left_name, b'LeftHand\0'+b'\0'*32)
put(right_name, b'RightHand\0'+b'\0'*32)
for table in tables['arkCpntAnimEvolved']:
    q(animation+table['subobject_offset'], base+table['vtable_rva'])
q(animation+0x38, model)
put(animation+0x18, struct.pack('<iI', 77, 0x0a))
put(animation+0x2d8, struct.pack('<QII', transforms, 2, 2 << 8))
pose_states = animation+0x2f8
put(animation+0x2e8, struct.pack('<QII', pose_states, 4, 4 << 8))
put(pose_states, struct.pack('<QII', mapping, 2, 2 << 8))
for state in (1,2,3):
    put(pose_states+state*16, struct.pack('<QII', mapping, 2, 2 << 8))
final_mapping = mapping+0x100
put(final_mapping, struct.pack('<hh', 0, 1))
put(pose_states+3*16, struct.pack('<QII', final_mapping, 2, 2 << 8))
put(mapping, struct.pack('<hh', 1, 0))
matrix = [1, 0, 0, float('nan'), 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 0]
put(transforms, struct.pack('<16f', *matrix))
matrix[12] = 4
put(transforms+64, struct.pack('<16f', *matrix))
snapshot = lambda: animation_snapshot(read, animation, base, tables, (40, 0))
result = snapshot()
assert result['sampled_joints'][0]['position'] == [4, 2, 3]
assert result['sampled_joints'][1]['position'] == [1, 2, 3]
assert [j['name'] for j in result['sampled_joints']] == ['LeftHand', 'RightHand']
assert joint_name(read, base, 0xffff) is None
reject(lambda: joint_name(read, base, 12))
assert not result['rig_binding_verified'] and not result['snapshot_frame_synchronised']
assert result['pose_state'] == 0 and not result['final_blended_pose_verified']
render_source = animation_snapshot(read, animation, base, tables, (40, 0), 3)
assert render_source['pose_state'] == 3 and render_source['pose_role'] == 'renderer_upload_source'
assert render_source['model_origin'] == [10,20,30] and not render_source['final_blended_pose_verified']
assert render_source['sampled_joints'][0]['position'] == [1,2,3]
reject(lambda: animation_snapshot(read, animation, base, tables, (40, 0), 4))
saved_final_map = read(pose_states+3*16, 16)
put(pose_states+3*16, struct.pack('<QII', mapping, 1, 2 << 8))
reject(lambda: animation_snapshot(read, animation, base, tables, (40, 0), 3))
put(pose_states+3*16, saved_final_map)
saved_states = read(animation+0x2e8, 16)
put(animation+0x2e8, struct.pack('<QII', mapping, 2, 2 << 8))
reject(snapshot) # A flattened map is not the native four-state container.
put(animation+0x2e8, saved_states)
assert component_ready(0x0a, 40, 0) and not component_ready(0, 40, 0)
put(handle, struct.pack('<iIQ', 77, 0, animation))
assert checked_component(read, handle, 40, 0) == animation
put(handle, struct.pack('<iIQ', 78, 0, animation))
assert checked_component(read, handle, 40, 0) is None
put(handle, struct.pack('<iIQ', -1, 0, animation))
assert checked_component(read, handle, 40, 0) is None
put(mapping, struct.pack('<hh', 2, 0))
reject(snapshot)
put(mapping, struct.pack('<hh', 1, 0))
q(player+0x360, model+8)
reject(snapshot)
q(player+0x360, model)
q(animation+0x20, 0)
reject(snapshot)
q(animation+0x20, base+tables['arkCpntAnimEvolved'][1]['vtable_rva'])
put(animation+0x2d8, struct.pack('<QII', transforms, 3, 2 << 8))
reject(lambda: native_list(read, animation+0x2d8, 64, 4096))
put(animation+0x2d8, struct.pack('<QII', transforms, 2, 2 << 8))
matrix[0] = -1
reject(lambda: decode_transform(struct.pack('<16f', *matrix)))

# A matching body vtable alone is insufficient: require the current player and
# model references before accepting separate arm skeletons.
components, body = 0x2710000, 0x2810000
tables['arkCpntEquipment'] = [{'subobject_offset': 0, 'vtable_rva': 0x1b9ae68}]
tables['arkCpntPlayerBody'] = [{'subobject_offset': 0, 'vtable_rva': 0x1c5a8d8},
                            {'subobject_offset': 0x20, 'vtable_rva': 0x1c5a9b0}]
for table in tables['arkCpntPlayerBody']:
    q(body+table['subobject_offset'], base+table['vtable_rva'])
put(body+0x18, struct.pack('<iI', 88, 0x0a))
q(body+0x10, player)
q(body+0x58, animation)
q(animation+0x10, player)
put(player+0x268, struct.pack('<QII', components, 2, 2 << 8))
put(components, struct.pack('<QQ', body, animation))
assert [c['address'] for c in player_components(read, base, tables)] == [body, animation]
q(body+0x378, animation)
q(body+0x688, 0)
body_read = lambda: body_snapshot(read, body, base, tables, (40, 0))
body_result = body_read()
assert body_result['arms'][0]['joint_names'] == ['LeftHand', 'RightHand']
assert not body_result['arms'][1]['rig_allocated'] and not body_result['rig_binding_verified']
q(body+0x10, player+8)
reject(body_read)
q(body+0x10, player)
q(animation+0x38, model+8)
reject(body_read)
q(animation+0x38, model)
q(body+0x20, 0)
reject(body_read)
q(body+0x20, base+0x1c5a9b0)
q(body+0x378, body)
reject(body_read)
q(body+0x378, animation)

def recycled_read(address, size):
    value = read(address, size)
    if address == transforms+64:
        put(animation+0x18, struct.pack('<iI', 78, 0x0a))
    return value

reject(lambda: animation_snapshot(recycled_read, animation, base, tables, (40, 0)))
put(skeleton, bytes(0x130));q(skeleton,base+0x1b0bd08)
q(base+0x1b0bd08+0x88,base+0x24f240)
q(skeleton+0x70,names);put(skeleton+0x78,struct.pack('<H',2))
hierarchy_blob=0x3000000;q(skeleton+0x158,hierarchy_blob)
put(hierarchy_blob+0x1c,struct.pack('<I',0x100));put(hierarchy_blob+0x11c,struct.pack('<HH',4,1))
put(hierarchy_blob+0x40,bytes(20));put(hierarchy_blob+0x42+4*4,struct.pack('<H',0xffff))
put(hierarchy_blob+0x42+1*4,struct.pack('<H',0x8000))
assert skeleton_parents(read,skeleton,base,2)==[-1,0]
inspected=body_snapshot(read,body,base,tables,(40,0),inspect_skeleton=True)
assert inspected['arms'][0]['skeleton_layout']['parent_indices']==[-1,0]
assert not inspected['arms'][0]['skeleton_layout']['engine_methods_invoked']
put(hierarchy_blob+0x42+1*4,struct.pack('<H',0x8001))
reject(lambda: skeleton_parents(read,skeleton,base,2))
put(hierarchy_blob+0x42+1*4,struct.pack('<H',0x8002))
reject(lambda: skeleton_parents(read,skeleton,base,2))
put(hierarchy_blob+0x1c,struct.pack('<I',0))
reject(lambda: skeleton_parents(read,skeleton,base,2))
print('PASS: joint mapping, owned arm/skeleton layout, remapped parents, cycle/bounds rejection and stale/type/owner/list guards. Synthetic memory only.')

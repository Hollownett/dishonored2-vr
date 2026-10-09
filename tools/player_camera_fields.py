"""Owned native camera state inspection for the pinned executable; no writes/calls."""
import math
import struct
from rig_fields import pointer, component_ready


def player_camera_snapshot(read, base, player, equipment=None, *, camera=None):
    """Generated camera metadata 0x159baf0, dual RTTI and owned equipment link."""
    if equipment is not None:
        if pointer(read,equipment)!=base+0x1b9ae68 or pointer(read,equipment+0x10)!=player:
            raise ValueError('Equipment owner/type differs')
        camera=pointer(read,equipment+0x48)
    if camera is None:raise ValueError('An owned camera or equipment link is required')
    expected=((0,0x1c72218),(0x20,0x1c722f8))
    def identity():
        if any(pointer(read,camera+offset)!=base+table for offset,table in expected) or pointer(read,camera+0x10)!=player:
            raise ValueError('Native camera owner/type differs')
        generation,flags=struct.unpack('<iI',read(camera+0x18,8))
        if generation==-1 or not component_ready(flags,40,0):
            raise ValueError('Native camera is not started')
        return generation,flags
    before=identity()
    # Read only the generated state region, not the 0x25150-byte camera base.
    raw=read(camera+0x25150,0x200)
    body=struct.unpack_from('<Q',raw,0x20)[0]
    if pointer(read,body)!=base+0x1c5a8d8 or pointer(read,body+0x10)!=player:
        raise ValueError('Camera body owner/type differs')
    def floats(offset,count=1):
        values=struct.unpack_from('<'+'f'*count,raw,offset-0x25150)
        if not all(math.isfinite(v) and abs(v)<1e7 for v in values):
            raise ValueError('Camera state contains invalid floats')
        return list(values) if count>1 else values[0]
    names={'max_pitch_up':0x251a4,'max_pitch_down':0x251a8,'joystick_turn_speed':0x251ac,
           'mouse_turn_speed':0x251b0,'pitch':0x25264,'arms_pitch':0x25268,'neck_pitch':0x2526c,
           'head_pitch':0x25270,'yaw':0x25274,'roll':0x25278,'last_pitch_velocity':0x252ac,
           'last_yaw_velocity':0x252b0,'last_constrained_pitch':0x252fc,'last_constrained_yaw':0x25300}
    values={name:floats(offset) for name,offset in names.items()}
    joints=list(struct.unpack_from('<6h',raw,0x48))
    if any(joint < -1 or joint>=2048 for joint in joints):
        raise ValueError('Camera control joint index exceeds native bounds')
    aim_origin=floats(0x2527c,3);aim_axis=floats(0x25288,9)
    controller=struct.unpack_from('<Q',raw,8)[0]
    if pointer(read,controller)!=base+0x1c72918 or pointer(read,controller+0x10)!=player:
        raise ValueError('Camera controller owner/type differs')
    look_raw=read(controller+0xd0,0x10)
    look=struct.unpack_from('<2f',look_raw)
    if not all(math.isfinite(v) and abs(v)<1e7 for v in look):raise ValueError('Controller look source invalid')
    physics=struct.unpack_from('<Q',raw,0x10)[0]
    physics_tables=((0,0x1c73268),(0x20,0x1c73340),(0x30,0x1c73378))
    if any(pointer(read,physics+offset)!=base+table for offset,table in physics_tables) or pointer(read,physics+0x10)!=player:
        raise ValueError('Camera physics owner/type differs')
    physics_identity=read(physics+0x18,8)
    physics_generation,physics_flags=struct.unpack('<iI',physics_identity)
    if physics_generation==-1 or not component_ready(physics_flags,40,0):
        raise ValueError('Camera physics is not started')
    heading_delegate=pointer(read,physics+0x48)
    # Native 0x1b2290 owns a non-polymorphic physics object with an embedded
    # arkPlayerUserData, backreference to this component, entity transform and
    # self link. Do not guess a vtable at offset zero of that object.
    if pointer(read,heading_delegate+0x160)!=base+0x1b02768 or \
            pointer(read,heading_delegate+0x168)!=physics or \
            pointer(read,heading_delegate+0x120)!=player+0x300 or \
            pointer(read,heading_delegate+0x1e0)!=heading_delegate:
        raise ValueError('Native physics heading delegate identity differs')
    heading=struct.unpack('<f',read(heading_delegate+0xd0,4))[0]
    if not math.isfinite(heading) or abs(heading)>math.pi+1e-5:
        raise ValueError('Native physics heading exceeds the wrapped radian range')
    if read(physics+0x18,8)!=physics_identity or pointer(read,physics+0x48)!=heading_delegate:
        raise ValueError('Physics heading ownership changed while reading')
    physics_yaw_velocity=struct.unpack('<f',read(heading_delegate+0xbc,4))[0]
    physics_yaw_impulse=struct.unpack('<f',read(heading_delegate+0x40,4))[0]
    entity_axis=list(struct.unpack('<9f',read(player+0x30c,36)))
    if not all(math.isfinite(v) and abs(v)<1e7 for v in [physics_yaw_velocity,physics_yaw_impulse,*entity_axis]):
        raise ValueError('Native physics velocity or entity axis invalid')
    if identity()!=before or (equipment is not None and
            (pointer(read,equipment+0x48)!=camera or pointer(read,equipment+0x10)!=player)):
        raise ValueError('Camera ownership changed while reading')
    return dict(camera=camera,body=body,controller=controller,physics=physics,
                heading_delegate=heading_delegate,physics_heading_radians=heading,
                physics_yaw_velocity=physics_yaw_velocity,physics_yaw_impulse=physics_yaw_impulse,
                entity_axis=entity_axis,
                heading_delegate_type_verified=True,player_owner_verified=True,
                generated_field_offsets_verified=True,read_consistent=read(camera+0x25150,0x200)==raw,
                control_joint_indices=joints,values=values,aim_origin=aim_origin,aim_axis=aim_axis,
                current_mode=struct.unpack_from('<i',raw,0xb0)[0],target_mode=struct.unpack_from('<i',raw,0xb4)[0],
                mode_blend_body_yaw=bool(raw[0x110]),code_lock_control=bool(raw[0x69]),
                controller_look_xy=list(look),controller_device_byte=look_raw[12],
                angular_units_verified=True,gameplay_heading_writer_verified=False,
                snapshot_frame_synchronised=False,game_memory_writes=False,
                source='Exact-build generated metadata 0x159baf0 and camera consumers 0x7f0bd0/0x7f1400')

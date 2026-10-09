"""Read-only player interaction ownership for the pinned Steam executable."""
import struct
from rig_fields import pointer, native_list, component_ready


def interaction_snapshot(read, base):
    game=pointer(read,base+0x39a5290)
    if pointer(read,game)!=base+0x1c940a8:raise ValueError('Game owner/type differs')
    player=pointer(read,game+0x3130b0)
    if pointer(read,player)!=base+0x1c5e428:raise ValueError('Player owner/type differs')
    data,count=native_list(read,player+0x268,8,4096)
    addresses=read(data,count*8) if count else b''
    candidates=[];sensor_candidates=[]
    for at in struct.unpack('<'+'Q'*count,addresses):
        if pointer(read,at)==base+0x1c72e98 and pointer(read,at+0x10)==player:candidates.append(at)
        if pointer(read,at)==base+0x1b403b8 and pointer(read,at+0x10)==player:sensor_candidates.append(at)
    result=dict(player=player,component_count=count,interaction_available=False,
                game_memory_writes=False,engine_calls=False,grab_implemented=False)
    if not candidates:return result
    if len(candidates)!=1:raise ValueError('Duplicate native player interaction owners')
    at=candidates[0];identity=read(at+0x18,8)
    generation,flags=struct.unpack('<iI',identity)
    if generation==-1 or not component_ready(flags,40,0):raise ValueError('Interaction is not started')
    raw=read(at+0x20,0x50)
    camera,state_machine=struct.unpack_from('<QQ',raw)
    if pointer(read,camera)!=base+0x1c72218 or pointer(read,camera+0x20)!=base+0x1c722f8 or pointer(read,camera+0x10)!=player:
        raise ValueError('Interaction camera owner/type differs')
    if pointer(read,state_machine)!=base+0x1c73d58 or pointer(read,state_machine+0x10)!=player:
        raise ValueError('Interaction state-machine owner/type differs')
    if pointer(read,state_machine+0x318)!=at:raise ValueError('State-machine interaction back-reference differs')
    camera_identity=read(camera+0x18,8);state_identity=read(state_machine+0x18,8)
    for own in (camera_identity,state_identity):
        gen,f=struct.unpack('<iI',own)
        if gen==-1 or not component_ready(f,40,0):raise ValueError('Interaction dependency is not started')
    sensor_state=None
    if len(sensor_candidates)>1:raise ValueError('Duplicate native interaction sensors')
    if sensor_candidates:
        sensor_at=sensor_candidates[0];sensor_identity=read(sensor_at+0x18,8)
        sensor_generation,sensor_flags=struct.unpack('<iI',sensor_identity)
        if sensor_generation==-1 or not component_ready(sensor_flags,40,0):raise ValueError('Interaction sensor is not started')
        sensor_raw=read(sensor_at+0x20,0x78)
        sensor_machine=struct.unpack_from('<Q',sensor_raw)[0]
        sensor_parent=struct.unpack_from('<Q',sensor_raw,0x10)[0]
        sensor_camera=struct.unpack_from('<Q',sensor_raw,0x20)[0]
        if sensor_machine!=state_machine or sensor_camera!=camera:
            raise ValueError(f'Interaction sensor dependencies differ: machine_matches={sensor_machine==state_machine}, camera_matches={sensor_camera==camera}')
        if pointer(read,sensor_parent)!=base+0x1c5b108 or pointer(read,sensor_parent+0x10)!=player:
            raise ValueError('Interaction sensor parent owner/type differs')
        if sensor_raw[0x18]>1:raise ValueError('Interaction sensor enabled flag differs')
        sensor_state=dict(address=sensor_at,parent=sensor_parent,enabled=bool(sensor_raw[0x18]),
            target_ids=list(struct.unpack_from('<4I',sensor_raw,0x64)))
        if read(sensor_at+0x18,8)!=sensor_identity or read(sensor_at+0x20,0x78)!=sensor_raw:
            raise ValueError('Interaction sensor lifetime/state changed')
    target_generation=struct.unpack_from('<i',raw,0x10)[0]
    target=struct.unpack_from('<Q',raw,0x18)[0]
    target_state=dict(generation=target_generation,pointer=target,available=False)
    if target_generation!=-1 and target:
        if not 0x10000<=target<0x0000800000000000 or target%8:raise ValueError('Invalid interaction target reference')
        target_identity=read(target+0x18,8)
        actual_generation,target_flags=struct.unpack('<iI',target_identity)
        if actual_generation==target_generation:
            table=pointer(read,target);owner=pointer(read,target+0x10)
            if not base<=table<base+0x041cb000:raise ValueError('Target vtable outside the native image')
            target_state.update(available=True,owner=owner,vtable_rva=hex(table-base),flags=target_flags)
            if read(target+0x18,8)!=target_identity:raise ValueError('Target lifetime changed')
    if raw[0x20]>1 or raw[0x4c]>1:raise ValueError('Interaction state flags differ')
    if (read(at+0x18,8)!=identity or read(at+0x20,0x50)!=raw or
            read(camera+0x18,8)!=camera_identity or read(state_machine+0x18,8)!=state_identity or
            native_list(read,player+0x268,8,4096)!=(data,count) or
            (read(data,count*8) if count else b'')!=addresses or pointer(read,game+0x3130b0)!=player):
        raise ValueError('Interaction owner or target changed during inspection')
    result.update(interaction_available=True,interaction=at,generation=generation,camera=camera,state_machine=state_machine,
                  native_interacting=bool(raw[0x20]),native_secondary_interacting=bool(raw[0x4c]),target_reference=target_state)
    result['interaction_sensor']=sensor_state
    return result

"""Bounded read-only lean owner/state guards for the pinned Steam build."""
import math
import struct
from rig_fields import pointer, native_list, component_ready


def discover_lean_trial_owner(read, base):
    game=pointer(read,base+0x39a5290)
    if pointer(read,game)!=base+0x1c940a8:raise ValueError('Game type differs')
    player=pointer(read,game+0x3130b0)
    if pointer(read,player)!=base+0x1c5e428:raise ValueError('Primary player type differs')
    identity=read(player+0xa8,4)
    header=read(player+0x268,16);data,count=native_list(read,player+0x268,8,128)
    raw=read(data,count*8) if count else b''
    found={0x1c73d58:[],0x1c72218:[]}
    for at in struct.unpack('<'+'Q'*count,raw):
        if not at:continue
        table=pointer(read,at)-base
        if table in found and pointer(read,at+0x10)==player:found[table].append(at)
    if any(len(items)!=1 for items in found.values()):raise ValueError('Unique owned FSM/camera required')
    owner=dict(game=game,player=player,player_id=struct.unpack('<I',identity)[0],machine=found[0x1c73d58][0],camera=found[0x1c72218][0])
    owner['controller']=pointer(read,owner['machine']+0x2f8);owner['body']=pointer(read,owner['machine']+0x300)
    owner['identities']={key:read(owner[key]+0x18,8).hex() for key in ('machine','camera','controller','body')}
    verify_lean_trial_owner(read,base,owner)
    if read(player+0x268,16)!=header or (count and read(data,count*8)!=raw) or read(player+0xa8,4)!=identity:raise ValueError('Discovery changed during read')
    return owner


def verify_lean_trial_owner(read,base,owner):
    game=owner['game'];player=owner['player'];machine=owner['machine']
    if pointer(read,base+0x39a5290)!=game or pointer(read,game)!=base+0x1c940a8 or pointer(read,game+0x3130b0)!=player or pointer(read,player)!=base+0x1c5e428 or struct.unpack('<I',read(player+0xa8,4))[0]!=owner['player_id']:raise ValueError('Primary owner changed')
    tables={'machine':((0,0x1c73d58),),'camera':((0,0x1c72218),(0x20,0x1c722f8)),'controller':((0,0x1c72918),),'body':((0,0x1c5a8d8),(0x20,0x1c5a9b0))}
    for key,expected in tables.items():
        at=owner[key];identity=read(at+0x18,8);generation,flags=struct.unpack('<iI',identity)
        if generation==-1 or not component_ready(flags,40,0) or identity.hex()!=owner['identities'][key] or pointer(read,at+0x10)!=player or any(pointer(read,at+off)!=base+vt for off,vt in expected):raise ValueError(f'{key} ownership/type/generation changed')
    if pointer(read,machine+0x2f8)!=owner['controller'] or pointer(read,machine+0x300)!=owner['body']:raise ValueError('FSM dependencies changed')
    if pointer(read,owner['camera']+0x25158)!=owner['controller'] or pointer(read,owner['camera']+0x25170)!=owner['body']:raise ValueError('Camera/FSM dependencies differ')


def lean_trial_state(read,base,owner):
    verify_lean_trial_owner(read,base,owner)
    machine=owner['machine'];pool=pointer(read,machine+0x90);state=pointer(read,pool+0x10+18*8)
    declaration=pointer(read,state+0x18);device=pointer(read,base+0x2274340)
    def state_fields():
        if pointer(read,state)!=base+0x1c82b28 or struct.unpack('<i',read(state+8,4))[0]!=18 or pointer(read,state+0x10)!=machine or pointer(read,declaration)!=base+0x1c829f8 or struct.unpack('<i',read(declaration+0x60,4))[0]!=18:raise ValueError('Lean slot/type/declaration differs')
        active=read(state+0x20,1)[0];action=struct.unpack('<I',read(state+0x30,4))[0]
        gamepad=read(device+0xa544,1)[0];option=struct.unpack('<i',read(base+0x3995628,4))[0]
        if active>1 or action>=64 or gamepad>1 or option not in (0,1) or pointer(read,base+0x3995630)!=base+0x1c82bf8:raise ValueError('Lean fields invalid')
        return active,action,gamepad,option
    fields=state_fields();verify_lean_trial_owner(read,base,owner)
    if pointer(read,machine+0x90)!=pool or pointer(read,pool+0x10+18*8)!=state or pointer(read,state+0x18)!=declaration or pointer(read,base+0x2274340)!=device or state_fields()!=fields:raise ValueError('Lean state changed during read')
    return dict(state=state,pool=pool,declaration=declaration,active=bool(fields[0]),action_index=fields[1],gamepad=bool(fields[2]),toggle_option=fields[3])

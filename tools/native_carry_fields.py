"""Exact read-only carry ownership guards shared by native source inspection."""
import struct
from rig_fields import pointer, native_list, component_ready


def identity(read, at):
    raw=read(at+0x18,8);generation,flags=struct.unpack('<iI',raw)
    if generation==-1 or not component_ready(flags,40,0):raise ValueError('Carry component not started')
    return raw.hex()


def carry_current(read,base,old):
    if len(old.get('states',[]))!=2:raise ValueError('Require both typed carry states')
    def root():
        if (pointer(read,base+0x39a5290)!=old['game'] or pointer(read,old['game'])!=base+0x1c940a8 or
            pointer(read,old['game']+0x3130b0)!=old['player'] or pointer(read,old['player'])!=base+0x1c5e428 or
            pointer(read,old['body'])!=base+0x1c5a8d8 or pointer(read,old['body']+0x20)!=base+0x1c5a9b0 or
            pointer(read,old['body']+0x10)!=old['player'] or identity(read,old['body'])!=old['body_identity'] or
            pointer(read,old['machine'])!=base+0x1c73d58 or pointer(read,old['machine']+0x10)!=old['player'] or
            identity(read,old['machine'])!=old['machine_identity'] or pointer(read,old['machine']+0x300)!=old['body'] or
            pointer(read,old['machine']+0x90)!=old['pool'] or read(old['player']+0x268,16).hex()!=old['components_raw'] or
            pointer(read,old['component_slot'])!=old['machine']):raise ValueError('Carry owner changed')
    result=dict(old);first=None
    for _ in range(2):
        root();active=[]
        for item,index,vt,declvt in zip(old['states'],(49,45),(0x1c7a998,0x1c77748),(0x1c7a868,0x1c77618)):
            at,decl=item['state'],item['declaration'];value=read(at+0x20,1)[0]
            if (pointer(read,old['pool']+0x10+index*8)!=at or pointer(read,at)!=base+vt or
                struct.unpack('<i',read(at+8,4))[0]!=index or pointer(read,at+0x10)!=old['machine'] or
                pointer(read,at+0x18)!=decl or pointer(read,decl)!=base+declvt or
                struct.unpack('<i',read(decl+0x60,4))[0]!=index or value>1):raise ValueError('Carry state type/owner/active differs')
            active.append(bool(value))
        if first is not None and first!=active:raise ValueError('Carry activation changed during snapshot')
        first=active
    root();result['active']=first;result['carrying']=any(first);result['typed_owner_verified']=True
    return result


def carry_snapshot(read,base,game,player,body):
    components_raw=read(player+0x268,16)
    data,count=native_list(read,player+0x268,8,4096)
    if not count:raise ValueError('Missing carry owner components')
    raw=read(data,count*8);entries=struct.unpack('<'+'Q'*count,raw)
    machines=[]
    for i,at in enumerate(entries):
        if 0x10000<=at<0x800000000000 and at%8==0 and struct.unpack('<Q',read(at,8))[0]==base+0x1c73d58 and pointer(read,at+0x10)==player:machines.append((at,data+i*8))
    if len(machines)!=1:raise ValueError('Ambiguous carry FSM owner')
    machine,slot=machines[0];pool=pointer(read,machine+0x90)
    states=[]
    for index in (49,45):
        at=pointer(read,pool+0x10+index*8);states.append(dict(index=index,state=at,declaration=pointer(read,at+0x18)))
    old=dict(game=game,player=player,body=body,machine=machine,pool=pool,component_slot=slot,
             components_raw=components_raw.hex(),body_identity=identity(read,body),machine_identity=identity(read,machine),states=states)
    result=carry_current(read,base,old)
    if read(data,count*8)!=raw:raise ValueError('Carry component membership changed')
    return result

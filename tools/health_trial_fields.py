"""Pinned native NPC health source guards; read-only, no engine calls."""
import math
import struct
from rig_fields import pointer,native_list,component_ready
from object_fields import entity_for_id


def primary_owner(read,base):
    game=pointer(read,base+0x39a5290);player=pointer(read,game+0x3130b0)
    if pointer(read,game)!=base+0x1c940a8 or pointer(read,player)!=base+0x1c5e428:raise ValueError('Primary type differs')
    eid=struct.unpack('<I',read(player+0xa8,4))[0]
    if entity_for_id(read,game,eid)!=player:raise ValueError('Primary registry ID differs')
    return dict(game=game,player=player,player_id=eid)


def primary_feet(read,base,owner):
    if primary_owner(read,base)!=owner:raise ValueError('Primary owner changed')
    feet=struct.unpack('<3f',read(owner['player']+0x300,12))
    if not all(math.isfinite(x) and abs(x)<1e7 for x in feet):raise ValueError('Primary position invalid')
    if primary_owner(read,base)!=owner:raise ValueError('Primary owner changed during read')
    return list(feet)


def authored_name(read,entity):
    at=pointer(read,entity+0x20,1);raw=bytearray()
    for i in range(256):
        b=read(at+i,1)
        if b==b'\0':break
        raw.extend(b)
    else:raise ValueError('Authored name exceeds bound')
    name=raw.decode('utf-8')
    if not name or any(ord(x)<32 for x in name) or pointer(read,entity+0x20,1)!=at or read(at,len(raw)+1)!=bytes(raw)+b'\0':raise ValueError('Authored name changed/invalid')
    return at,name


def discover_health_actor(read,base,game,eid):
    entity=entity_for_id(read,game,eid)
    if entity is None or pointer(read,entity)!=base+0x1c5e108:raise ValueError('NPC ID/type differs')
    name_pointer,name=authored_name(read,entity)
    header=read(entity+0x268,16);data,count=native_list(read,entity+0x268,8,128)
    raw=read(data,count*8) if count else b'';found=[]
    for at in struct.unpack('<'+'Q'*count,raw):
        if at and pointer(read,at)==base+0x1c51588 and pointer(read,at+0x10)==entity:found.append(at)
    if len(found)!=1:raise ValueError('Unique owned HealthEvolved required')
    at=found[0];identity=read(at+0x18,8);declaration=pointer(read,at+8)
    descriptor_vtable=pointer(read,declaration)
    if not base<=descriptor_vtable<base+0x041cb000:raise ValueError('Health declaration vtable outside image')
    owner=dict(game=game,entity=entity,entity_id=eid,health_component=at,health_identity=identity.hex(),declaration=declaration,declaration_vtable=descriptor_vtable,name_pointer=name_pointer,authored_name=name)
    health_actor_sample(read,base,owner)
    if read(entity+0x268,16)!=header or (count and read(data,count*8)!=raw):raise ValueError('Health discovery changed')
    return owner


def health_actor_sample(read,base,owner):
    entity=owner['entity'];at=owner['health_component'];game=owner['game'];eid=owner['entity_id'];decl=owner['declaration']
    def guard():
        if pointer(read,base+0x39a5290)!=game or pointer(read,game)!=base+0x1c940a8 or entity_for_id(read,game,eid)!=entity or pointer(read,entity)!=base+0x1c5e108:raise ValueError('NPC registry/type changed')
        identity=read(at+0x18,8);generation,flags=struct.unpack('<iI',identity)
        if identity.hex()!=owner['health_identity'] or generation==-1 or not component_ready(flags,40,0) or pointer(read,at)!=base+0x1c51588 or pointer(read,at+0x10)!=entity or pointer(read,at+8)!=decl or pointer(read,decl)!=owner['declaration_vtable']:raise ValueError('Health ownership/generation/declaration changed')
        encoded=owner['authored_name'].encode('utf-8')+b'\0'
        if pointer(read,entity+0x20,1)!=owner['name_pointer'] or read(owner['name_pointer'],len(encoded))!=encoded:raise ValueError('Authored identity changed')
    guard()
    def values():
        health=struct.unpack('<f',read(at+0x20,4))[0];maximum=struct.unpack('<f',read(decl+0x68,4))[0]
        auxiliary=struct.unpack('<Q',read(at+0x70,8))[0];flags=read(entity+0xd1,1)[0]
        if not all(math.isfinite(x) and abs(x)<1e7 for x in (health,maximum)):raise ValueError('Health source invalid')
        return dict(health_source=health,base_health_source=maximum,health_auxiliary=auxiliary,owner_byte_d1=flags,damage_exclusion_bit8=bool(flags&8),exact_getter_value_verified=auxiliary==0)
    before=values();position=struct.unpack('<3f',read(entity+0x300,12));after=values();position_after=struct.unpack('<3f',read(entity+0x300,12));guard()
    if not all(math.isfinite(x) and abs(x)<1e7 for x in (*position,*position_after)):raise ValueError('NPC position invalid')
    # Auxiliary may alter getter value; never label raw +20 as exact health then.
    return dict(**before,health_after=after['health_source'],health_fields_consistent=before==after,position=list(position),position_after=list(position_after),position_consistent=position==position_after)


def nearest_health_actors(read,base,primary,radius=100,limit=4,extra_ids=()):
    origin=primary_feet(read,base,primary);game=primary['game'];candidates=[]
    for start in range(0,0x3fff,2048):
        count=min(2048,0x3fff-start)
        registry=struct.unpack('<'+'Q'*count,read(game+0x3130b0+start*8,count*8))
        for index,entity in enumerate(registry,start):
            if not entity or entity==primary['player']:continue
            try:
                if pointer(read,entity)!=base+0x1c5e108:continue
                eid=struct.unpack('<I',read(entity+0xa8,4))[0]
                if eid>>16!=index or entity_for_id(read,game,eid)!=entity:continue
                position=struct.unpack('<3f',read(entity+0x300,12));distance=math.dist(position,origin)
                if not math.isfinite(distance) or distance>radius:continue
                _,name=authored_name(read,entity)
                if 'dunwall_guard' not in name.lower() and 'mortimer_ramsey.def' not in name.lower():continue
                owner=discover_health_actor(read,base,game,eid);sample=health_actor_sample(read,base,owner)
                if sample['health_source']>0 and sample['exact_getter_value_verified'] and not sample['damage_exclusion_bit8']:candidates.append((distance,owner))
            except (ValueError,OSError,UnicodeError):continue
    candidates.sort(key=lambda pair:pair[0]);selected=[owner for _,owner in candidates[:limit]]
    for _,owner in candidates:
        if 'mortimer_ramsey.def' in owner['authored_name'].lower() and owner not in selected:selected.append(owner)
    for eid in extra_ids:
        try:
            owner=discover_health_actor(read,base,game,eid)
            if owner not in selected:selected.append(owner)
        except (ValueError,OSError,UnicodeError):pass
    if primary_owner(read,base)!=primary:raise ValueError('Primary changed during actor discovery')
    return selected[:8]

"""Bounded, read-only native entity/rigid-body discovery; no engine calls."""
import math
import struct
from rig_fields import pointer, native_list, component_ready, decode_transform

INVALID_ENTITY = 0x3fff0010
BODY_TYPES = {0x1c745c8: ('rigid_body', 0x1c74798),
              0x1b64b68: ('throwable', 0x1b64d38)}


def entity_for_id(read, game, entity_id):
    # Exact native 0x7b5e00, with explicit signed-index bounds.
    index = entity_id >> 16
    if entity_id == INVALID_ENTITY or not 0 <= index < 0x3fff:
        return None
    at = struct.unpack('<Q', read(game+0x3130b0+index*8, 8))[0]
    if not at or not 0x10000 <= at < 0x0000800000000000 or at % 8:
        return None
    return at if struct.unpack('<I', read(at+0xa8, 4))[0] == entity_id else None


def rigid_bodies(read, base, entity):
    game = pointer(read, base+0x39a5290)
    if pointer(read, game) != base+0x1c940a8:
        raise ValueError('Game owner differs')
    entity_id = struct.unpack('<I', read(entity+0xa8, 4))[0]
    if entity_for_id(read, game, entity_id) != entity:
        raise ValueError('Entity registry owner differs')
    world = pointer(read, game+0x60410)
    pool = pointer(read, world+0x28)
    pool_count = struct.unpack('<I', read(world+0x30, 4))[0]
    if not 0 < pool_count <= 0x100000:
        raise ValueError('Physics pool bounds differ')
    entries, count = native_list(read, entity+0x268, 8, 4096)
    addresses = read(entries, count*8) if count else b''
    result = []
    for at in struct.unpack('<'+'Q'*count, addresses):
        table = pointer(read, at)-base
        if table not in BODY_TYPES:
            continue
        name, secondary = BODY_TYPES[table]
        identity = read(at+0x18, 8)
        generation, flags = struct.unpack('<iI', identity)
        if (generation == -1 or not component_ready(flags, 40, 0) or
                pointer(read, at+0x10) != entity or pointer(read, at+0xe8) != base+secondary):
            continue
        handle = struct.unpack('<I', read(at+0xf8, 4))[0]
        index = handle & 0xffffff
        if index == 0xffffff or index >= pool_count:
            continue
        record = pool+index*0xb0
        raw = read(record, 0xb0)
        if struct.unpack_from('<I', raw, 0x70)[0] != handle:
            continue
        transform = decode_transform(raw[:64])
        body_flags = struct.unpack_from('<I', raw, 0x44)[0]
        mode = struct.unpack('<B', read(at+0xd2, 1))[0]
        item = dict(entity=entity, entity_id=entity_id, component=at, generation=generation,
                    type=name, handle=handle, record=record, body_flags=body_flags,
                    temporary_activation_state=mode, **transform)
        if name == 'throwable':
            item['throwable_flags'] = struct.unpack('<I', read(at+0x438, 4))[0]
        if (read(at+0x18, 8) != identity or pointer(read, at+0x10) != entity or
                struct.unpack('<I', read(at+0xf8, 4))[0] != handle or
                struct.unpack('<I', read(record+0x70, 4))[0] != handle):
            continue
        result.append(item)
    if (entity_for_id(read, game, entity_id) != entity or
            native_list(read, entity+0x268, 8, 4096) != (entries, count) or
            (read(entries, count*8) if count else b'') != addresses):
        raise ValueError('Entity component lifetime changed')
    return result


def nearby_bodies(read, base, radius=4):
    if not math.isfinite(radius) or not 0 < radius <= 20:
        raise ValueError('Bounded inspection radius required')
    game = pointer(read, base+0x39a5290)
    if pointer(read, game) != base+0x1c940a8:
        raise ValueError('Game owner differs')
    player = pointer(read, game+0x3130b0)
    if pointer(read, player) != base+0x1c5e428:
        raise ValueError('Player owner differs')
    origin = struct.unpack('<3f', read(player+0x300, 12))
    if not all(math.isfinite(v) and abs(v) < 1e7 for v in origin):
        raise ValueError('Player position differs')
    result = []; rejected = 0
    for start in range(0, 0x3fff, 2048):
        size = min(2048, 0x3fff-start)
        entities = struct.unpack('<'+'Q'*size, read(game+0x3130b0+start*8, size*8))
        for index, entity in enumerate(entities, start):
            if entity == player or not entity:
                continue
            try:
                entity_id = struct.unpack('<I', read(entity+0xa8, 4))[0]
                if entity_id >> 16 != index or entity_id == INVALID_ENTITY:
                    continue
                pos = struct.unpack('<3f', read(entity+0x300, 12))
                distance = math.dist(pos, origin)
                if not math.isfinite(distance) or distance > radius:
                    continue
                for body in rigid_bodies(read, base, entity):
                    body['distance_from_player'] = distance
                    result.append(body)
            except (ValueError, OSError):
                rejected += 1
    if pointer(read, game+0x3130b0) != player:
        raise ValueError('Player changed during inspection')
    return dict(origin=list(origin), radius=radius, bodies=sorted(result, key=lambda b:b['distance_from_player']),
                rejected_entities=rejected, game_memory_writes=False, engine_calls=False)

"""Read-only Iggy input ownership from the pinned executable's 0x033740 dispatcher."""
import math
import struct
from rig_fields import native_list, pointer


def native_menu_snapshot(read, base):
    root = pointer(read, base+0x2274270)
    manager = root+0x18
    callback = pointer(read, manager+0x828)
    identity = struct.unpack('<3Q', read(callback, 24))
    if identity != (base+0x1abe800, manager, base+0x32d40):
        raise ValueError('Iggy input callback ownership differs')
    batches, batch_count = native_list(read, manager+8, 32, 1024)
    files = []
    batch = None
    if batch_count:
        batch = batches+(batch_count-1)*32
        wrappers, count = native_list(read, batch+0x10, 0xde8, 64)
        for i in range(count):
            wrapper = wrappers+i*0xde8
            file = pointer(read, wrapper)
            player = pointer(read, file+0x1618)
            cursor_bytes = read(wrapper+0xb08, 8)
            cursor = struct.unpack('<2f', cursor_bytes)
            cursor_reset = struct.unpack('<2I',cursor_bytes)==(0x7149f2ca,0x7149f2ca)
            if not cursor_reset and not all(math.isfinite(v) and abs(v)<1e6 for v in cursor):
                raise ValueError('Iggy cursor cache is invalid: '+','.join(hex(v) for v in struct.unpack('<2I',cursor_bytes)))
            member_data, member_count = native_list(read, wrapper+0x878, 0x28, 16)
            files.append(dict(wrapper=wrapper, file=file, iggy_player=player,
                              cached_stage_cursor=list(cursor), cursor_cache_reset=cursor_reset, cursor_member_data=member_data,
                              cursor_member_count=member_count))
        if native_list(read, batch+0x10, 0xde8, 64) != (wrappers, count):
            raise ValueError('Iggy file list changed while sampling')
        for item in files:
            if pointer(read, item['wrapper']) != item['file'] or pointer(read, item['file']+0x1618) != item['iggy_player']:
                raise ValueError('Iggy file identity changed while sampling')
    if pointer(read, base+0x2274270) != root or pointer(read, manager+0x828) != callback or \
            struct.unpack('<3Q', read(callback, 24)) != identity or \
            native_list(read, manager+8, 32, 1024) != (batches, batch_count):
        raise ValueError('Iggy input owner changed while sampling')
    return dict(root=root, manager=manager, callback=callback, batch_count=batch_count,
                files=files, owned_callback_verified=True, read_consistent=True,
                engine_methods_invoked=False, game_memory_writes=False,
                pointer_hit_testing_verified=False)

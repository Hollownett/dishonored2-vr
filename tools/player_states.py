"""Read-only: list the player state machine's states (pool index, RTTI class, active flag).

Usage: python tools/player_states.py --pid <pid> [--watch seconds]
--watch prints active-state changes (e.g. while jumping or performing a takedown).
"""
import argparse
import json
import struct
import time
from scan_body_material import attach, try_read
from rtti_fields import rtti_name
from rig_fields import pointer, native_list

FSM_VTABLE = 0x1c73d58


def machine_of(read, base):
    game = pointer(read, base + 0x39a5290)
    player = pointer(read, game + 0x3130b0)
    data, count = native_list(read, player + 0x268, 8, 4096)
    for at in struct.unpack('<' + 'Q' * count, read(data, count * 8)):
        if pointer(read, at) == base + FSM_VTABLE and pointer(read, at + 0x10) == player:
            return at
    raise ValueError('player state machine not found')


def states(read, base, machine):
    pool = pointer(read, machine + 0x90)
    out = []
    for i in range(160):
        raw = try_read(read, pool + 0x10 + i * 8, 8)
        s = raw and struct.unpack('<Q', raw)[0]
        if not s or not 0x10000 < s < 0x7fff00000000:
            continue
        head = try_read(read, s, 0x28)
        if not head or struct.unpack_from('<I', head, 8)[0] != i or struct.unpack_from('<Q', head, 0x10)[0] != machine:
            continue
        name = (rtti_name(read, base, s) or '?').replace('.?AV', '').rstrip('@')
        out.append(dict(index=i, name=name, active=head[0x20], vtable=hex(struct.unpack_from('<Q', head, 0)[0] - base)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--watch', type=float, default=0)
    a = ap.parse_args()
    base, read = attach(a.pid)
    machine = machine_of(read, base)
    table = states(read, base, machine)
    print(json.dumps(table, indent=1))
    last = None
    end = time.time() + a.watch
    while time.time() < end:
        active = [s['name'] for s in states(read, base, machine) if s['active'] == 1]
        if active != last:
            print('%.2f' % (a.watch - (end - time.time())), active)
            last = active
        time.sleep(.02)


if __name__ == '__main__':
    main()

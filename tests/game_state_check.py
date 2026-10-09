"""Synthetic state objects: no game process or writes."""
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from game_state import game_logic_mask

base, game = 0x140000000, 0x200000000
manager = game+0x1f6ec8
memory = {base+0x39a5290: struct.pack('<Q', game), game: struct.pack('<Q', base+0x1c940a8),
          game+0x341058: struct.pack('<i', 3), manager: bytes(0x8b8)}
def read(address, size):
    data = memory.get(address)
    if data is None or len(data) != size:
        raise OSError('Unreadable synthetic address')
    return data
def states(*active):
    entries = bytearray(0x8b8)
    for state in range(1, 17):
        address = 0x300000000+state*0x100
        data = bytearray(0x41)
        struct.pack_into('<Q', data, 8, manager)
        struct.pack_into('<i', data, 0x18, state)
        data[0x40] = int(state in active)
        memory[address] = bytes(data)
        struct.pack_into('<Q', entries, state*8, address)
    memory[manager] = bytes(entries)
states(1)
assert game_logic_mask(read, base) == 2
states(2)
assert game_logic_mask(read, base) == 4
states(1, 7)
assert game_logic_mask(read, base) == 130  # Journal over gameplay must suppress tracking.
memory[game+0x341058] = struct.pack('<i', 2)
assert game_logic_mask(read, base) == 0
memory[game+0x341058] = struct.pack('<i', 3)
memory[game] = struct.pack('<Q', base+1)
assert game_logic_mask(read, base) == 0
print('PASS: gameplay, pause, overlay, loading and stale game-object rejection')

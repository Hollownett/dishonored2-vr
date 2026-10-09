"""Exact-build, read-only game-logic state decoding (MIT profile; see notices)."""
import struct


def game_logic_mask(read, base):
    """Return active logic-state bits, or zero if the native state cannot be verified."""
    try:
        game = struct.unpack('<Q', read(base+0x39a5290, 8))[0]
        if not game or struct.unpack('<Q', read(game, 8))[0] != base+0x1c940a8:
            return 0
        if struct.unpack('<i', read(game+0x341058, 4))[0] != 3:
            return 0
        manager = game+0x1f6ec8
        entries = struct.unpack('<'+'Q'*(0x8b8//8), read(manager, 0x8b8))
        mask = 0
        for entry in set(entries)-{0}:
            try:
                data = read(entry, 0x41)
            except (OSError, ValueError):
                continue
            backref = struct.unpack_from('<Q', data, 8)[0]
            state = struct.unpack_from('<i', data, 0x18)[0]
            if backref == manager and 0 < state < 17 and data[0x40] == 1:
                mask |= 1 << state
        return mask
    except (OSError, ValueError, struct.error):
        return 0

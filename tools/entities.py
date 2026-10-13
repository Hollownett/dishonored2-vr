"""Read-only: list live idGameLocal entities (class, name, world position, distance to player).

Usage: python tools/entities.py --pid P [--kind npc|item|all] [--near X Y Z --radius R] [--json] [--limit N] [--health]
Layout evidence: docs/entities.md.
"""
import argparse
import json
import math
import re
import struct
from scan_body_material import attach, try_read
from rtti_fields import rtti_name
from rig_fields import pointer, native_list

GAME_PTR = 0x39a5290        # *(base+GAME_PTR) = idGameLocal (vtable base+0x1c940a8)
ENTITIES = 0x3130b0         # idGameLocal::entities[] (idEntity*), slot 0 = player1
ENTITIES_END = 0x340000     # scan bound (last non-null slot seen at ~0x337128)
PLAYER = 0x3130b0           # same slot 0
E_NAME, E_FLAGS, E_POS, E_COMPS, E_DECL = 0x20, 0xd0, 0x78, 0x268, 0x2e8
REF_POS = 0x30              # arkCpntReferenceFrame(_Player) world origin (float x3)
# arkCpntHealthEvolved (vtable base+0x1c51588), see docs/entities.md "NPC health": component+0x20 float current health,
# *(component+8)+0x68 float max health, component+0x70 aux ptr (0 = raw value is exact); entity+0xd1 bit 0x08 dead, 0x20 unconscious?
H_DECL, H_CUR, H_AUX, D_MAX, E_STATE = 0x08, 0x20, 0x70, 0x68, 0xd1
S_DEAD, S_UNCON = 0x08, 0x20
ITEM_NAME = re.compile(r'pickup|loot|food|coin|bottle|bread|apple|fruit|book|note|potion|ammo', re.I)


class World:
    def __init__(self, read, base, health=False):
        self.read, self.base, self.cache, self.health = read, base, {}, health

    def cls(self, obj):
        head = try_read(self.read, obj, 8)
        if not head:
            return None
        vt = struct.unpack('<Q', head)[0]
        if vt not in self.cache:
            n = rtti_name(self.read, self.base, obj)
            self.cache[vt] = n[4:-2] if n and n.startswith('.?AV') else n
        return self.cache[vt]

    def string(self, ptr, n=96):
        b = try_read(self.read, ptr, n) if 0x10000 <= ptr < 0x7fff00000000 else None
        return b.split(b'\0')[0].decode('latin1') if b else ''

    def components(self, ent):
        try:
            data, count = native_list(self.read, ent + E_COMPS, 8, 4096)
            return [(q, self.cls(q) or '?') for q in struct.unpack('<%dQ' % count, self.read(data, count * 8))] if count else []
        except (ValueError, OSError):
            return []

    def health_of(self, ent, comps):
        for q, n in comps:
            if n == 'arkCpntHealthEvolved':
                decl, aux = struct.unpack('<QQ', self.read(q + H_DECL, 8) + self.read(q + H_AUX, 8))
                cur, mx = struct.unpack('<f', self.read(q + H_CUR, 4))[0], struct.unpack('<f', self.read(decl + D_MAX, 4))[0]
                st = self.read(ent + E_STATE, 1)[0]
                return dict(comp=hex(q), cur=round(cur, 3), max=round(mx, 3), dead=bool(st & S_DEAD), uncon=bool(st & S_UNCON), aux=aux != 0)
        return None

    def entity(self, slot, ent, full):
        cls = self.cls(ent)
        if not cls:
            return None
        name = self.string(struct.unpack('<Q', self.read(ent + E_NAME, 8))[0])
        comps = None
        if full or cls in ('arkNPC', 'arkPickup', 'arkMovable') or cls == 'arkPlayer':
            comps = self.components(ent)
        pos = frame = None
        for q, n in comps or []:
            if n.startswith('arkCpntReferenceFrame'):
                pos, frame = struct.unpack('<3f', self.read(q + REF_POS, 12)), q
                break
        src = 'frame'
        if pos is None:
            pos, src = struct.unpack('<3f', self.read(ent + E_POS, 12)), 'ent+0x78'
        e = dict(slot=slot, addr=hex(ent), cls=cls, name=name, pos=[round(v, 2) for v in pos], pos_src=src)
        if frame:
            e['frame'] = hex(frame)
        if comps is not None:
            cn = [n for _, n in comps]
            e['pickable'] = 'arkCpntPickable_Simple' in cn
            e['loot'] = 'arkCpntLoot' in cn
        if cls == 'arkNPC':
            # ponytail: guard/civilian by name+flag bit 0x1000 only; alive/awareness not decoded (see docs)
            e['flags'] = hex(struct.unpack('<I', self.read(ent + E_FLAGS, 4))[0])
            e['guard'] = 'guard' in name
            e['dead'] = 'human_dead' in name  # corpse-def spawn name; with --health use e['health']['dead'] (entity+0xd1 bit 8)
            if self.health:
                e['health'] = self.health_of(ent, comps)
        else:
            decl = try_read(self.read, struct.unpack('<Q', self.read(ent + E_DECL, 8))[0] + 0x48, 8)
            e['model'] = self.string(struct.unpack('<Q', decl)[0], 120) if decl else ''
        return e


def scan(read, base, kind, health=False):
    w = World(read, base, health)
    game = pointer(read, base + GAME_PTR)
    out = []
    size = ENTITIES_END - ENTITIES
    raw = b''.join(try_read(read, game + ENTITIES + o, min(0x4000, size - o)) or bytes(min(0x4000, size - o))
                   for o in range(0, size, 0x4000))
    for slot in range(size // 8):
        ent = struct.unpack_from('<Q', raw, slot * 8)[0]
        if not 0x10000 <= ent < 0x7fff00000000:
            continue
        head = try_read(read, ent, 8)
        if not head or not base <= struct.unpack('<Q', head)[0] < base + 0x41cb000:
            continue
        try:
            e = w.entity(slot, ent, kind == 'all')
        except (OSError, ValueError, struct.error):
            continue
        if not e:
            continue
        is_item = e.get('pickable') or e.get('loot') or e['cls'] == 'arkPickup' or bool(ITEM_NAME.search(e['name']))
        if (kind == 'npc' and e['cls'] != 'arkNPC') or (kind == 'item' and not (is_item and e['cls'] != 'arkNPC')):
            continue
        out.append(e)
    player = w.entity(0, pointer(read, game + PLAYER), True)
    return out, player


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--kind', choices=['npc', 'item', 'all'], default='all')
    ap.add_argument('--near', type=float, nargs=3, metavar=('X', 'Y', 'Z'))
    ap.add_argument('--radius', type=float)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--health', action='store_true', help='NPC current/max health + dead/unconscious flags')
    a = ap.parse_args()
    base, read = attach(a.pid)
    ents, player = scan(read, base, a.kind, a.health)
    origin = a.near or player['pos']
    for e in ents:
        e['dist'] = round(math.dist(e['pos'], origin), 2)
        e['player_dist'] = round(math.dist(e['pos'], player['pos']), 2)
    if a.radius is not None:
        ents = [e for e in ents if e['dist'] <= a.radius]
    ents.sort(key=lambda e: e['dist'])
    if a.limit:
        ents = ents[:a.limit]
    if a.json:
        print(json.dumps(dict(player=player, entities=ents), indent=1))
        return
    print('player', player['pos'])
    for e in ents:
        x, y, z = e['pos']
        h = e.get('health')
        print('%-14s %-34s (%8.2f,%8.2f,%8.2f) d=%7.2f %s%s' % (e['cls'], e['name'][-34:], x, y, z, e['dist'],
              ('DEAD ' if e.get('dead') else '') + ('guard' if e.get('guard') else 'civ') if e['cls'] == 'arkNPC' else e.get('model', '')[-40:],
              ' hp=%g/%g%s%s%s' % (h['cur'], h['max'], ' DEAD' if h['dead'] else '', ' UNCON?' if h['uncon'] else '', ' aux' if h['aux'] else '') if h else ''))


if __name__ == '__main__':
    main()

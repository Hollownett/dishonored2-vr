"""Summarise the mod's combat timeline (artifacts/native-combat.csv: tick,event,entity,value,extra) after a headset session.

    python tools/combat_report.py [csv]

Shows swings, blade/weapon contacts with their tip speeds (the contact-hit gate is 1.5 m/s), contact hits sent to the
engine, and how many NPC contacts were too slow to count: the numbers to tune the gate from real swings."""
import collections
import csv
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'artifacts', 'native-combat.csv')
    rows = [r for r in csv.reader(open(path)) if len(r) >= 4]
    counts = collections.Counter(r[1] for r in rows)
    print('events:', dict(counts))
    for kind in ('blade_npc', 'blade_world', 'gun_world', 'contact_hit'):
        speeds = sorted(float(r[3]) for r in rows if r[1] == kind)
        if speeds:
            q = lambda f: speeds[min(len(speeds) - 1, int(f * len(speeds)))]
            print(f'{kind:12s} n={len(speeds):4d} speed m/s p10={q(.1):.2f} p50={q(.5):.2f} p90={q(.9):.2f} max={speeds[-1]:.2f}')
    slow = sum(1 for r in rows if r[1] == 'blade_npc' and float(r[3]) < 1.5)
    print(f'NPC blade contacts under the 1.5 m/s gate: {slow}')
    swings = [int(r[0]) for r in rows if r[1] in ('slash', 'thrust', 'overhead')]
    hits = [int(r[0]) for r in rows if r[1] == 'contact_hit']
    lead = [min((h - s for h in hits if 0 <= h - s <= 600), default=None) for s in swings]
    lead = [x for x in lead if x is not None]
    if lead:
        print(f'contact hit after swing detection: n={len(lead)} median {sorted(lead)[len(lead) // 2]} ms')


if __name__ == '__main__':
    main()

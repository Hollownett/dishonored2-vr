"""Merge helper-agent review proposals (artifacts/doto-lab/review/*.json) into tools/doto/offsets.json.

Each proposal keeps its evidence ('why') and gains 'source': 'review/<name>'. Shapes:
  sites  {file, contains, from, to, why}: one hex literal -> 'sites'; a hex string, byte run or decimal -> 'text'
  rvas   {file, contains, from, to, why} -> 'rvas' (site-scoped address override, any literal class)
  bytes  {file, d2_at, why, safe} -> 'bytes' (accepted semantic byte-check difference) when safe is not false
A proposal already present (same file/contains/from) is replaced. Prints what it merged and what it skipped.

Usage: python tools/doto/merge_review.py artifacts/doto-lab/review/input-ui.json [...]
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
HEX1 = re.compile(r'^0x[0-9a-fA-F]+$')


def main():
    path = os.path.join(HERE, 'offsets.json')
    with open(path, encoding='utf-8') as f:
        rules = json.load(f)
    for k in ('sites', 'rvas', 'bytes', 'text', 'lines'):
        rules.setdefault(k, [])
    inv = json.load(open(os.path.join(ROOT, 'artifacts', 'doto-lab', 'inventory.json')))
    byte_lines = {(r['file'], r['line']) for r in inv if r['cls'] == 'bytes'}
    src_cache = {}

    def line_no(file, contains):
        if file not in src_cache:
            with open(os.path.join(ROOT, file), encoding='utf-8', errors='replace', newline='') as f:
                src_cache[file] = f.read().split('\n')
        hits = [i + 1 for i, ln in enumerate(src_cache[file]) if contains in ln]
        return hits

    def put(kind, entry, key):
        lst = rules[kind]
        for i, e in enumerate(lst):
            if all(e.get(k) == entry.get(k) for k in key):
                lst[i] = entry
                return 'replaced'
        lst.append(entry)
        return 'added'

    for rp in sys.argv[1:]:
        name = os.path.splitext(os.path.basename(rp))[0]
        data = json.load(open(rp, encoding='utf-8'))
        src = f'review/{name}'
        for s in data.get('sites', []):
            hits = line_no(s['file'], s['contains'])
            if len(hits) != 1:
                print(f'SKIP site {s["file"]} {s["contains"][:50]!r}: {len(hits)} matching lines')
                continue
            frm, to = str(s['from']), str(s['to'])
            single = HEX1.match(frm) and HEX1.match(to)
            if single and (s['file'], hits[0]) not in byte_lines:
                put('sites', {'file': s['file'], 'contains': s['contains'], 'from': frm, 'to': to, 'why': s.get('why', ''), 'source': src},
                    ('file', 'contains', 'from'))
            else:
                put('text', {'file': s['file'], 'contains': s['contains'], 'old': frm, 'new': to, 'why': s.get('why', ''), 'source': src},
                    ('file', 'contains', 'old'))
        for r in data.get('rvas', []):
            if not r.get('file'):
                print(f'SKIP rva without file: {r}')
                continue
            hits = line_no(r['file'], r.get('contains', ''))
            if len(hits) < 1:
                print(f'SKIP rva {r["file"]} {r.get("contains", "")[:50]!r}: no matching line')
                continue
            put('rvas', {'file': r['file'], 'contains': r.get('contains', ''), 'from': r['from'], 'to': r['to'], 'why': r.get('why', ''), 'source': src},
                ('file', 'contains', 'from'))
        for b in data.get('bytes', []):
            if b.get('safe') is False:
                print(f'not accepting unsafe byte entry {b["file"]} {b["d2_at"]}')
                continue
            put('bytes', {'file': b['file'], 'd2_at': b['d2_at'], 'why': b.get('why', ''), 'source': src}, ('file', 'd2_at'))
        print(f'{src}: merged')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(rules, f, indent=1)
    print({k: len(v) for k, v in rules.items() if isinstance(v, list)})


if __name__ == '__main__':
    main()

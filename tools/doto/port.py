"""Generate the Death of the Outsider source tree from the Dishonored 2 mod sources.

The D2 sources stay the single source of truth. This writes build/doto-src/src/ with:
  * every image-relative address (code, vtables, imports, globals) translated to the DOTO build;
    an address that cannot be translated becomes a poison RVA (0xfff00000 + n*0x10), beyond the
    image, so the mod's own prologue / vtable / name checks refuse that feature instead of
    reading or hooking the wrong code;
  * struct field offsets that moved (unanimous aligned-instruction votes, or an entry in
    tools/doto/offsets.json) rewritten;
  * prologue / instruction byte checks re-read from Dishonored_DO.exe at the translated site
    when the only differences are rip-relative displacements or branch targets (anything else is
    left as is, so that check fails and the feature is skipped, and listed in the report);
  * the game identity (exe name, SHA-256, SizeOfImage) switched, and D2VR_GAME_DOTO defined.

Usage: python tools/doto/port.py [--out build/doto-src] [--report artifacts/doto-lab/port-report.json]
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from translate import Translator  # noqa: E402

D2_SHA = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
DOTO_SHA = 'be73d979fdb5a6eacbcd385944c6060b97c997d06ab9d72fb39f275a866b955d'
POISON0 = 0xfff00000

# literal text substitutions (file, old, new, required)
IDENTITY = [
    ('src/dxgi_probe.cpp', 'L"Dishonored2.exe"', 'L"Dishonored_DO.exe"', True),
    ('src/game_camera.hpp', D2_SHA, DOTO_SHA, True),
]
SIZE_OF_IMAGE = {0x41cb000: 0x431f000}
CONFIDENT = {'exact', 'aligned', 'voted', 'pattern'}


def load_offsets():
    path = os.path.join(HERE, 'offsets.json')
    if not os.path.exists(path):
        return {'global': {}, 'sites': []}
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    data.setdefault('global', {})
    data.setdefault('sites', [])
    data.setdefault('lines', [])
    data['_used'] = set()
    return data


def line_rule(rules, file, line_text, value):
    """offsets.json "lines": [{"file", "contains", "map": {"0x9c": "0x8c", ...}, "why"}]: every offset literal
    with a mapped value on a line containing the text. Used for uniformly shifted structs (renderView_t)."""
    for i, r in enumerate(rules.get('lines', [])):
        if r['file'] == file and r['contains'] in line_text:
            m = {int(k, 0): int(v, 0) for k, v in r['map'].items()}
            if value in m:
                rules['_used'].add(i)
                return m[value], r.get('why', 'offsets.json line rule')
    return None


def entity_rule(rules, file, line_text, col, value):
    """offsets.json "entity": an idEntity field read off a base identifier that holds an engine entity
    (player, entity, npc, ...): the proven idEntity shift for the listed field values."""
    e = rules.get('entity')
    if not e:
        return None
    m = {int(k, 0): int(v, 0) for k, v in e['map'].items()}
    if value not in m:
        return None
    b = re.search(r'([A-Za-z_]\w*)\s*\)?\s*\+\s*$', line_text[:col])
    if not b:
        return None
    base = b.group(1)
    if base in e['bases'] or base in e.get('file_bases', {}).get(file, []):
        return m[value], e['why']
    return None


def site_override(rules, file, line_text, value, col=None):
    lr = line_rule(rules, file, line_text, value)
    if lr:
        return lr
    for r in rules['sites']:
        if r['file'] == file and int(r['from'], 0) == value and (not r.get('contains') or r['contains'] in line_text):
            return int(r['to'], 0), r.get('why', 'offsets.json')
    g = rules['global'].get(f'{value:#x}')
    if g:
        return int(g['to'], 0), g.get('why', 'offsets.json global')
    if col is not None:
        return entity_rule(rules, file, line_text, col, value)
    return None


def site_bytes_accepted(rules, file, d2_rva, n=1):
    """offsets.json "bytes": [{"file":..., "d2_at":"0x...", "why":...}] accepts a semantic byte difference
    after a manual review of the hook (for example a changed stack frame the detour never reads)."""
    for r in rules.get('bytes', []):
        try:
            at = int(r['d2_at'], 0)
        except ValueError:
            continue
        if r['file'] == file and d2_rva <= at < d2_rva + n:
            return True
    return False


def file_context_votes(t, inv, src_lines):
    """Per literal: displacement d2 -> Counter(doto) over the aligned instructions of the engine functions
    named near it (address literals and addresses in comments): the whole file for files under 1500 lines,
    otherwise within +-80 lines. Returns {(file, line): {d2: Counter}} computed lazily via a closure."""
    import difflib
    import align
    align._init(t.a.path, t.b.path)
    refs = collections.defaultdict(list)   # file -> [(line, fn)]
    for r in inv:
        v = r['value']
        if r['cls'].startswith('rva-code') or (r['cls'] == 'comment' and v >= 0x10000):
            f = t.a.function_of(v)
            if f is not None and f in t.fn:
                refs[r['file']].append((r['line'], f))
    per_fn = {}

    def pairs_of(f):
        if f not in per_fn:
            g = t.fn[f]
            ta = align.tokens(align._A, f, t.a.fns[f][1])
            tb = align.tokens(align._B, g, t.b.fns[g][1])
            sa, sb = [x[0] for x in ta], [x[0] for x in tb]
            blocks = [(0, 0, len(sa))] if sa == sb else difflib.SequenceMatcher(None, sa, sb, autojunk=False).get_matching_blocks()
            c = collections.defaultdict(collections.Counter)
            for i, j, n in blocks:
                for k in range(n):
                    for p, q in zip(ta[i + k][2], tb[j + k][2]):
                        c[p][q] += 1
            per_fn[f] = c
        return per_fn[f]

    def votes(file, line, d):
        big = len(src_lines[file]) >= 1500
        fns = {f for ln, f in refs.get(file, []) if not big or abs(ln - line) <= 80}
        out = collections.Counter()
        for f in fns:
            out.update(pairs_of(f).get(d, {}))
        return out
    return votes


def byte_groups(rows, src_lines=None):
    """Group 'bytes' literals into arrays: adjacent literals separated only by commas, spaces, std::byte{ } and line
    breaks (a multi-line initializer is one array)."""
    import re as _re
    sep = _re.compile(r'^[\s,{}]*(?:std::byte\s*\{)?[\s,{}]*$')
    groups = []
    cur = []

    def between(a, b):
        if src_lines is None:
            return None
        lines = src_lines[a['file']]
        if a['line'] == b['line']:
            return lines[a['line'] - 1][a['col'] + len(a['text']):b['col']]
        if b['line'] != a['line'] + 1:
            return None
        return lines[a['line'] - 1][a['col'] + len(a['text']):] + '\n' + lines[b['line'] - 1][:b['col']]

    for r in rows:
        if r['cls'] != 'bytes':
            if cur:
                groups.append(cur)
                cur = []
            continue
        if cur:
            gap = between(cur[-1], r) if cur[-1]['file'] == r['file'] else None
            joined = gap is not None and all(sep.match(part) for part in gap.replace('std::byte{', ' ').split('\n'))
            if joined:
                # inside one array every '}' closes a std::byte{ ; any other brace is an array boundary
                plain = gap.replace('std::byte{', '')
                if '{' in plain or plain.count('}') > gap.count('std::byte{'):
                    joined = False
            if not joined:
                groups.append(cur)
                cur = []
        cur.append(r)
    if cur:
        groups.append(cur)
    return [g for g in groups if len(g) >= 4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'doto-src'))
    ap.add_argument('--report', default=os.path.join(ROOT, 'artifacts', 'doto-lab', 'port-report.json'))
    args = ap.parse_args()

    inv_path = os.path.join(ROOT, 'artifacts', 'doto-lab', 'inventory.json')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'inventory.py'), os.path.join(ROOT, 'artifacts', 'doto-lab', 'd2.idx')],
                          stdout=open(inv_path, 'w'))
    with open(inv_path) as f:
        inv = json.load(f)
    t = Translator()
    rules = load_offsets()

    files = collections.defaultdict(list)
    for r in inv:
        files[r['file']].append(r)
    src_lines = {}
    for name in files:
        with open(os.path.join(ROOT, name), encoding='utf-8', errors='surrogateescape', newline='') as f:
            src_lines[name] = f.read().split('\n')

    edits = collections.defaultdict(list)   # file -> [(line, col, old, new)]
    report = {'rva': [], 'poison': [], 'offset_moved': [], 'offset_review': [], 'bytes': [], 'identity': []}
    poison_ids = {}
    cache = {}

    def tr(v):
        if v not in cache:
            cache[v] = t.rva(v)
        return cache[v]

    # ---- code addresses the inventory saw as plain numbers: pin-table entries {0xRVA,N,{bytes}} / {0xRVA,N,0xHASHull}
    # and caller/return-address tables (another code address on the same line), when the value is exactly a D2
    # instruction start inside a function ----
    from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    _md = Cs(CS_ARCH_X86, CS_MODE_64)
    _starts = {}

    def insn_start(x):
        f = t.a.function_of(x)
        if f is None:
            # a leaf without unwind info: accept when its start maps (call votes / pattern)
            y, how, _ = t.code_rva(x)
            return y is not None and how in ('voted', 'pattern', 'aligned', 'exact')
        if f not in _starts:
            _starts[f] = {i.address for i in _md.disasm(t.a.bytes(f, t.a.fns[f][1] - f), f)}
        return x in _starts[f]
    code_lines = {(r['file'], r['line']) for r in inv if r['cls'].startswith('rva-code')}
    pin_after = re.compile(r'\s*,\s*\d+\s*,\s*(?:\{|0x[0-9a-fA-F]+u?ll\})')
    promoted = 0
    for r in inv:
        if r['cls'] != 'offset' or not (0x1000 <= r['value'] < t.a.sections['.text'][1]):
            continue
        line = src_lines[r['file']][r['line'] - 1]
        before = line[:r['col']]
        after = line[r['col'] + len(r['text']):]
        if re.search(r'[\w\]\)&|]\s*[+&|^]\s*$|[<>]=?\s*$', before):
            continue   # a field offset, a mask or a bound
        if not (pin_after.match(after) or (r['file'], r['line']) in code_lines):
            continue
        if insn_start(r['value']):
            r['cls'] = 'rva-code'
            promoted += 1
    print(f'promoted {promoted} table literals to code addresses')

    # ---- reviewed per-site address overrides (any literal class but comments and bytes) ----
    handled = set()
    for r in inv:
        if r['cls'] in ('comment', 'bytes'):
            continue
        line = src_lines[r['file']][r['line'] - 1]
        o = site_override(rules, r['file'], line, r['value'], r['col'])
        if o and r['cls'] != 'offset':   # a reviewed offset rule wins over an address classification
            new = f'{o[0]:#x}'
            if new.lower() != r['text'].lower():
                edits[r['file']].append((r['line'], r['col'], r['text'], new))
            report['offset_moved'].append({**r, 'to': new, 'why': o[1]})
            handled.add((r['file'], r['line'], r['col']))
            continue
        for o in rules.get('rvas', []):
            if not o.get('file') or o['file'] != r['file']:
                continue
            if o.get('contains') and o['contains'] not in line:
                continue
            if int(o['from'], 0) == r['value']:
                new = o['to']
                if new.lower() != r['text'].lower():
                    edits[r['file']].append((r['line'], r['col'], r['text'], new))
                report['rva'].append({**r, 'to': new, 'how': 'manual', 'why': o.get('why', '')})
                handled.add((r['file'], r['line'], r['col']))
                o['_used'] = True
                break

    # ---- addresses ----
    for r in inv:
        cls = r['cls']
        if not cls.startswith('rva') or (r['file'], r['line'], r['col']) in handled:
            continue
        v = r['value']
        manual = {int(o['from'], 0): (int(o['to'], 0), o['why']) for o in rules.get('rvas', []) if not o.get('file')}
        if v in manual:
            y, how, why = manual[v][0], 'manual', manual[v][1]
        else:
            y, how, why = tr(v)
        if y is not None and how in CONFIDENT | {'near', 'manual'}:
            new = f'{y:#x}'
            report['rva'].append({**r, 'to': new, 'how': how, 'why': why})
        else:
            if v not in poison_ids:
                poison_ids[v] = POISON0 + 0x10 * len(poison_ids)
            new = f'{poison_ids[v]:#x}'
            report['poison'].append({**r, 'to': new, 'why': why})
        if new.lower() != r['text'].lower():
            edits[r['file']].append((r['line'], r['col'], r['text'], new))

    # ---- offsets ----
    file_votes = file_context_votes(t, inv, src_lines)
    for r in inv:
        if r['cls'] != 'offset' or (r['file'], r['line'], r['col']) in handled:
            continue
        v = r['value']
        line = src_lines[r['file']][r['line'] - 1]
        fv = file_votes(r['file'], r['line'], v) if v >= 0x10 else None
        if fv and not site_override(rules, r['file'], line, v, r['col']):
            y, n = max(fv.items(), key=lambda kv: kv[1])
            tot = sum(fv.values())
            if n / tot >= 0.9 and (n >= 2 or v >= 0x400 or y == v):
                if y != v:
                    edits[r['file']].append((r['line'], r['col'], r['text'], f'{y:#x}' if y >= 0 else f'-{-y:#x}'))
                    report['offset_moved'].append({**r, 'to': f'{y:#x}', 'why': f'file context {n}/{tot}'})
                continue
            report['offset_review'].append({**r, 'kind': 'file-mixed', 'votes': {f'{k:#x}': c for k, c in fv.items()}})
            continue
        if v in SIZE_OF_IMAGE:
            edits[r['file']].append((r['line'], r['col'], r['text'], f'{SIZE_OF_IMAGE[v]:#x}'))
            report['offset_moved'].append({**r, 'to': f'{SIZE_OF_IMAGE[v]:#x}', 'why': 'SizeOfImage'})
            continue
        o = site_override(rules, r['file'], line, v, r['col'])
        if o:
            if o[0] != v:
                edits[r['file']].append((r['line'], r['col'], r['text'], f'{o[0]:#x}'))
            report['offset_moved'].append({**r, 'to': f'{o[0]:#x}', 'why': o[1]})
            continue
        y, kind, why = t.offset(v)
        if kind == 'moved' and v >= 0x400:
            total = sum(t.disp[v].values())
            if total >= 2:
                edits[r['file']].append((r['line'], r['col'], r['text'], f'{y:#x}'))
                report['offset_moved'].append({**r, 'to': f'{y:#x}', 'why': why})
                continue
        if kind in ('moved', 'mixed') or (kind == 'none' and v >= 0x1000):
            report['offset_review'].append({**r, 'kind': kind, 'votes': why})

    # ---- byte checks ----
    rva_rows = [r for r in inv if r['cls'].startswith('rva-code')]
    by_file_rva = collections.defaultdict(list)
    for r in rva_rows:
        by_file_rva[r['file']].append(r)
    text_lo, text_hi = t.a.sections['.text']
    d2_text = t.a.mem[text_lo:text_hi]
    for g in byte_groups(inv, src_lines):
        bs = bytes(r['value'] for r in g)
        file, line = g[0]['file'], g[0]['line']
        loc = None
        cands = [r['value'] for r in by_file_rva[file] if abs(r['line'] - line) <= 12]
        hits = set()
        for c in cands:
            window = t.a.mem[max(c - 8, 0):c + 384]
            i = window.find(bs)
            while i >= 0:
                hits.add(max(c - 8, 0) + i)
                i = window.find(bs, i + 1)
        if len(hits) == 1:
            loc = hits.pop()
            how_found = 'near literal'
        else:
            i = d2_text.find(bs)
            if i >= 0 and d2_text.find(bs, i + 1) < 0:
                loc = text_lo + i
                how_found = 'unique in .text'
        entry = {'file': file, 'line': line, 'col': g[0]['col'], 'n': len(bs), 'd2': bs.hex()}
        if loc is None:
            entry['status'] = 'not-located'
            report['bytes'].append(entry)
            continue
        y, how, why = t.code_rva(loc)
        entry.update({'d2_at': f'{loc:#x}', 'found': how_found})
        if y is None:
            entry['status'] = 'site-unmapped'
            report['bytes'].append(entry)
            continue
        cb = t.b.bytes(y, len(bs))
        entry.update({'doto_at': f'{y:#x}', 'doto': cb.hex(), 'how': how})
        status, notes = t.compare_bytes(loc, y, len(bs))
        entry['status'] = status
        entry['notes'] = notes
        accepted = status in ('relocated', 'layout') or site_bytes_accepted(rules, file, loc, len(bs))
        if status != 'same' and accepted:
            entry['rewritten'] = True
            for r, nb in zip(g, cb):
                new = f'0x{nb:02x}'
                if r['text'].lower() != new:
                    # keep the literal's own spelling style (0x8B vs 0x8b)
                    if r['text'][2:].isupper():
                        new = '0x' + new[2:].upper()
                    edits[file].append((r['line'], r['col'], r['text'], new))
        report['bytes'].append(entry)

    # ---- write ----
    out_src = os.path.join(args.out, 'src')
    if os.path.exists(out_src):
        shutil.rmtree(out_src)
    shutil.copytree(os.path.join(ROOT, 'src'), out_src)
    original = {f: lines[:] for f, lines in src_lines.items()}
    for file, es in edits.items():
        lines = src_lines[file][:]
        for ln, col, old, new in sorted(es, key=lambda e: (e[0], -e[1])):
            s = lines[ln - 1]
            if s[col:col + len(old)] != old:
                raise SystemExit(f'{file}:{ln}:{col} expected {old!r}, found {s[col:col + len(old)]!r}')
            lines[ln - 1] = s[:col] + new + s[col + len(old):]
        src_lines[file] = lines
    # FNV-1a code pins {rva,size,0xHASHull}: re-hash DOTO's bytes when they differ only by relocations / voted layout moves
    pin = re.compile(r'\{(0x[0-9a-fA-F]+),(\d+),(0x[0-9a-fA-F]+)ull\}')

    def fnv(data):
        h = 14695981039346656037
        for c in data:
            h = ((h ^ c) * 1099511628211) & 0xffffffffffffffff
        return h
    report['hash_pins'] = []
    for file in list(original):
        for i, ln in enumerate(original[file]):
            for m in pin.finditer(ln):
                rva, size, h = int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)
                if fnv(t.a.bytes(rva, size)) != h:
                    continue   # not a D2 code-hash pin
                y, how, why = t.rva(rva)
                entry = {'file': file, 'line': i + 1, 'd2': f'{rva:#x}', 'size': size}
                if y is None:
                    entry['status'] = 'unmapped'
                else:
                    status, notes = t.compare_bytes(rva, y, size)
                    entry.update({'doto': f'{y:#x}', 'status': status, 'notes': notes})
                    if status in ('same', 'relocated', 'layout'):
                        new = f'{fnv(t.b.bytes(y, size)):#018x}'
                        if m.group(3) in src_lines[file][i] and new != m.group(3):
                            src_lines[file][i] = src_lines[file][i].replace(m.group(3) + 'ull', new + 'ull')
                            entry['rehashed'] = True
                report['hash_pins'].append(entry)
    # reviewed raw text rules (hex strings, decimal sizes, byte runs): the line is found in the D2 source by 'contains',
    # then 'old' -> 'new' on the generated line (after the literal edits above)
    for t_rule in rules.get('text', []):
        file = t_rule['file']
        if file not in src_lines:
            with open(os.path.join(ROOT, file), encoding='utf-8', errors='surrogateescape', newline='') as f:
                src_lines[file] = f.read().split('\n')
            original[file] = src_lines[file][:]
            files[file] = []
        hits = [i for i, ln in enumerate(original[file]) if t_rule['contains'] in ln]
        done = 0
        for i in hits:
            if t_rule['old'] in src_lines[file][i]:
                src_lines[file][i] = src_lines[file][i].replace(t_rule['old'], t_rule['new'])
                done += 1
        if not done:
            print(f"WARNING text rule matched nothing: {file} {t_rule['contains'][:60]!r}")
        report.setdefault('text', []).append({'file': file, 'lines': [i + 1 for i in hits], 'applied': done, 'why': t_rule.get('why', '')})
    for o in rules.get('rvas', []):
        if o.get('file') and not o.get('_used'):
            print(f"WARNING unused rva rule: {o['file']} {o.get('contains', '')[:60]!r} {o['from']}")
    for file in files:
        with open(os.path.join(args.out, file), 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
            f.write('\n'.join(src_lines[file]))
    for file, old, new, required in IDENTITY:
        path = os.path.join(args.out, file)
        with open(path, encoding='utf-8', errors='surrogateescape', newline='') as f:
            s = f.read()
        n = s.count(old)
        if required and n == 0:
            raise SystemExit(f'identity rule found nothing: {file} {old}')
        with open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
            f.write(s.replace(old, new))
        report['identity'].append({'file': file, 'old': old, 'new': new, 'count': n})
    # the game switch: one forced include so headers can test D2VR_GAME_DOTO
    with open(os.path.join(out_src, 'game_doto.hpp'), 'w', newline='\n') as f:
        f.write('#pragma once\n// Generated by tools/doto/port.py: this tree is the Death of the Outsider build.\n'
                '#define D2VR_GAME_DOTO 1\n'
                f'#define D2VR_DOTO_POISON_BASE {POISON0:#x}u\n')
    with open(os.path.join(args.out, 'poison.json'), 'w') as f:
        json.dump({f'{v:#x}': f'{p:#x}' for v, p in poison_ids.items()}, f, indent=1)
    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w') as f:
        json.dump(report, f, indent=1)
    unused = [r for i, r in enumerate(rules['lines']) if i not in rules['_used']]
    for r in unused:
        print(f"WARNING unused line rule: {r['file']} {r['contains']!r}")
    bc = collections.Counter(e['status'] for e in report['bytes'])
    hw = collections.Counter(e['how'] for e in report['rva'])
    print(f"addresses: {len(report['rva'])} translated {dict(hw)}, {len(report['poison'])} poisoned "
          f"({len(poison_ids)} unique)")
    print(f"offsets: {len(report['offset_moved'])} rewritten, {len(report['offset_review'])} to review")
    print(f'byte checks: {dict(bc)}')
    print(f"files edited: {len(edits)} -> {out_src}")


if __name__ == '__main__':
    main()

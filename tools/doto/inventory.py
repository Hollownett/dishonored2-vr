"""List every hex literal in the mod's C++ sources and classify it against the pinned Dishonored2.exe.

Classes:
  rva-code     an RVA inside .text   (function begin or instruction)
  rva-rdata    an RVA inside .rdata  (vtables, strings, constant tables)
  rva-data     an RVA inside .data   (globals)
  offset       a struct field offset or other number (not translated by RVA)
  bytes        part of a byte-array literal (prologue / instruction check)
  comment      inside a // or /* */ comment (left alone)

The decision uses the text around the literal first (base+X, *_rva=X, typed(...,X), caller==X, vtable names)
and the value second (it must land in the right section, and for code on an instruction or function begin).

Usage: python tools/doto/inventory.py artifacts/doto-lab/d2.idx > artifacts/doto-lab/inventory.json
"""
import bisect
import json
import os
import pickle
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEX = re.compile(r'0x[0-9a-fA-F]+')

# contexts that make a literal an image-relative address
RVA_BEFORE = re.compile(
    r'(?:\b(?:image_)?base_?|\bexe|\bmodule_base|\bimage|\bown_base|\bgame_base|\bbase\(\))\s*\+\s*$'
    r'|\b\w*(?:rva|Rva|RVA|vtable|Vtable|vtbl|_fn|_function|_hook|_site|_call|_caller|_entry|_target|_global|_table|_slot|_ret|_return|_code)\w*\s*[=:]\s*\{?\s*$'
    r'|\b\w*(?:rvas|Rvas|vtables|callers|sites|functions)\w*\s*(?:\[[^\]]*\])?\s*[{=]\s*\{?\s*(?:0x[0-9a-fA-F]+u?l*\s*,\s*)*$'
    r'|\b(?:caller|relative|ret|return_rva|rip|callsite|call_site|target|site|vtable|vt|vtbl|entry)\s*(?:==|!=|<|>|<=|>=)\s*$'
    r'|\(\s*(?:image_)?base_?\s*,\s*$'
    r'|\btyped\s*\([^()]*,\s*$'
    r'|\btyped\s*\((?:[^()]|\([^()]*\))*,\s*$'
)
OFFSET_BEFORE = re.compile(r'(?:\*\s*\w+|\w+\s*\.\s*\w+|\w+->\w+|\]|\))\s*\+\s*$|\+\s*$')


def strip_comments(lines):
    """Yield (line_no, code_part_mask) where mask[i] is True if char i is code (not comment / string)."""
    in_block = False
    for n, line in enumerate(lines, 1):
        mask = [True] * len(line)
        i = 0
        in_str = None
        while i < len(line):
            c = line[i]
            if in_block:
                mask[i] = False
                if line.startswith('*/', i):
                    mask[i + 1] = False if i + 1 < len(line) else None
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_str:
                mask[i] = False
                if c == '\\':
                    if i + 1 < len(line):
                        mask[i + 1] = False
                    i += 2
                    continue
                if c == in_str:
                    in_str = None
                i += 1
                continue
            if line.startswith('//', i):
                for j in range(i, len(line)):
                    mask[j] = False
                break
            if line.startswith('/*', i):
                in_block = True
                mask[i] = False
                i += 1
                continue
            if c in '"\'':
                # raw strings are rare in this code base; treat as simple strings
                in_str = c
                mask[i] = False
            i += 1
        yield n, line, mask


class Exe:
    def __init__(self, idx_path):
        with open(idx_path, 'rb') as f:
            idx = pickle.load(f)
        self.sections = {s[0]: (s[1], s[1] + s[2]) for s in idx['sections']}
        self.begins = sorted(r[0] for r in idx['functions'])
        self.ends = {r[0]: r[1] for r in idx['functions']}
        self.vt = set(idx['vt_names'])
        self.size = idx['size_of_image']

    def section(self, v):
        for name, (lo, hi) in self.sections.items():
            if lo <= v < hi:
                return name
        return None

    def function_of(self, v):
        i = bisect.bisect_right(self.begins, v) - 1
        if i >= 0 and v < self.ends[self.begins[i]]:
            return self.begins[i]
        return None


def classify(exe, value, before, in_bytes_list):
    sec = exe.section(value)
    rva_ctx = bool(RVA_BEFORE.search(before))
    if in_bytes_list and value <= 0xff:
        return 'bytes'
    if sec is None:
        return 'offset'
    field = bool(re.search(r'[\w\]\)]\s*\+\s*$', before)) and not rva_ctx
    if sec == '.text':
        fn = exe.function_of(value)
        if rva_ctx and (fn is not None or value >= 0x1000):
            return 'rva-code'   # a leaf without unwind info has no .pdata entry
        if fn == value and value >= 0x10000 and not field:
            return 'rva-code?'   # a function begin, no context: likely but review
        return 'offset'
    if sec == '.rdata':
        if rva_ctx or value in exe.vt:
            return 'rva-rdata'
        return 'offset' if value < 0x1000000 else 'rva-rdata?'
    if sec in ('.data', '.pdata', '.tls', '_RDATA'):
        if rva_ctx:
            return 'rva-data'
        return 'rva-data?' if value >= 0x2272000 else 'offset'
    return 'offset'


def main():
    exe = Exe(sys.argv[1])
    out = []
    src = os.path.join(ROOT, 'src')
    for name in sorted(os.listdir(src)):
        if not name.endswith(('.hpp', '.cpp', '.h')):
            continue
        path = os.path.join(src, name)
        with open(path, encoding='utf-8', errors='replace', newline='') as f:
            lines = f.read().split('\n')
        for n, line, mask in strip_comments(lines):
            for m in HEX.finditer(line):
                s, e = m.span()
                code = all(mask[s:e])
                value = int(m.group(0), 16)
                before = line[max(0, s - 160):s]
                # a {0x..,0x..,...} list of bytes
                # neighbouring lines count too: a byte array may break across lines (3 bytes here, 4 on the next)
                prev_line = lines[n - 2] if n >= 2 else ''
                next_line = lines[n] if n < len(lines) else ''
                around = prev_line[-60:] + ' ' + line[max(0, s - 60):min(len(line), e + 60)] + ' ' + next_line[:60]
                in_bytes = bool(re.search(r'(?:std::byte\{|\{|,)\s*$', line[max(0, s - 12):s]) or not line[:s].strip()) and \
                    len(re.findall(r'0x[0-9a-fA-F]{1,2}\b', around)) >= 4 and value <= 0xff
                cls = 'comment' if not code else classify(exe, value, before, in_bytes)
                out.append({'file': 'src/' + name, 'line': n, 'col': s, 'text': m.group(0), 'value': value, 'cls': cls,
                            'before': line[max(0, s - 50):s].strip()})
            # decimal elements of a byte array ({0x48,0x8b,0,0} / std::byte{0}): bytes too, so the array stays one group
            def emit(s, text):
                value = int(text)
                if value <= 0xff and all(mask[s:s + len(text)]):
                    out.append({'file': 'src/' + name, 'line': n, 'col': s, 'text': text, 'value': value, 'cls': 'bytes',
                                'before': line[max(0, s - 50):s].strip()})
            if len(re.findall(r'0x[0-9a-fA-F]{1,2}\b', line)) >= 3:
                for m in re.finditer(r'std::byte\{(\d{1,3})\}', line):          # std::byte{0} is a byte by type
                    emit(m.start(1), m.group(1))
                for g in re.finditer(r'\{([^{}]*)\}', line):                     # an innermost list made only of byte values
                    body, base0 = g.group(1), g.start(1)
                    items = [(mm.start() + base0, mm.group(0).strip()) for mm in re.finditer(r'[^,]+', body)]
                    tokens = [t for _, t in items]
                    if len(tokens) >= 4 and all(re.fullmatch(r'0x[0-9a-fA-F]{1,2}|\d{1,3}', t) for t in tokens) and \
                            sum(1 for t in tokens if t.startswith('0x')) >= 3:
                        for pos, tok in items:
                            if tok.isdigit():
                                emit(line.index(tok, pos), tok)   # the item may start with spaces
    out.sort(key=lambda r: (r['file'], r['line'], r['col']))
    json.dump(out, sys.stdout, indent=0)


if __name__ == '__main__':
    main()

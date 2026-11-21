"""Translate Dishonored 2 (pinned build) addresses and field offsets to Death of the Outsider.

Library + CLI. Every answer carries a method and a confidence:
  exact    the address sits in a function whose instruction stream is identical (same length),
           a vtable paired by RTTI name, an import slot paired by (dll, symbol), or a cvar paired by name
  aligned  the instruction was paired by sequence alignment inside a changed function
  voted    a data address paired by aligned rip-relative references (votes/total given)
  near     a data address inside an object whose start was paired (same delta)
  none     not mapped

CLI:  python tools/doto/translate.py 0x374e40 0x39a5290 ...
"""
import bisect
import collections
import os
import pickle
import struct
import sys

import pefile

LAB = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'artifacts', 'doto-lab')
D2_EXE = r'D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe'
DOTO_EXE = r'D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider\Dishonored_DO.exe'


class Image:
    def __init__(self, path, idx):
        self.path = path
        self.idx = idx
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        self.pe = pe
        self.mem = pe.get_memory_mapped_image()
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = {s[0]: (s[1], s[1] + s[2]) for s in idx['sections']}
        self.fns = {r[0]: r for r in idx['functions']}
        self.begins = sorted(self.fns)
        self.imports = {}
        self.import_by_slot = {}
        for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
            dll = entry.dll.decode().lower()
            for imp in entry.imports:
                key = (dll, imp.name.decode() if imp.name else '#%d' % imp.ordinal)
                slot = imp.address - self.base
                self.imports[key] = slot
                self.import_by_slot[slot] = key

    def section(self, rva):
        for name, (lo, hi) in self.sections.items():
            if lo <= rva < hi:
                return name
        return None

    def function_of(self, rva):
        i = bisect.bisect_right(self.begins, rva) - 1
        if i >= 0 and rva < self.fns[self.begins[i]][1]:
            return self.begins[i]
        return None

    def u64(self, rva):
        return struct.unpack_from('<Q', self.mem, rva)[0]

    def u32(self, rva):
        return struct.unpack_from('<I', self.mem, rva)[0]

    def cstr(self, rva, n=256):
        return self.mem[rva:rva + n].split(b'\0')[0]

    def bytes(self, rva, n):
        return bytes(self.mem[rva:rva + n])


class Translator:
    def __init__(self, lab=LAB):
        def load(name):
            with open(os.path.join(lab, name), 'rb') as f:
                return pickle.load(f)
        self.a = Image(D2_EXE, load('d2.idx'))
        self.b = Image(DOTO_EXE, load('doto.idx'))
        m = load('map.pickle')
        al = load('align.pickle')
        self.fn = m['fn']
        self.how = m['how']
        self.vt = m['vt']
        self.quality = al['quality']
        self.code = al['code']
        self.disp = al['disp']
        self.rip = al['rip']
        self.imm = al['imm']
        self.data_votes = m['data']
        # rip-relative targets from aligned instruction pairs: d2 -> Counter(doto)
        self.rip_anchor = sorted(self.rip)
        self._cvar_cache = None
        self._strings_b = None

    # ---------- code ----------
    def code_rva(self, x):
        a, b = self.a, self.b
        f = a.function_of(x)
        if f is None:
            # leaf code without unwind info: aligned call/jump targets, then a unique masked byte search
            y, how, why = self.voted(x)
            if y is not None:
                return y, how, 'leaf: ' + why
            return self.pattern(x)
        g = self.fn.get(f)
        if g is None:
            return None, 'none', f'function {f:#x} unmatched'
        q = self.quality.get(f)
        la, lb = a.fns[f][1] - f, b.fns[g][1] - g
        if q and q[3] and la == lb:
            return g + (x - f), 'exact', f'fn {f:#x}->{g:#x} identical ({self.how.get(f)})'
        pairs = self.code.get(f)
        if pairs is None:
            # identical token stream but different length (rel8 vs rel32 branch): fall back to begin only
            if x == f:
                return g, 'exact', f'function begin ({self.how.get(f)})'
            return None, 'none', f'fn {f:#x} same shape, lengths {la:#x}/{lb:#x}'
        arr = struct.unpack('<%di' % (len(pairs) // 4), pairs)
        offs = arr[0::2]
        tgt = arr[1::2]
        o = x - f
        i = bisect.bisect_right(offs, o) - 1
        if i >= 0 and offs[i] == o:
            return g + tgt[i], 'aligned', f'fn {f:#x}->{g:#x} insn {o:#x}->{tgt[i]:#x} ({q[2]}/{q[0]} aligned)'
        if x == f:
            return g, 'aligned', 'function begin'
        if i >= 0 and o - offs[i] < 16:
            # inside an aligned instruction (a patch site within it)
            return g + tgt[i] + (o - offs[i]), 'aligned', f'inside insn {offs[i]:#x}+{o - offs[i]}'
        return None, 'none', f'fn {f:#x} changed and offset {o:#x} not aligned'

    def masked(self, img, rva, n):
        """bytes at rva with rip displacements and branch targets zeroed, plus the mask"""
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
        from capstone import x86_const as X
        md = getattr(self, '_md', None)
        if md is None:
            md = self._md = Cs(CS_ARCH_X86, CS_MODE_64)
            md.detail = True
        blob = img.bytes(rva, n + 16)
        out = bytearray()
        keep = []
        for insn in md.disasm(blob, rva):
            if insn.address - rva >= n:
                break
            raw = bytearray(insn.bytes)
            k = [True] * len(raw)
            if insn.disp_size and any(op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP for op in insn.operands):
                for i in range(insn.disp_size):
                    k[insn.disp_offset + i] = False
            if (X.X86_GRP_JUMP in insn.groups or X.X86_GRP_CALL in insn.groups) and insn.imm_size:
                for i in range(insn.imm_size):
                    k[insn.imm_offset + i] = False
            for i, kk in enumerate(k):
                if not kk:
                    raw[i] = 0
            out += raw
            keep += k
        return bytes(out[:n]), keep[:n]

    def fields(self, img, rva, n):
        """Instruction fields overlapping [rva, rva+n): list of (lo, hi, kind, value) in window offsets.
        Decodes from the start of the containing function so a window that begins mid-instruction is right."""
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
        from capstone import x86_const as X
        md = getattr(self, '_md', None)
        if md is None:
            md = self._md = Cs(CS_ARCH_X86, CS_MODE_64)
            md.detail = True
        f = img.function_of(rva)
        start = f if f is not None and rva - f < 0x20000 else rva
        blob = img.bytes(start, rva - start + n + 16)
        out = []
        for insn in md.disasm(blob, start):
            if insn.address >= rva + n:
                break
            end = insn.address + insn.size
            if end <= rva:
                continue
            base = insn.address - rva
            out.append((base, base + insn.size, 'insn', insn.mnemonic))
            for op in insn.operands:
                if op.type == X.X86_OP_MEM:
                    if insn.disp_size:
                        lo = base + insn.disp_offset
                        kind = 'rip' if op.mem.base == X.X86_REG_RIP else 'disp'
                        out.append((lo, lo + insn.disp_size, kind, op.mem.disp))
                elif op.type == X.X86_OP_IMM and insn.imm_size:
                    lo = base + insn.imm_offset
                    kind = 'branch' if (X.X86_GRP_JUMP in insn.groups or X.X86_GRP_CALL in insn.groups) else 'imm'
                    out.append((lo, lo + insn.imm_size, kind, op.imm))
        return out

    def compare_bytes(self, a_rva, b_rva, n):
        """Classify the differences between D2 bytes at a_rva and DOTO bytes at b_rva.
        Returns (status, notes): same | relocated | layout | semantic."""
        ab, bb = self.a.bytes(a_rva, n), self.b.bytes(b_rva, n)
        if ab == bb:
            return 'same', []
        fa, fb = self.fields(self.a, a_rva, n), self.fields(self.b, b_rva, n)
        if [(x[0], x[1], x[2]) for x in fa] != [(x[0], x[1], x[2]) for x in fb]:
            return 'semantic', ['instruction boundaries differ']
        if [x[3] for x in fa if x[2] == 'insn'] != [x[3] for x in fb if x[2] == 'insn']:
            return 'semantic', ['mnemonics differ']
        status = 'relocated'
        notes = []
        for i in range(n):
            if ab[i] == bb[i]:
                continue
            owner = [k for k, x in enumerate(fa) if x[2] != 'insn' and x[0] <= i < x[1]]
            if not owner:
                return 'semantic', [f'byte {i} outside any operand field']
            k = owner[0]
            kind = fa[k][2]
            if kind in ('rip', 'branch'):
                continue
            if kind == 'disp':
                d2, dd = fa[k][3], fb[k][3]
                y, how, why = self.offset(d2)
                if y == dd and how == 'moved':
                    status = 'layout'
                    note = f'disp {d2:#x}->{dd:#x}'
                    if note not in notes:
                        notes.append(note)
                    continue
                return 'semantic', [f'disp {d2:#x}->{dd:#x} not the voted move ({how} {why})']
            return 'semantic', [f'{kind} {fa[k][3]:#x}->{fb[k][3]:#x}']
        return status, notes

    def pattern(self, x, n=24):
        import re
        pat, keep = self.masked(self.a, x, n)
        rx = b''.join(re.escape(bytes([c])) if k else b'.' for c, k in zip(pat, keep))
        lo, hi = self.b.sections['.text']
        blob = self.b.mem[lo:hi]
        hits = [m.start() + lo for m in re.finditer(rx, blob, re.S)]
        if len(hits) == 1:
            return hits[0], 'pattern', f'unique masked {n}-byte match'
        if len(hits) > 1 and n < 96:
            return self.pattern(x, n * 2)
        return None, 'none', f'{len(hits)} masked matches'

    # ---------- vtables / rdata ----------
    def vtable_rva(self, x):
        if x in self.vt:
            return self.vt[x], 'exact', 'vtable (RTTI ' + self.a.idx['vt_names'][x][0] + ')'
        # a slot inside a vtable
        best = None
        for vt0 in self.a.idx['vt_names']:
            if vt0 <= x < vt0 + 8 * 600 and (best is None or vt0 > best):
                best = vt0
        if best is not None and best in self.vt and (x - best) % 8 == 0:
            name = self.a.idx['vt_names'][best][0]
            rows_a = [r for r in self.a.idx['rtti'][name] if r[0] == best]
            rows_b = [r for r in self.b.idx['rtti'].get(name, []) if r[0] == self.vt[best]]
            k = (x - best) // 8
            if rows_a and rows_b and k < len(rows_a[0][3]) and k < len(rows_b[0][3]):
                same = len(rows_a[0][3]) == len(rows_b[0][3])
                return self.vt[best] + 8 * k, 'exact' if same else 'aligned', f'slot {k} of {name}'
        return None, 'none', ''

    def string_rva(self, x):
        s = self.a.cstr(x, 512)
        if len(s) < 4 or not all(32 <= c < 127 for c in s):
            return None, 'none', ''
        needle = s + b'\0'
        lo, hi = self.b.sections['.rdata']
        blob = self.b.mem[lo:hi]
        hits = []
        start = 0
        while len(hits) < 3:
            i = blob.find(needle, start)
            if i < 0:
                break
            # must be the start of a string
            if i == 0 or blob[i - 1] == 0:
                hits.append(lo + i)
            start = i + 1
        if len(hits) == 1:
            return hits[0], 'exact', f'string {s[:40]!r}'
        return None, 'none', f'string {s[:40]!r} has {len(hits)} hits'

    def import_rva(self, x):
        key = self.a.import_by_slot.get(x)
        if key and key in self.b.imports:
            return self.b.imports[key], 'exact', f'import {key[0]}!{key[1]}'
        return None, 'none', ''

    # ---------- data via aligned references ----------
    def voted(self, x):
        c = self.rip.get(x)
        if c:
            y, n = max(c.items(), key=lambda kv: kv[1])
            tot = sum(c.values())
            if n / tot >= 0.75:
                return y, 'voted', f'{n}/{tot} aligned refs'
            return None, 'none', f'conflicting refs {n}/{tot}'
        return None, 'none', ''

    def near(self, x, window=0x800):
        i = bisect.bisect_right(self.rip_anchor, x) - 1
        while i >= 0 and x - self.rip_anchor[i] <= window:
            p = self.rip_anchor[i]
            y, how, why = self.voted(p)
            if y is not None:
                # require the next anchor after p to keep the same delta (no reorder in between)
                j = bisect.bisect_right(self.rip_anchor, x)
                if j < len(self.rip_anchor):
                    p2 = self.rip_anchor[j]
                    y2, _, _ = self.voted(p2)
                    if y2 is not None and (y2 - p2) != (y - p) and p2 - x < window:
                        return None, 'none', f'deltas differ around {x:#x}'
                return y + (x - p), 'near', f'anchor {p:#x}+{x - p:#x} ({why})'
            i -= 1
        return None, 'none', 'no anchor'

    # ---------- cvars by name ----------
    def cvar_by_name(self, name):
        """DOTO cvar object rva for a cvar name: an object in .data whose +8 is a pointer to the name string."""
        if self._cvar_cache is None:
            self._cvar_cache = self._index_cvars(self.b)
        return self._cvar_cache.get(name)

    def _index_cvars(self, img):
        out = {}
        lo, hi = img.sections['.data']
        rlo, rhi = img.sections['.rdata']
        mem = img.mem
        for off in range(lo, min(hi, lo + 0x1c00000), 8):
            try:
                p = struct.unpack_from('<Q', mem, off)[0]
            except struct.error:
                break
            r = p - img.base
            if rlo <= r < rhi:
                s = img.cstr(r, 96)
                if 3 <= len(s) < 90 and s[:1].isalpha() and b'_' in s and all(33 <= c < 127 for c in s):
                    out.setdefault(s.decode(), []).append(off)
        return out

    # ---------- generic ----------
    def rva(self, x):
        sec = self.a.section(x)
        if sec == '.text':
            return self.code_rva(x)
        if sec == '.rdata':
            for f in (self.import_rva, self.vtable_rva, self.voted, self.string_rva, self.near):
                y, how, why = f(x)
                if y is not None:
                    return y, how, why
            return None, 'none', 'rdata unmapped'
        if sec in ('.data', '_RDATA', '.tls'):
            for f in (self.voted, self.near):
                y, how, why = f(x)
                if y is not None:
                    return y, how, why
            return None, 'none', 'data unmapped'
        return None, 'none', f'section {sec}'

    def offset(self, d):
        v = self.disp.get(d)
        if not v:
            return None, 'none', 'no aligned access'
        tot = sum(v.values())
        y, n = max(v.items(), key=lambda kv: kv[1])
        return y, ('same' if y == d else 'moved') if n / tot >= 0.9 else 'mixed', f'{n}/{tot}' + ('' if n == tot else ' ' + ','.join(f'{k:#x}:{c}' for k, c in sorted(v.items(), key=lambda kv: -kv[1])[:4]))


def h(v):
    return '-' if v is None else f'{v:#x}'


def main():
    t = Translator()
    for s in sys.argv[1:]:
        x = int(s, 0)
        y, how, why = t.rva(x)
        o = t.offset(x)
        print(f'{x:#x} -> {h(y)} [{how}] {why} | as offset: {h(o[0])} {o[1]} {o[2]}')


if __name__ == '__main__':
    main()

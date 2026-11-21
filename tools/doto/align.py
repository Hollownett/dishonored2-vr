"""Instruction-level alignment of matched D2/DOTO function pairs.

For every matched pair it disassembles both functions, tokenises each instruction by its
shape (mnemonic, operand kinds, registers, memory base/index/scale; NOT displacements,
immediates or rip targets) and aligns the two token lists. Aligned instruction pairs give:
  * code map:  d2 instruction rva -> doto instruction rva        (for hook sites, call sites)
  * disp votes: non-rip memory displacement d2 -> doto            (struct field offsets)
  * imm votes:  immediate d2 -> doto when they differ               (sizes, ids)
  * rip votes:  rip-relative target d2 -> doto                      (globals, constants, vtables)

Usage: python tools/doto/align.py artifacts/doto-lab/d2.idx artifacts/doto-lab/doto.idx artifacts/doto-lab/map.pickle artifacts/doto-lab/align.pickle
"""
import array
import collections
import difflib
import multiprocessing as mp
import pickle
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone import x86_const as X

_A = _B = None


def _load_text(exe):
    pe = pefile.PE(exe, fast_load=True)
    t = pe.sections[0]
    return t.VirtualAddress, t.get_data()


def _init(a_exe, b_exe):
    global _A, _B, _MD
    _A = _load_text(a_exe)
    _B = _load_text(b_exe)
    _MD = Cs(CS_ARCH_X86, CS_MODE_64)
    _MD.detail = True


def tokens(side, begin, end):
    va, code = side
    blob = code[begin - va:end - va]
    out = []
    for insn in _MD.disasm(blob, begin):
        shape = [insn.mnemonic]
        disp = []
        imm = []
        rip = None
        for op in insn.operands:
            if op.type == X.X86_OP_REG:
                shape.append('r%d' % op.reg)
            elif op.type == X.X86_OP_IMM:
                if X.X86_GRP_JUMP in insn.groups or X.X86_GRP_CALL in insn.groups:
                    shape.append('t')
                    rip = op.imm
                else:
                    shape.append('i')
                    imm.append(op.imm)
            elif op.type == X.X86_OP_MEM:
                m = op.mem
                if m.base == X.X86_REG_RIP:
                    shape.append('m[rip]%d' % op.size)
                    rip = insn.address + insn.size + m.disp
                else:
                    shape.append('m[%d+%d*%d]%d' % (m.base, m.index, m.scale, op.size))
                    disp.append(m.disp)
        out.append((' '.join(shape), insn.address, disp, imm, rip))
    return out


def _pair(job):
    a0, a1, b0, b1 = job
    ta = tokens(_A, a0, a1)
    tb = tokens(_B, b0, b1)
    sa = [t[0] for t in ta]
    sb = [t[0] for t in tb]
    if sa == sb:
        blocks = [(0, 0, len(sa))]
        identical = True
    else:
        identical = False
        if len(sa) * len(sb) > 4_000_000:
            # very long functions: align in windows anchored on the common prefix/suffix only
            p = 0
            while p < min(len(sa), len(sb)) and sa[p] == sb[p]:
                p += 1
            q = 0
            while q < min(len(sa), len(sb)) - p and sa[-1 - q] == sb[-1 - q]:
                q += 1
            blocks = [(0, 0, p), (len(sa) - q, len(sb) - q, q)]
        else:
            blocks = difflib.SequenceMatcher(None, sa, sb, autojunk=False).get_matching_blocks()
    code = array.array('i')
    disp = []
    imm = []
    rip = []
    matched = 0
    for i, j, n in blocks:
        for k in range(n):
            x, y = ta[i + k], tb[j + k]
            matched += 1
            if not identical:
                code.append(x[1] - a0)
                code.append(y[1] - b0)
            for p, q in zip(x[2], y[2]):
                disp.append((p, q))
            for p, q in zip(x[3], y[3]):
                if p != q:
                    imm.append((p, q))
            if x[4] is not None and y[4] is not None:
                rip.append((x[4], y[4]))
    return a0, len(ta), len(tb), matched, identical, code.tobytes(), disp, imm, rip


def main():
    a_idx, b_idx, map_path, out_path = sys.argv[1:5]
    with open(a_idx, 'rb') as f:
        A = pickle.load(f)
    with open(b_idx, 'rb') as f:
        B = pickle.load(f)
    with open(map_path, 'rb') as f:
        M = pickle.load(f)
    af = {r[0]: r for r in A['functions']}
    bf = {r[0]: r for r in B['functions']}
    jobs = [(x, af[x][1], y, bf[y][1]) for x, y in M['fn'].items()]
    jobs.sort(key=lambda j: -(j[1] - j[0]))   # long ones first for balance
    code_map = {}
    fn_quality = {}
    disp_votes = collections.defaultdict(collections.Counter)
    imm_votes = collections.defaultdict(collections.Counter)
    rip_votes = collections.defaultdict(collections.Counter)
    with mp.Pool(initializer=_init, initargs=(A['exe'], B['exe'])) as pool:
        for a0, na, nb, matched, identical, code, disp, imm, rip in pool.imap_unordered(_pair, jobs, chunksize=16):
            fn_quality[a0] = (na, nb, matched, identical)
            if not identical:
                code_map[a0] = code   # int32 pairs (d2 offset, doto offset) within the two functions
            for p, q in disp:
                disp_votes[p][q] += 1
            for p, q in imm:
                imm_votes[p][q] += 1
            for p, q in rip:
                rip_votes[p][q] += 1
    out = {'code': code_map, 'quality': fn_quality,
           'disp': {k: dict(v) for k, v in disp_votes.items()},
           'imm': {k: dict(v) for k, v in imm_votes.items()},
           'rip': {k: dict(v) for k, v in rip_votes.items()}}
    with open(out_path, 'wb') as f:
        pickle.dump(out, f, protocol=pickle.HIGHEST_PROTOCOL)
    full = sum(1 for q in fn_quality.values() if q[3])
    print(f'pairs {len(fn_quality)}, fully aligned {full}, changed fns {len(code_map)}, disp keys {len(disp_votes)}, rip keys {len(rip_votes)}')


if __name__ == '__main__':
    main()

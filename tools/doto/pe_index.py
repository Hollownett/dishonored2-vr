"""Index a game executable for D2 -> DOTO address mapping (read-only on the exe).

For every function in the exception directory (.pdata) it records:
  begin, end, a hash of the bytes with every relocatable operand masked (rip-relative
  displacements, rel8/rel32 branch targets), a hash of the exact bytes, the ordered list
  of rip-relative targets and call targets, and a per-instruction (offset, mnemonic) list
  is NOT kept (too large); align.py re-disassembles matched pairs on demand.

It also records the MSVC RTTI: class name -> list of vtable RVAs (one per complete
object locator), so vtables can be paired by class name.

Usage: python tools/doto/pe_index.py <exe> <out.pickle>
"""
import hashlib
import multiprocessing as mp
import pickle
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone import x86_const as X

_CODE = None
_TEXT_VA = 0
_IMAGE = 0


def _init(code, text_va, image):
    global _CODE, _TEXT_VA, _IMAGE, _MD
    _CODE, _TEXT_VA, _IMAGE = code, text_va, image
    _MD = Cs(CS_ARCH_X86, CS_MODE_64)
    _MD.detail = True


def _one(fn):
    begin, end = fn
    off = begin - _TEXT_VA
    blob = _CODE[off:off + (end - begin)]
    masked = bytearray(blob)
    refs = []      # rip-relative data targets, in order
    calls = []     # direct call targets, in order
    mnems = []
    n = 0
    for insn in _MD.disasm(blob, begin):
        n += 1
        mnems.append(insn.mnemonic)
        rel = insn.address - begin
        # rip-relative memory operand
        if insn.disp_size and any(op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP for op in insn.operands):
            d0 = rel + insn.disp_offset
            target = insn.address + insn.size + insn.disp
            refs.append(target)
            for i in range(insn.disp_size):
                masked[d0 + i] = 0
            # an immediate after a rip operand (mov [rip+x], imm) keeps its value
        grp = insn.groups
        if (X.X86_GRP_JUMP in grp or X.X86_GRP_CALL in grp) and insn.imm_size and insn.operands and insn.operands[0].type == X.X86_OP_IMM:
            target = insn.operands[0].imm
            i0 = rel + insn.imm_offset
            for i in range(insn.imm_size):
                masked[i0 + i] = 0
            if X.X86_GRP_CALL in grp:
                calls.append(target)
            elif not (begin <= target < end):
                calls.append(target)   # tail jump out of the function
    mh = hashlib.blake2b(bytes(masked), digest_size=12).digest()
    eh = hashlib.blake2b(blob, digest_size=12).digest()
    sh = hashlib.blake2b(' '.join(mnems).encode(), digest_size=12).digest()
    return begin, end, mh, eh, sh, n, refs, calls


def rtti(pe, data, image_base):
    """class name -> [vtable rva]; vtable rva -> (name, offset_in_complete)"""
    rdata = next(s for s in pe.sections if s.Name.startswith(b'.rdata'))
    r0 = rdata.VirtualAddress
    rbytes = rdata.get_data()
    out = {}
    by_vt = {}
    # a vtable is preceded by a pointer to a COL (signature 1, x64)
    mem = pe.get_memory_mapped_image()
    def u32(rva):
        return struct.unpack_from('<I', mem, rva)[0]
    def u64(rva):
        return struct.unpack_from('<Q', mem, rva)[0]
    lo, hi = image_base + r0, image_base + r0 + len(rbytes)
    for i in range(0, len(rbytes) - 16, 8):
        col = struct.unpack_from('<Q', rbytes, i)[0]
        if not (lo <= col < hi):
            continue
        col_rva = col - image_base
        try:
            if u32(col_rva) != 1:
                continue
            td = u32(col_rva + 12)
            self_rva = u32(col_rva + 20)
            if self_rva != col_rva:
                continue
            name = mem[td + 16:td + 16 + 200].split(b'\0')[0].decode('ascii', 'replace')
            off = u32(col_rva + 4)
        except (struct.error, IndexError):
            continue
        vt = r0 + i + 8
        # first slot must point into .text
        first = u64(vt)
        if not (image_base + 0x1000 <= first < image_base + 0x1000 + pe.sections[0].Misc_VirtualSize):
            continue
        # count slots
        slots = []
        j = vt
        tlo, thi = image_base + 0x1000, image_base + 0x1000 + pe.sections[0].Misc_VirtualSize
        while len(slots) < 4096:
            p = u64(j)
            if not (tlo <= p < thi):
                break   # the next vtable's COL pointer (in .rdata) or data ends the table
            slots.append(p - image_base)
            j += 8
        out.setdefault(name, []).append((vt, off, col_rva, slots))
        by_vt[vt] = (name, off)
    return out, by_vt


def main():
    exe, dst = sys.argv[1], sys.argv[2]
    pe = pefile.PE(exe, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
    image_base = pe.OPTIONAL_HEADER.ImageBase
    text = pe.sections[0]
    code = text.get_data()
    fns = sorted({(e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION})
    # merge chained unwind fragments: keep only entries whose begin is not inside a previous range
    with mp.Pool(initializer=_init, initargs=(code, text.VirtualAddress, image_base)) as pool:
        rows = pool.map(_one, fns, chunksize=512)
    classes, by_vt = rtti(pe, None, image_base)
    out = {
        'exe': exe,
        'image_base': image_base,
        'size_of_image': pe.OPTIONAL_HEADER.SizeOfImage,
        'timestamp': pe.FILE_HEADER.TimeDateStamp,
        'sections': [(s.Name.rstrip(b'\0').decode(), s.VirtualAddress, s.Misc_VirtualSize) for s in pe.sections],
        'functions': rows,
        'rtti': classes,
        'vt_names': by_vt,
    }
    with open(dst, 'wb') as f:
        pickle.dump(out, f, protocol=pickle.HIGHEST_PROTOCOL)
    print(f'{exe}: {len(rows)} functions, {len(classes)} RTTI classes, {len(by_vt)} vtables')


if __name__ == '__main__':
    main()

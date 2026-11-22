"""Static look at how an engine cvar is registered in each game exe: finds the name string, every rip-relative lea of it,
and disassembles the instructions around each one (the flags and default value sit in the same constructor call).

    python tools/doto/cvar_static.py in_controlInactiveWindow [win_pauseOnFocusLoss ...]
"""
import struct
import sys
from pathlib import Path

import capstone
import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target  # noqa: E402

EXES = {
    'D2': Path(r'D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe'),
    'DOTO': Path(r'D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider\Dishonored_DO.exe'),
}


def lea_refs(pe, data, text, rva_target):
    """Offsets in .text of `lea r64, [rip+disp]` (48/4c 8d modrm 05/0d/15/...) whose target is rva_target."""
    t0, tbytes = text.VirtualAddress, data[text.PointerToRawData:text.PointerToRawData + text.SizeOfRawData]
    out = []
    i = 0
    while True:
        i = tbytes.find(b'\x8d', i + 1)
        if i < 0:
            break
        if i < 1 or tbytes[i - 1] not in (0x48, 0x4c):
            continue
        modrm = tbytes[i + 1] if i + 1 < len(tbytes) else 0
        if modrm & 0xc7 != 0x05:
            continue
        disp, = struct.unpack_from('<i', tbytes, i + 2)
        end = t0 + i + 6
        if end + disp == rva_target:
            out.append(t0 + i - 1)
    return out


def main():
    names = sys.argv[1:] or ['in_controlInactiveWindow', 'win_pauseOnFocusLoss']
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for game, exe in EXES.items():
        if not exe.exists():
            print(game, 'missing', exe)
            continue
        data = exe.read_bytes()
        pe = pefile.PE(data=data, fast_load=True)
        text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
        for name in names:
            off = data.find(name.encode() + b'\0')
            while off > 0 and data[off - 1] != 0:
                off = data.find(name.encode() + b'\0', off + 1)
            rva = pe.get_rva_from_offset(off)
            refs = lea_refs(pe, data, text, rva)
            print(f'== {game} {name} string rva {rva:#x} lea refs {[hex(r) for r in refs]}')
            for r in refs[:4]:
                start = r - 0x30
                code = pe.get_data(start, 0x70)
                for ins in md.disasm(code, pe.OPTIONAL_HEADER.ImageBase + start):
                    note = ''
                    if 'rip' in ins.op_str and ins.mnemonic == 'lea':
                        disp = int(ins.op_str.split('rip')[1].strip(' +]').replace('- ', '-'), 0) if '+' in ins.op_str or '-' in ins.op_str else 0
                        tgt = ins.address + ins.size + disp - pe.OPTIONAL_HEADER.ImageBase
                        try:
                            s = pe.get_string_at_rva(tgt, 80)
                            if s and all(32 <= c < 127 for c in s):
                                note = f'  ; "{s.decode()}"'
                        except Exception:
                            pass
                    print(f'   {ins.address - pe.OPTIONAL_HEADER.ImageBase:#09x} {ins.mnemonic} {ins.op_str}{note}')
                print()


if __name__ == '__main__':
    main()

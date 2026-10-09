"""Read-only exact-build displacement scan; candidates are NOT decoded instructions."""
import bisect
import hashlib
import json
from pathlib import Path
import struct
from probe_game import PE

SHA = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'


def scan(data):
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Unsupported executable')
    pe = PE(data)
    pdata = next(s for s in pe.sections if s['name'] == '.pdata')
    ranges = [struct.unpack_from('<III', data, pdata['raw_offset'] + i)
              for i in range(0, pdata['virtual_size'] - 11, 12)]
    starts = [r[0] for r in ranges]
    rows = []
    for section in pe.sections:
        if not section['executable']:
            continue
        code = data[section['raw_offset']:section['raw_offset'] + section['raw_size']]
        for field in range(0x2527c, 0x252ac, 4):
            needle, at = struct.pack('<I', field), 0
            while True:
                at = code.find(needle, at)
                if at < 0:
                    break
                rva = section['rva'] + at
                index = bisect.bisect_right(starts, rva) - 1
                unwind = ranges[index] if index >= 0 else None
                rows.append({'field': hex(field), 'displacement_rva': hex(rva),
                             'unwind_start': hex(unwind[0]) if unwind and rva < unwind[1] else None,
                             'preceding_bytes': code[max(0, at - 8):at].hex()})
                at += 4
    return {'sha256': SHA, 'warning': 'Byte candidates only; unwind entries can be chained fragments.',
            'candidates': rows}


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    analysis = root / 'artifacts/re-toolchain/direct-ghidra'
    report = scan((analysis / 'Dishonored2-steam17576867.exe').read_bytes())
    (analysis / 'aim-field-candidates.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Exported {len(report['candidates'])} offline byte candidates")

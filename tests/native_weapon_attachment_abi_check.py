"""Offline exact-PE ABI/prefix checks; never opens the game process."""
import hashlib
from pathlib import Path
import re
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_game import PE
ROOT=Path(__file__).resolve().parents[1]

class AttachmentAbiCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data=(ROOT/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
        if hashlib.sha256(data).hexdigest()!='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180':
            raise ValueError('Native executable is not the pinned build')
        cls.pe=PE(data)

    def code(self,rva,size):
        offset=self.pe.offset(rva)
        return self.pe.data[offset:offset+size]

    def test_installed_prefixes_are_exact_native_code(self):
        source=(ROOT/'src/native_weapon_attachment_trace.hpp').read_text()
        block=source.split('std::array<std::array<unsigned char,32>,4> expected{{',1)[1].split('}}}};',1)[0]
        rows=re.findall(r'\{\{([^{}]+)',block)
        self.assertEqual(len(rows),4)
        for rva,row in zip((0x7506d0,0x9b900,0x753c10,0x53d6a0),rows):
            expected=bytes(int(token.strip(),0) for token in row.strip().rstrip(',').split(','))
            self.assertEqual(len(expected),32)
            self.assertEqual(self.code(rva,32),expected)

    def test_exact_compose_and_scoped_leaf_calls(self):
        for call,target in ((0x753d04,0x7506d0),(0x753edc,0x7506d0),(0x53d534,0x9b900),
                            (0x7550ec,0x753c10),(0x754582,0x753c10),(0x75422f,0x53d6a0),(0x53d700,0x9b900)):
            code=self.code(call,5)
            self.assertEqual(code[0],0xe8)
            self.assertEqual(call+5+struct.unpack('<i',code[1:])[0],target)

    def test_binding_primary_and_secondary_rtti(self):
        frames=self.pe.rtti_vtables('arkCpntReferenceFrame')
        self.assertEqual({(entry['vtable_rva'],entry['subobject_offset']) for entry in frames},
                         {(0x1c573e8,0),(0x1c574c0,32)})
        methods=self.code(0x1c573e8,32)
        self.assertEqual(struct.unpack_from('<Q',methods,24)[0]-self.pe.image_base,0x751b00)

if __name__=='__main__':unittest.main()

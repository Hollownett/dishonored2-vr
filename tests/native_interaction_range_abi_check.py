"""Pinned offline range-origin ABI and native stage0 caller verification."""
import hashlib
from pathlib import Path
import re
import struct
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from probe_game import PE

class RangeAbi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data = (ROOT/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
        if hashlib.sha256(data).hexdigest() != 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180':
            raise ValueError('Native executable differs from pinned build')
        cls.pe = PE(data)
        cls.source = (ROOT/'src/native_interaction_aim.hpp').read_text()

    def code(self, rva, length):
        at = self.pe.offset(rva)
        return self.pe.data[at:at+length]

    def test_prefixes_match_pinned_native_bytes(self):
        for name, rva, length in [('range_origin_code',0x417820,8),('eligibility_code',0x3b80a0,32),('origin_call',0x3b81d3,5)]:
            text = re.search(name+r'\{([^}]+)\}',self.source).group(1)
            expected = bytes(int(value.strip(),0) for value in text.split(','))
            self.assertEqual(len(expected),length)
            self.assertEqual(expected,self.code(rva,length))

    def test_exact_native_leaf_and_guard_prop_call_sites(self):
        self.assertEqual(self.code(0x417820,8),bytes.fromhex('488d81d8000000c3'))
        # Two descriptors in the direct branch and two in the retrieved-volume
        # branch; character detection shares the latter native eligibility calls.
        for call in [0x3b8745,0x3b876c,0x3b8a0b,0x3b8a30,0x3b81d3]:
            code=self.code(call,5)
            self.assertEqual(code[0],0xe8)
            target=call+5+struct.unpack('<i',code[1:])[0]
            self.assertEqual(target,0x417820 if call==0x3b81d3 else 0x3b80a0)
        self.assertEqual(self.code(0x3b893f,3),bytes.fromhex('41b701'))
        self.assertIn('caller!=base_+0x3b81d8',self.source)
        self.assertIn('queued_revision_[scope->side]!=scope->revision',self.source)

if __name__=='__main__':
    unittest.main()

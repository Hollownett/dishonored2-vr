import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inspect_game_state import state_snapshot


class NativeStateSnapshotCheck(unittest.TestCase):
    def setUp(self):
        self.base=0x140000000;self.game=0x200000000;self.manager=self.game+0x1f6ec8
        self.entry=0x300000000;self.outer=3;self.active=1;self.backref=self.manager;self.identifier=1

    def read(self,at,size):
        if at==self.base+0x39a5290:return struct.pack('<Q',self.game)
        if at==self.game:return struct.pack('<Q',self.base+0x1c940a8)
        if at==self.game+0x341058:return struct.pack('<i',self.outer)
        if at==self.manager:
            raw=bytearray(size);struct.pack_into('<QQQ',raw,0x20,self.entry,self.entry,0x99999);return bytes(raw)
        if at==self.entry:
            raw=bytearray(size);struct.pack_into('<QQ',raw,0,self.base+0x1c60478,self.backref)
            struct.pack_into('<i',raw,0x18,self.identifier);raw[0x40]=self.active;return bytes(raw)
        raise OSError('Unreadable candidate')

    def test_owned_state_and_duplicate_reads(self):
        result=state_snapshot(self.read,self.base)
        self.assertEqual(result['logic_mask'],2)
        self.assertEqual(len(result['entries']),1)
        self.assertEqual(result['entries'][0]['slot'],'0x20')
        self.assertEqual(result['invalid_reads'],1)
        self.assertTrue(result['manager_bytes_stable'])

    def test_non_gameplay_outer_state_is_not_relaxed(self):
        self.outer=2
        result=state_snapshot(self.read,self.base)
        self.assertEqual(result['logic_mask'],0)
        self.assertEqual(result['outer_state'],2)
        self.assertEqual(result['entries'][0]['active'],1)

    def test_pause_and_invalid_entries(self):
        self.identifier=2
        self.assertEqual(state_snapshot(self.read,self.base)['logic_mask'],4)
        self.active=0
        self.assertEqual(state_snapshot(self.read,self.base)['logic_mask'],0)
        self.active=1;self.backref=0
        self.assertEqual(state_snapshot(self.read,self.base)['entries'],[])


if __name__=='__main__':unittest.main()

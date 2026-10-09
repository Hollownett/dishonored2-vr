import sys
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import inspect_native_lean_queries as q

class LeanQueriesCheck(unittest.TestCase):
    def setUp(self):
        self.base=0x140000000;self.player=0x200000;self.sensor=0x300000;self.machine=0x400000
        self.decl=0x500000;self.shape=0x600000;self.game=0x700000;self.world=0x800000;self.data=0x900000
        self.mem={}
        self.put(self.player+0x268,'QII',self.data,1,256);self.put(self.data,'Q',self.sensor)
        self.put(self.sensor,'Q',self.base+0x1b40648);self.put(self.sensor+8,'Q',self.decl)
        self.put(self.sensor+0x10,'Q',self.player);self.put(self.sensor+0x18,'iI',1,10)
        self.put(self.sensor+0x20,'Q',self.machine);self.put(self.sensor+0x38,'B',1)
        self.put(self.sensor+0x40,'Q',self.shape);self.put(self.sensor+0x48,'12f',*(0.,)*12)
        self.put(self.sensor+0x78,'2i',41,42);self.put(self.sensor+0x80,'2f',.25,.5)
        self.put(self.decl+0x70,'4fII',.1,.2,.3,.4,0xdeadbeef,0x802b)
        self.put(self.base+0x39a5290,'Q',self.game);self.put(self.game+0x60410,'Q',self.world)
        self.owned=dict(player=self.player,state_machine=self.machine)
    def put(self,at,fmt,*values):
        self.mem.update({at+i:v for i,v in enumerate(struct.pack('<'+fmt,*values))})
    def read(self,at,size):return bytes(self.mem.get(at+i,0) for i in range(size))
    def sample(self):
        with patch.object(q,'interaction_snapshot',return_value=self.owned):return q.lean_queries(self.read,self.base)
    def test_exact_offsets_and_limits(self):
        state=self.sample();self.assertEqual(state['collision_mask'],0x802b)
        self.assertEqual(state['query_ids'],(41,42));self.assertEqual(state['limits'],(.25,.5))
        self.assertAlmostEqual(state['maximum_query_distance_native_units'],.4)
        self.assertFalse(state['resource_abi_verified']);self.assertFalse(state['engine_thread_observed'])
    def test_inactive_owner_and_nonfinite_reject(self):
        self.put(self.sensor+0x18,'iI',1,0)
        with self.assertRaises(ValueError):self.sample()
        self.put(self.sensor+0x18,'iI',1,10);self.put(self.decl+0x70,'f',float('nan'))
        with self.assertRaises(ValueError):self.sample()
    def test_shape_replacement_reject(self):
        def read(at,size):
            value=self.read(at,size)
            if at==self.shape:self.put(self.sensor+0x40,'Q',self.shape+0x1000)
            return value
        with patch.object(q,'interaction_snapshot',return_value=self.owned):
            with self.assertRaisesRegex(ValueError,'changed'):q.lean_queries(read,self.base)
    def test_machine_replacement_reject(self):
        with patch.object(q,'interaction_snapshot',side_effect=[self.owned,dict(player=self.player,state_machine=0x410000)]):
            with self.assertRaisesRegex(ValueError,'changed'):q.lean_queries(self.read,self.base)
if __name__=='__main__':unittest.main()

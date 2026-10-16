import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inspect_door_usage import filter_actors


class DoorInspectorFilterCheck(unittest.TestCase):
    def setUp(self):
        self.historical={'entity':1,'entity_id':88998248,'components':[],'authored_name':'Historical A'}
        self.other_historical={'entity':2,'entity_id':88932711,'components':[{'vtable_rva':'0x123'}]}
        self.nearby_openable={'entity':3,'entity_id':123456,'components':[{'vtable_rva':'0x1c569c8','generation':7}]}
        self.multiple_components={'entity':4,'entity_id':654321,'components':[{'vtable_rva':'0x123'},
            {'vtable_rva':'0x1c569c8','generation':9}]}
        self.lookalike={'entity':5,'entity_id':777,'components':[{'vtable_rva':'0x01c569c8'}]}
        self.actors=[self.historical,self.other_historical,self.nearby_openable,self.multiple_components,self.lookalike]

    def test_default_preserves_historical_entity_filter(self):
        selected=filter_actors(self.actors)
        self.assertEqual([actor['entity_id'] for actor in selected],[88998248,88932711])
        self.assertIs(selected[0],self.historical)
        self.assertIs(selected[1],self.other_historical)

    def test_nearby_mode_selects_every_exact_typed_openable(self):
        selected=filter_actors(self.actors,nearby_openables_only=True)
        self.assertEqual([actor['entity_id'] for actor in selected],[123456,654321])
        self.assertIs(selected[0],self.nearby_openable)
        self.assertIs(selected[1],self.multiple_components)
        self.assertNotIn('hand_state',selected[0])

    def test_nearby_mode_handles_missing_components_and_no_match(self):
        self.assertEqual(filter_actors([{'entity_id':1}],nearby_openables_only=True),[])
        self.assertEqual(filter_actors([],nearby_openables_only=True),[])


if __name__=='__main__':
    unittest.main()

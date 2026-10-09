"""Check affine order and reject replaced/unreadable native skinning buffers."""
import math
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from rig_fields import affine_product,skinning_snapshot


class SkinningCheck(unittest.TestCase):
    def test_noncommuting_rotation_translation(self):
        rotate=[0,-1,0,10,1,0,0,20,0,0,1,30]
        translate=[1,0,0,2,0,1,0,3,0,0,1,4]
        self.assertEqual(affine_product(rotate,translate),[0,-1,0,7,1,0,0,22,0,0,1,34])
        self.assertNotEqual(affine_product(rotate,translate),affine_product(translate,rotate))
        with self.assertRaises(ValueError): affine_product(rotate,[math.nan]*12)

    def fixture(self):
        base,skeleton,model,inverse,output,source=0x140000000,0x20000,0x40000,0x60000,0x80000,0xa0000
        memory={}
        def put(address,data): memory.update((address+i,b) for i,b in enumerate(data))
        def read(address,size): return bytes(memory[address+i] for i in range(size))
        for address,value in ((skeleton,base+0x1b0bd08),(skeleton+0x120,inverse),(model+0x1ff8,output)):
            put(address,struct.pack('<Q',value))
        transform=[0,1,0,0,-1,0,0,0,0,0,1,0,10,20,30,0]
        inv=[1,0,0,2,0,1,0,3,0,0,1,4]
        skin=[0,-1,0,7,1,0,0,22,0,0,1,34]
        put(inverse,struct.pack('<12f',*inv)*69)
        put(output,struct.pack('<12f',*skin)*69)
        put(source,struct.pack('<16f',*transform)*69)
        joints=[dict(joint=j,transform_address=source+j*64,position=[10,20,30],
                     rotation_columns=[[0,1,0],[-1,0,0],[0,0,1]]) for j in range(69)]
        pose=dict(skeleton=skeleton,model=model,skeleton_joints=69,pose_state=3,sampled_joints=joints)
        return read,put,pose,base,inverse,output,source

    def test_actual_output_matches_and_does_not_claim_mesh_proof(self):
        read,_,pose,base,*_=self.fixture()
        result=skinning_snapshot(read,pose,base)
        self.assertEqual(result['maximum_absolute_error'],0)
        self.assertFalse(result['snapshot_frame_synchronised'])
        self.assertFalse(result['mesh_skin_weights_verified'])

    def test_changed_source_suppresses_comparison(self):
        read,put,pose,base,_,_,source=self.fixture()
        put(source+48,struct.pack('<f',11))
        self.assertIsNone(skinning_snapshot(read,pose,base)['maximum_absolute_error'])

    def test_owned_body_palette_and_wrong_body_semantics(self):
        read,_,pose,base,*_=self.fixture()
        pose['skeleton_joints']=20;pose['sampled_joints']=pose['sampled_joints'][:20]
        pose['joint_names']=['center_spine03_jnt','center_head_jnt','left_ankle_jnt','right_ankle_jnt']
        self.assertEqual(skinning_snapshot(read,pose,base)['maximum_absolute_error'],0)
        pose['joint_names']=[]
        with self.assertRaises(ValueError):skinning_snapshot(read,pose,base)

    def test_replaced_inverse_bind_is_rejected(self):
        read,put,pose,base,inverse,*_=self.fixture()
        calls=0
        def changing(address,size):
            nonlocal calls
            if address==inverse:
                calls+=1
                if calls==2: put(inverse,struct.pack('<f',2))
            return read(address,size)
        with self.assertRaises(ValueError): skinning_snapshot(changing,pose,base)


if __name__=='__main__': unittest.main()

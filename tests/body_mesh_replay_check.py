"""Check the native affine order, weighted vertices and model frame boundary."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from render_body_rig import skin_positions,model_to_world
from rig_fields import affine_product


class BodyReplayCheck(unittest.TestCase):
    def test_noncommuting_native_palette_and_eight_weights(self):
        rows=affine_product([0,-1,0,10,1,0,0,20,0,0,1,30],
                            [1,0,0,2,0,1,0,3,0,0,1,4])
        palette=np.tile(np.array(rows).reshape(3,4),(20,1,1))
        vertex=([1,2,3],bytes((128,127,0,0,0,0,0,0)),bytes((0,19,255,255,255,255,255,255)))
        np.testing.assert_allclose(skin_positions([vertex],palette),[[5,23,37]])
        np.testing.assert_allclose(skin_positions([vertex],np.tile(palette[0],(69,1,1))),[[5,23,37]])

    def test_invalid_owned_vertex_palette(self):
        palette=np.tile(np.c_[np.eye(3),[0,0,0]],(20,1,1))
        for vertex in (([0,0,0],[255]*8,[0]*8),([0,0,0],[255]+[0]*7,[20]*8),
                       ([float('nan'),0,0],[255]+[0]*7,[0]*8)):
            with self.assertRaises(ValueError):skin_positions([vertex],palette)
        with self.assertRaises(ValueError):skin_positions([],palette)

    def test_scaled_native_world_frame_and_reflection(self):
        rig=dict(model_origin=[10,20,30],model_axis_rows=[[0,1,0],[-1,0,0],[0,0,1]],model_scale=[2,3,4])
        np.testing.assert_allclose(model_to_world([[1,2,3]],rig),[[4,22,42]])
        rig['model_axis_rows'][2]=[0,0,-1]
        with self.assertRaises(ValueError):model_to_world([[1,2,3]],rig)


if __name__=='__main__':unittest.main()

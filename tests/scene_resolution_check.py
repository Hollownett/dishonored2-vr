import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_scene_resolution import analyze


def draw(size, draw_id=1, targets=3, phase=3, texture=None):
    width,height = size
    texture = texture or size
    rows = [dict(phase=str(phase),event='draw_indexed',thread='1',context='10',slot=str(slot),
                 width=str(width),height=str(height),texture_width=str(texture[0]),texture_height=str(texture[1]),
                 format=str(10+slot),draw_id=str(draw_id)) for slot in range(targets)]
    rows.append(dict(rows[0],event='draw_indexed_depth',slot='0',format='39'))
    return rows


class SceneResolutionCheck(unittest.TestCase):
    def test_full_scene_geometry(self):
        result=analyze(draw((3102,3208)),[3102,3208])
        self.assertEqual(result['verdict'],'full_size_geometry_observed')

    def test_lower_scene_not_hidden_by_full_size_ui(self):
        result=analyze(draw((2636,2726))+draw((3102,3208),2,1),[3102,3208])
        self.assertEqual(result['verdict'],'scene_below_requested_size')
        self.assertLess(result['geometry_candidates'][0]['output_fraction'][0],.86)

    def test_shadow_and_single_target_copy_are_inconclusive(self):
        result=analyze(draw((2048,2048))+draw((3102,3208),2,1),[3102,3208])
        self.assertEqual(result['verdict'],'inconclusive_no_scene_geometry')

    def test_padded_allocation_keeps_actual_viewport(self):
        result=analyze(draw((3102,3208),texture=(4096,4096)),[3102,3208])
        self.assertEqual(result['geometry_candidates'][0]['viewport'],[3102,3208])

    def test_earlier_phase_cannot_prove_settled_scene(self):
        result=analyze(draw((3102,3208),phase=2),[3102,3208])
        self.assertEqual(result['verdict'],'inconclusive_no_scene_geometry')


if __name__ == '__main__':
    unittest.main()

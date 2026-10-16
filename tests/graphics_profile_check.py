import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from vr_graphics_profile import PROFILES, SETTINGS, edit_settings


class GraphicsProfileCheck(unittest.TestCase):
    def test_medium_changes_only_detail_settings_and_restores(self):
        source=(b'arkBind "PAD_RSTICK" "LOOK"\r\nr_textureDetailsPreset "5"\r\n'
                b'r_modelsQualityPreset "2"\r\nr_AntiAliasingPreset "3"\r\n'
                b'r_renderQualityPresets "2"\r\nark_playerBobAmount "0"\r\n')
        result,changes,_=edit_settings(source,PROFILES['vr-medium'])
        self.assertEqual(len(changes),6)
        self.assertTrue(all(value=='2' for value in PROFILES['vr-medium'].values()))
        self.assertIn(b'r_textureDetailsPreset "2"\r\n',result)
        for line in source.splitlines(keepends=True):
            if not line.startswith(b'r_textureDetailsPreset '): self.assertIn(line,result)
        restored,_,skipped=edit_settings(result,{key:value['before'] for key,value in changes.items()},
                                       {key:value['after'] for key,value in changes.items()})
        self.assertEqual(restored,source)
        self.assertFalse(skipped)

    def test_legacy_profile_preserved(self):
        self.assertEqual(PROFILES['legacy-high'],SETTINGS)
        self.assertEqual(SETTINGS['r_textureDetailsPreset'],'3')

    def test_preserve_bindings_and_unrelated_bytes(self):
        source=b'// keep\r\narkBind "F" "INTERACT"\r\nr_textureDetailsPreset "0"\r\nother "untouched"\r\n'
        result,changes,_=edit_settings(source,dict(r_textureDetailsPreset='3',view_skipShakes='1'))
        self.assertIn(b'arkBind "F" "INTERACT"\r\n',result)
        self.assertIn(b'other "untouched"\r\n',result)
        desired={key:value['before'] for key,value in changes.items()}
        expected={key:value['after'] for key,value in changes.items()}
        restored,_,_=edit_settings(result,desired,expected)
        self.assertEqual(restored,source)

    def test_restore_does_not_overwrite_user_choice(self):
        restored,_,skipped=edit_settings(b'r_textureDetailsPreset "5"\n',dict(r_textureDetailsPreset='0'),dict(r_textureDetailsPreset='3'))
        self.assertEqual(restored,b'r_textureDetailsPreset "5"\n')
        self.assertEqual(skipped,dict(r_textureDetailsPreset='5'))

    def test_duplicate_key_rejected(self):
        with self.assertRaises(ValueError):
            edit_settings(b'view_skipShakes "0"\nview_skipShakes "1"\n',dict(view_skipShakes='1'))

    def test_added_fields_removed_without_touching_other_changes(self):
        result,_,_=edit_settings(b'existing "x"',dict(view_skipShakes='1'))
        result+=b'new_user_setting "y"\n'
        restored,_,_=edit_settings(result,dict(view_skipShakes=None),dict(view_skipShakes='1'))
        self.assertEqual(restored,b'existing "x"\nnew_user_setting "y"\n')


if __name__=='__main__': unittest.main()

"""Pinned native movie provenance and reuse schema; no game process access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import gen_menu_patches as patches
from record_live_session import low_priority


def load_abc(path):
    spec = importlib.util.spec_from_file_location('native_movie_abc', ROOT/'artifacts/menu-re/abc.py')
    abc = importlib.util.module_from_spec(spec); spec.loader.exec_module(abc)
    source = path.read_bytes()
    for hit in re.finditer(b'\x10\x00\x2e\x00', source):
        try:
            value, reader = abc.parse(source, hit.start())
            if len(value['strings']) > 500:
                abc.parse_rest(value, reader)
                return abc, value
        except Exception:
            continue
    raise ValueError('No valid native ABC found')


class NativeMenuPatchCheck(unittest.TestCase):
    def test_generated_header_matches_verified_movies(self):
        generated = patches.generate(ROOT/'artifacts/menu-re/patched', ROOT/'artifacts/menu-re/ex')
        self.assertEqual(generated, (ROOT/'src/menu_patches.hpp').read_text())
        self.assertIn('original_sha256,patched_sha256', generated)
        for path in (ROOT/'artifacts/menu-re/patched').glob('*.iggy.patch.json'):
            _, old, new, edits = patches.movie_patch(path, ROOT/'artifacts/menu-re/ex')
            self.assertEqual(len(old), len(new))
            self.assertNotEqual(hashlib.sha256(old).digest(), hashlib.sha256(new).digest())
            self.assertGreater(len(edits), 0)

    def test_outside_edit_corruption_and_overlap_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); extracted=root/'ex'; extracted.mkdir()
            source = ROOT/'artifacts/menu-re/patched/corvo_pause_menu.iggy.patch.json'
            patch_file=root/source.name; shutil.copyfile(source,patch_file)
            shutil.copyfile(ROOT/'artifacts/menu-re/patched/ui_corvo_pause_menu.iggy', root/'ui_corvo_pause_menu.iggy')
            raw=bytearray((ROOT/'artifacts/menu-re/ex/ui_corvo_pause_menu.iggy').read_bytes())
            raw[20] ^= 1; (extracted/'ui_corvo_pause_menu.iggy').write_bytes(raw)
            with self.assertRaises(ValueError): patches.movie_patch(patch_file,extracted)
            shutil.copyfile(ROOT/'artifacts/menu-re/ex/ui_corvo_pause_menu.iggy', extracted/'ui_corvo_pause_menu.iggy')
            manifest=json.loads(patch_file.read_text()); manifest['patches'].append(manifest['patches'][0])
            patch_file.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): patches.movie_patch(patch_file,extracted)

    def test_native_options_owns_real_widgets_and_provider_schema(self):
        parser, movie = load_abc(ROOT/'artifacts/menu-re/ex/ui_lib_options.iggy')
        classes = {parser.mname(movie, value['name']): value for value in movie['inst']}
        def names(name): return {parser.mname(movie, trait['name']).split(':')[-1] for trait in classes[name]['traits']}
        self.assertTrue({'currentSection','options','listItems_pool','menuItems_pool'} <= names('com.bsp.options:OptionsData'))
        self.assertTrue({'optionStepperBoolean','optionStepper','slider','dropBox','link','widgetDefinition'} <= names('com.bsp.options:SettingDefinition'))
        self.assertTrue({'optionsMenu','header','glass','blades'} <= names('com.bsp.options:OptionsScreen'))
        self.assertIn('com.bsp.options:PauseOptionsScreen', classes)


if __name__ == '__main__':
    low_priority()
    unittest.main()
